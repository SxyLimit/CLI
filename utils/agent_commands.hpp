#pragma once

#include "agent_state.hpp"
#include "../tools/agent/fs_read.hpp"
#include "../tools/agent/fs_write.hpp"
#include "../tools/agent/fs_tree.hpp"

#include <tuple>

namespace agent {

inline ToolExecutionResult command_todo_plan(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto goal = args.get("--goal");
  if(goal.empty()){
    return json_error("missing --goal");
  }
  auto title = args.get("--title");
  auto planId = args.get("--plan-id");
  auto mode = args.get("--mode", "minimal");
  auto& plan = state().todo.create_plan(goal, title, planId, mode);
  sj::Object data;
  data.emplace("plan", plan_to_json(plan));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_todo_view(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto planId = args.get("--plan");
  if(planId.empty()) return json_error("missing --plan");
  auto record = state().todo.get_record(planId);
  if(!record) return json_error("plan not found", "not_found");
  sj::Object data;
  data.emplace("plan", plan_to_json(record->plan));
  if(args.flags.count("--include-history")){
    data.emplace("events", events_to_json(record->events));
    sj::Array snapshots;
    for(const auto& snap : record->snapshots){
      snapshots.emplace_back(plan_snapshot_to_json(snap));
    }
    data.emplace("snapshots", sj::Value(std::move(snapshots)));
    data.emplace("signals", signals_to_json(record->signals));
  }
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult require_plan_with_version(const ParsedArgs& args,
                                                     PlanRecord*& record,
                                                     Plan*& plan){
  auto planId = args.get("--plan");
  if(planId.empty()) return json_error("missing --plan");
  record = state().todo.get_record(planId);
  if(!record) return json_error("plan not found", "not_found");
  plan = &record->plan;
  auto versionText = args.get("--expected-version");
  if(versionText.empty()) return json_error("missing --expected-version");
  int expected = static_cast<int>(parse_ll(versionText, -1));
  if(expected != plan->version){
    sj::Object err;
    err.emplace("plan_id", sj::Value(plan->id));
    err.emplace("current_version", sj::Value(plan->version));
    err.emplace("expected_version", sj::Value(expected));
    return json_result(sj::Value(std::move(err)), 2);
  }
  return ToolExecutionResult{};
}

inline ToolExecutionResult command_todo_update(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  PlanRecord* record = nullptr;
  Plan* plan = nullptr;
  auto error = require_plan_with_version(args, record, plan);
  if(error.exitCode != 0) return error;
  auto stepId = args.get("--step");
  if(stepId.empty()) return json_error("missing --step");
  auto* step = state().todo.find_step(*plan, stepId);
  if(!step) return json_error("step not found", "not_found");
  state().todo.push_undo(*record);
  if(args.has("--title")) step->title = args.get("--title");
  if(args.has("--description")) step->description = args.get("--description");
  if(args.has("--priority")) step->priority = static_cast<int>(parse_ll(args.get("--priority"), step->priority));
  if(args.has("--owner")) step->owner = args.get("--owner");
  if(args.has("--acceptance")) step->acceptance = args.get("--acceptance");
  if(args.has("--estimate")){
    step->estimateHours = parse_double(args.get("--estimate"), step->estimateHours);
    step->hasEstimate = true;
  }
  for(const auto& value : args.getList("--add-tag")){
    if(std::find(step->tags.begin(), step->tags.end(), value) == step->tags.end()){
      step->tags.push_back(value);
    }
  }
  for(const auto& value : args.getList("--remove-tag")){
    auto it = std::remove(step->tags.begin(), step->tags.end(), value);
    step->tags.erase(it, step->tags.end());
  }
  bump_version(*plan);
  state().todo.record_event(*record, "update", "updated step " + step->id);
  sj::Object data;
  data.emplace("plan", plan_to_json(*plan));
  data.emplace("step", step_to_json(*step));
  return json_success(sj::Value(std::move(data)));
}

inline size_t find_step_index(const Plan& plan, const std::string& stepId){
  for(size_t i = 0; i < plan.steps.size(); ++i){
    if(plan.steps[i].id == stepId) return i;
  }
  return static_cast<size_t>(-1);
}

inline ToolExecutionResult command_todo_add(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  PlanRecord* record = nullptr;
  Plan* plan = nullptr;
  auto error = require_plan_with_version(args, record, plan);
  if(error.exitCode != 0) return error;
  auto title = args.get("--title");
  if(title.empty()) return json_error("missing --title");
  Step step;
  step.id = ensure_step_id(*plan);
  step.title = title;
  step.description = args.get("--description");
  if(args.has("--priority")) step.priority = static_cast<int>(parse_ll(args.get("--priority"), 0));
  if(args.has("--status")) step.status = args.get("--status", "pending");
  if(args.has("--owner")) step.owner = args.get("--owner");
  if(args.has("--acceptance")) step.acceptance = args.get("--acceptance");
  if(args.has("--estimate")){
    step.estimateHours = parse_double(args.get("--estimate"), 0.0);
    step.hasEstimate = true;
  }
  if(args.has("--depends")) step.dependencies = split_csv(args.get("--depends"));
  if(args.has("--tags")) step.tags = split_csv(args.get("--tags"));
  state().todo.push_undo(*record);
  auto afterId = args.get("--after");
  size_t insertIndex = plan->steps.size();
  if(afterId.empty()){
    plan->steps.push_back(step);
    insertIndex = plan->steps.size() - 1;
  }else{
    size_t index = find_step_index(*plan, afterId);
    if(index == static_cast<size_t>(-1)){
      return json_error("after step not found", "not_found");
    }
    insertIndex = index + 1;
    plan->steps.insert(plan->steps.begin() + insertIndex, step);
  }
  if(state().todo.has_dependency_cycle(*plan)){
    plan->steps.erase(plan->steps.begin() + insertIndex);
    return json_error("dependency cycle detected", "cycle");
  }
  bump_version(*plan);
  state().todo.record_event(*record, "add", "added step " + step.id);
  sj::Object data;
  data.emplace("plan", plan_to_json(*plan));
  data.emplace("step", step_to_json(plan->steps[insertIndex]));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_todo_remove(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  PlanRecord* record = nullptr;
  Plan* plan = nullptr;
  auto error = require_plan_with_version(args, record, plan);
  if(error.exitCode != 0) return error;
  auto steps = args.getList("--step");
  if(steps.empty()) return json_error("missing --step");
  std::unordered_set<std::string> removeSet(steps.begin(), steps.end());
  std::vector<std::string> blockers;
  for(const auto& step : plan->steps){
    for(const auto& dep : step.dependencies){
      if(removeSet.count(dep) && !removeSet.count(step.id)){
        blockers.push_back(step.id);
      }
    }
  }
  if(!blockers.empty()){
    sj::Object err;
    sj::Array arr;
    for(const auto& id : blockers) arr.emplace_back(sj::Value(id));
    err.emplace("dependent_steps", sj::Value(std::move(arr)));
    return json_result(sj::Value(std::move(err)), 2);
  }
  state().todo.push_undo(*record);
  plan->steps.erase(std::remove_if(plan->steps.begin(), plan->steps.end(), [&](const Step& step){
    return removeSet.count(step.id) > 0;
  }), plan->steps.end());
  for(const auto& id : steps){
    remove_step_from_dependencies(*plan, id);
  }
  bump_version(*plan);
  state().todo.record_event(*record, "remove", "removed steps" );
  sj::Object data;
  data.emplace("plan", plan_to_json(*plan));
  return json_success(sj::Value(std::move(data)));
}

inline bool validate_order(const Plan& plan, const std::vector<std::string>& order, std::string& errorStep){
  std::unordered_map<std::string, size_t> positions;
  for(size_t i = 0; i < order.size(); ++i){
    positions[order[i]] = i;
  }
  for(const auto& step : plan.steps){
    auto it = positions.find(step.id);
    if(it == positions.end()){
      errorStep = step.id;
      return false;
    }
    for(const auto& dep : step.dependencies){
      auto jt = positions.find(dep);
      if(jt == positions.end()) continue;
      if(jt->second > it->second){
        errorStep = step.id;
        return false;
      }
    }
  }
  return true;
}

inline ToolExecutionResult command_todo_reorder(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  PlanRecord* record = nullptr;
  Plan* plan = nullptr;
  auto error = require_plan_with_version(args, record, plan);
  if(error.exitCode != 0) return error;
  auto orderText = args.get("--order");
  if(orderText.empty()) return json_error("missing --order");
  auto order = split_csv(orderText);
  if(order.size() != plan->steps.size()) return json_error("order length mismatch");
  std::string errorStep;
  if(!validate_order(*plan, order, errorStep)){
    sj::Object err;
    err.emplace("conflict_step", sj::Value(errorStep));
    return json_result(sj::Value(std::move(err)), 2);
  }
  state().todo.push_undo(*record);
  std::vector<Step> newSteps;
  for(const auto& id : order){
    auto* step = state().todo.find_step(*plan, id);
    if(!step) return json_error("unknown step in order", "not_found");
    newSteps.push_back(*step);
  }
  plan->steps = std::move(newSteps);
  bump_version(*plan);
  state().todo.record_event(*record, "reorder", "reordered steps");
  sj::Object data;
  data.emplace("plan", plan_to_json(*plan));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult apply_dependencies(Plan& plan,
                                              Step& step,
                                              const std::vector<std::string>& deps,
                                              bool replace){
  auto original = step.dependencies;
  if(replace){
    step.dependencies = deps;
  }else{
    for(const auto& dep : deps){
      if(std::find(step.dependencies.begin(), step.dependencies.end(), dep) == step.dependencies.end()){
        step.dependencies.push_back(dep);
      }
    }
  }
  if(state().todo.has_dependency_cycle(plan)){
    step.dependencies = original;
    sj::Array cycle;
    auto list = state().todo.dependency_cycle(plan);
    for(const auto& id : list) cycle.emplace_back(sj::Value(id));
    sj::Object err;
    err.emplace("cycle", sj::Value(std::move(cycle)));
    return json_result(sj::Value(std::move(err)), 2);
  }
  return ToolExecutionResult{};
}

inline ToolExecutionResult command_todo_dep_set(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  PlanRecord* record = nullptr;
  Plan* plan = nullptr;
  auto error = require_plan_with_version(args, record, plan);
  if(error.exitCode != 0) return error;
  auto stepId = args.get("--step");
  if(stepId.empty()) return json_error("missing --step");
  auto deps = split_csv(args.get("--deps"));
  auto* step = state().todo.find_step(*plan, stepId);
  if(!step) return json_error("step not found", "not_found");
  state().todo.push_undo(*record);
  auto guard = apply_dependencies(*plan, *step, deps, true);
  if(guard.exitCode != 0) return guard;
  bump_version(*plan);
  state().todo.record_event(*record, "dep.set", "reset dependencies for " + step->id);
  sj::Object data;
  data.emplace("plan", plan_to_json(*plan));
  data.emplace("step", step_to_json(*step));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_todo_dep_add(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  PlanRecord* record = nullptr;
  Plan* plan = nullptr;
  auto error = require_plan_with_version(args, record, plan);
  if(error.exitCode != 0) return error;
  auto stepId = args.get("--step");
  if(stepId.empty()) return json_error("missing --step");
  auto deps = split_csv(args.get("--deps"));
  auto* step = state().todo.find_step(*plan, stepId);
  if(!step) return json_error("step not found", "not_found");
  state().todo.push_undo(*record);
  auto guard = apply_dependencies(*plan, *step, deps, false);
  if(guard.exitCode != 0) return guard;
  bump_version(*plan);
  state().todo.record_event(*record, "dep.add", "added dependencies for " + step->id);
  sj::Object data;
  data.emplace("plan", plan_to_json(*plan));
  data.emplace("step", step_to_json(*step));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_todo_dep_remove(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  PlanRecord* record = nullptr;
  Plan* plan = nullptr;
  auto error = require_plan_with_version(args, record, plan);
  if(error.exitCode != 0) return error;
  auto stepId = args.get("--step");
  if(stepId.empty()) return json_error("missing --step");
  auto deps = split_csv(args.get("--deps"));
  auto* step = state().todo.find_step(*plan, stepId);
  if(!step) return json_error("step not found", "not_found");
  state().todo.push_undo(*record);
  for(const auto& dep : deps){
    auto it = std::remove(step->dependencies.begin(), step->dependencies.end(), dep);
    step->dependencies.erase(it, step->dependencies.end());
  }
  bump_version(*plan);
  state().todo.record_event(*record, "dep.remove", "removed dependencies for " + step->id);
  sj::Object data;
  data.emplace("plan", plan_to_json(*plan));
  data.emplace("step", step_to_json(*step));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_todo_split(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  PlanRecord* record = nullptr;
  Plan* plan = nullptr;
  auto error = require_plan_with_version(args, record, plan);
  if(error.exitCode != 0) return error;
  auto stepId = args.get("--step");
  if(stepId.empty()) return json_error("missing --step");
  auto* step = state().todo.find_step(*plan, stepId);
  if(!step) return json_error("step not found", "not_found");
  auto children = args.getList("--child");
  if(children.empty()) return json_error("missing --child");
  state().todo.push_undo(*record);
  bool keepParent = args.flags.count("--keep-parent") > 0;
  size_t insertPos = 0;
  for(size_t i = 0; i < plan->steps.size(); ++i){
    if(plan->steps[i].id == stepId){
      insertPos = i + 1;
      break;
    }
  }
  std::vector<Step> newSteps;
  for(const auto& token : children){
    Step child;
    child.id = ensure_step_id(*plan);
    auto parts = split_csv(token);
    if(parts.empty()){
      child.title = token;
    }else{
      child.title = parts[0];
      if(parts.size() > 1) child.description = parts[1];
    }
    child.parentId = step->id;
    child.status = "pending";
    newSteps.push_back(child);
  }
  if(keepParent){
    step->virtualParent = true;
    step->status = "pending";
  }else{
    plan->steps.erase(plan->steps.begin() + (insertPos - 1));
    insertPos -= 1;
  }
  plan->steps.insert(plan->steps.begin() + insertPos, newSteps.begin(), newSteps.end());
  bump_version(*plan);
  state().todo.record_event(*record, "split", "split step " + stepId);
  sj::Object data;
  data.emplace("plan", plan_to_json(*plan));
  sj::Array arr;
  for(size_t idx = insertPos; idx < insertPos + newSteps.size(); ++idx){
    arr.emplace_back(step_to_json(plan->steps[idx]));
  }
  data.emplace("children", sj::Value(std::move(arr)));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_todo_merge(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  PlanRecord* record = nullptr;
  Plan* plan = nullptr;
  auto error = require_plan_with_version(args, record, plan);
  if(error.exitCode != 0) return error;
  auto steps = split_csv(args.get("--steps"));
  if(steps.size() < 2) return json_error("need at least two steps");
  std::vector<size_t> indices;
  for(const auto& id : steps){
    size_t idx = find_step_index(*plan, id);
    if(idx == static_cast<size_t>(-1)) return json_error("step not found", "not_found");
    indices.push_back(idx);
  }
  std::sort(indices.begin(), indices.end());
  Step merged;
  merged.id = ensure_step_id(*plan);
  merged.title = args.get("--title", "Merged step");
  merged.description = args.get("--description");
  merged.priority = args.has("--priority") ? static_cast<int>(parse_ll(args.get("--priority"), 0)) : 0;
  merged.acceptance = args.get("--acceptance");
  merged.owner = args.get("--owner");
  for(auto idx : indices){
    const auto& src = plan->steps[idx];
    merged.dependencies.insert(merged.dependencies.end(), src.dependencies.begin(), src.dependencies.end());
    merged.tags.insert(merged.tags.end(), src.tags.begin(), src.tags.end());
    merged.artifacts.insert(merged.artifacts.end(), src.artifacts.begin(), src.artifacts.end());
    merged.links.insert(merged.links.end(), src.links.begin(), src.links.end());
  }
  std::sort(merged.dependencies.begin(), merged.dependencies.end());
  merged.dependencies.erase(std::unique(merged.dependencies.begin(), merged.dependencies.end()), merged.dependencies.end());
  std::sort(merged.tags.begin(), merged.tags.end());
  merged.tags.erase(std::unique(merged.tags.begin(), merged.tags.end()), merged.tags.end());
  state().todo.push_undo(*record);
  Step firstStep = plan->steps[indices.front()];
  plan->steps.erase(std::remove_if(plan->steps.begin(), plan->steps.end(), [&](const Step& s){
    return std::find(steps.begin(), steps.end(), s.id) != steps.end();
  }), plan->steps.end());
  plan->steps.insert(plan->steps.begin() + indices.front(), merged);
  bump_version(*plan);
  state().todo.record_event(*record, "merge", "merged steps into " + merged.id);
  sj::Object data;
  data.emplace("plan", plan_to_json(*plan));
  data.emplace("step", step_to_json(plan->steps[indices.front()]));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_todo_mark(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  PlanRecord* record = nullptr;
  Plan* plan = nullptr;
  auto error = require_plan_with_version(args, record, plan);
  if(error.exitCode != 0) return error;
  auto stepId = args.get("--step");
  auto status = args.get("--status");
  if(stepId.empty() || status.empty()) return json_error("missing --step or --status");
  auto* step = state().todo.find_step(*plan, stepId);
  if(!step) return json_error("step not found", "not_found");
  if(status == "done" && !dependencies_done(*plan, *step)){
    sj::Array deps;
    for(const auto& dep : step->dependencies){
      const Step* d = state().todo.find_step(*plan, dep);
      if(d && d->status != "done") deps.emplace_back(sj::Value(dep));
    }
    sj::Object err;
    err.emplace("blocked_by", sj::Value(std::move(deps)));
    return json_result(sj::Value(std::move(err)), 2);
  }
  state().todo.push_undo(*record);
  step->status = status;
  if(args.has("--reason")){
    StepNote note;
    note.id = random_id("note-");
    note.text = args.get("--reason");
    note.timestamp = now_iso8601();
    step->notes.push_back(std::move(note));
  }
  if(args.has("--artifact")){
    auto art = args.get("--artifact");
    if(std::find(step->artifacts.begin(), step->artifacts.end(), art) == step->artifacts.end()){
      step->artifacts.push_back(art);
    }
  }
  bump_version(*plan);
  state().todo.record_event(*record, "mark", "marked " + step->id + " as " + status);
  sj::Object data;
  data.emplace("plan", plan_to_json(*plan));
  data.emplace("step", step_to_json(*step));
  return json_success(sj::Value(std::move(data)));
}

inline ChecklistItem* find_checklist_item(Step& step, const std::string& itemId){
  for(auto& item : step.checklist){
    if(item.id == itemId) return &item;
  }
  return nullptr;
}

inline ToolExecutionResult command_todo_checklist(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  PlanRecord* record = nullptr;
  Plan* plan = nullptr;
  auto error = require_plan_with_version(args, record, plan);
  if(error.exitCode != 0) return error;
  auto stepId = args.get("--step");
  auto op = args.get("--op");
  if(stepId.empty() || op.empty()) return json_error("missing --step or --op");
  auto* step = state().todo.find_step(*plan, stepId);
  if(!step) return json_error("step not found", "not_found");
  state().todo.push_undo(*record);
  if(op == "add"){
    ChecklistItem item;
    item.id = random_id("chk-");
    item.text = args.get("--text");
    item.createdAt = now_iso8601();
    item.updatedAt = item.createdAt;
    step->checklist.push_back(item);
  }else if(op == "remove"){
    auto id = args.get("--item");
    auto it = std::remove_if(step->checklist.begin(), step->checklist.end(), [&](const ChecklistItem& item){ return item.id == id; });
    step->checklist.erase(it, step->checklist.end());
  }else if(op == "toggle"){
    auto id = args.get("--item");
    auto* item = find_checklist_item(*step, id);
    if(!item) return json_error("checklist item not found", "not_found");
    item->done = !item->done;
    item->updatedAt = now_iso8601();
  }else if(op == "rename"){
    auto id = args.get("--item");
    auto* item = find_checklist_item(*step, id);
    if(!item) return json_error("checklist item not found", "not_found");
    item->text = args.get("--text", item->text);
    item->updatedAt = now_iso8601();
  }else{
    return json_error("unknown op");
  }
  bump_version(*plan);
  state().todo.record_event(*record, "checklist", "updated checklist of " + step->id);
  sj::Object data;
  data.emplace("plan", plan_to_json(*plan));
  data.emplace("step", step_to_json(*step));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_todo_annotate(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  PlanRecord* record = nullptr;
  Plan* plan = nullptr;
  auto error = require_plan_with_version(args, record, plan);
  if(error.exitCode != 0) return error;
  auto stepId = args.get("--step");
  if(stepId.empty()) return json_error("missing --step");
  auto* step = state().todo.find_step(*plan, stepId);
  if(!step) return json_error("step not found", "not_found");
  state().todo.push_undo(*record);
  if(args.has("--note")){
    StepNote note;
    note.id = random_id("note-");
    note.text = args.get("--note");
    note.timestamp = now_iso8601();
    step->notes.push_back(std::move(note));
  }
  for(const auto& art : args.getList("--artifacts-add")){
    if(std::find(step->artifacts.begin(), step->artifacts.end(), art) == step->artifacts.end()) step->artifacts.push_back(art);
  }
  for(const auto& art : args.getList("--artifacts-remove")){
    auto it = std::remove(step->artifacts.begin(), step->artifacts.end(), art);
    step->artifacts.erase(it, step->artifacts.end());
  }
  for(const auto& link : args.getList("--links-add")){
    if(std::find(step->links.begin(), step->links.end(), link) == step->links.end()) step->links.push_back(link);
  }
  bump_version(*plan);
  state().todo.record_event(*record, "annotate", "annotated " + step->id);
  sj::Object data;
  data.emplace("plan", plan_to_json(*plan));
  data.emplace("step", step_to_json(*step));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_todo_block(const ToolExecutionRequest& request, bool block){
  auto args = parse_args(request.tokens, 1);
  PlanRecord* record = nullptr;
  Plan* plan = nullptr;
  auto error = require_plan_with_version(args, record, plan);
  if(error.exitCode != 0) return error;
  auto stepId = args.get("--step");
  if(stepId.empty()) return json_error("missing --step");
  auto* step = state().todo.find_step(*plan, stepId);
  if(!step) return json_error("step not found", "not_found");
  state().todo.push_undo(*record);
  step->blocked = block;
  if(block) step->blockReason = args.get("--reason");
  else step->blockReason.clear();
  bump_version(*plan);
  state().todo.record_event(*record, block ? "block" : "unblock", (block ? "blocked " : "unblocked ") + step->id);
  sj::Object data;
  data.emplace("plan", plan_to_json(*plan));
  data.emplace("step", step_to_json(*step));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_todo_snapshot(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto planId = args.get("--plan");
  if(planId.empty()) return json_error("missing --plan");
  auto record = state().todo.get_record(planId);
  if(!record) return json_error("plan not found", "not_found");
  PlanSnapshotRecord snap;
  snap.snapshotId = random_id("plan-snap-");
  snap.reason = args.get("--reason");
  snap.createdAt = now_iso8601();
  snap.plan = record->plan;
  record->snapshots.push_back(snap);
  sj::Object data;
  data.emplace("snapshot", plan_snapshot_to_json(snap));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_todo_history(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto planId = args.get("--plan");
  if(planId.empty()) return json_error("missing --plan");
  auto record = state().todo.get_record(planId);
  if(!record) return json_error("plan not found", "not_found");
  int limit = static_cast<int>(parse_ll(args.get("--limit"), 0));
  sj::Array arr;
  auto begin = record->events.rbegin();
  auto end = record->events.rend();
  int count = 0;
  for(auto it = begin; it != end; ++it){
    arr.emplace_back(log_event_to_json({it->eventId, planId, "", it->type, it->detail, it->timestamp, it->version}));
    if(limit > 0 && ++count >= limit) break;
  }
  sj::Object data;
  data.emplace("events", sj::Value(std::move(arr)));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_todo_undo(const ToolExecutionRequest& request, bool redo){
  auto args = parse_args(request.tokens, 1);
  auto planId = args.get("--plan");
  if(planId.empty()) return json_error("missing --plan");
  auto record = state().todo.get_record(planId);
  if(!record) return json_error("plan not found", "not_found");
  int steps = static_cast<int>(parse_ll(args.get("--steps"), 1));
  int applied = 0;
  for(int i = 0; i < steps; ++i){
    if(redo){
      if(record->redoStack.empty()) break;
      record->undoStack.push_back(record->plan);
      record->plan = record->redoStack.back();
      record->redoStack.pop_back();
    }else{
      if(record->undoStack.empty()) break;
      record->redoStack.push_back(record->plan);
      record->plan = record->undoStack.back();
      record->undoStack.pop_back();
    }
    ++applied;
  }
  sj::Object data;
  data.emplace("plan", plan_to_json(record->plan));
  data.emplace("applied", sj::Value(applied));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_todo_brief(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto planId = args.get("--plan");
  if(planId.empty()) return json_error("missing --plan");
  auto record = state().todo.get_record(planId);
  if(!record) return json_error("plan not found", "not_found");
  int kDone = static_cast<int>(parse_ll(args.get("--k-done"), 3));
  int kNext = static_cast<int>(parse_ll(args.get("--k-next"), 3));
  int tokenCap = static_cast<int>(parse_ll(args.get("--token-cap"), 200));
  auto blockers = collect_blockers(record->plan);
  auto done = find_done(record->plan);
  if(static_cast<int>(done.size()) > kDone) done.erase(done.begin(), done.end() - kDone);
  Step* now = find_first_running(record->plan);
  if(!now) now = find_first_pending(record->plan);
  std::vector<std::string> next;
  bool foundNow = false;
  for(auto& step : record->plan.steps){
    if(&step == now){
      foundNow = true;
      continue;
    }
    if(foundNow && step.status == "pending"){ next.push_back(step.id); }
    if(static_cast<int>(next.size()) >= kNext) break;
  }
  std::ostringstream oss;
  oss << "Goal: " << record->plan.goal << "\n";
  if(now){
    oss << "Now: [" << now->id << "] " << now->title << "\n";
  }else{
    oss << "Now: <none>\n";
  }
  oss << "Done: " << join_csv(done) << "\n";
  oss << "Next: " << join_csv(next) << "\n";
  oss << "Blockers: " << join_csv(blockers) << "\n";
  if(now){
    oss << "Acceptance: " << now->acceptance << "\n";
  }
  std::string mic = oss.str();
  if(static_cast<int>(mic.size()) > tokenCap * 4){
    mic.resize(tokenCap * 4);
  }
  sj::Object data;
  data.emplace("mic_text", sj::Value(mic));
  data.emplace("now_step_id", sj::Value(now ? now->id : ""));
  sj::Array arrDone;
  for(const auto& id : done) arrDone.emplace_back(sj::Value(id));
  data.emplace("done_step_ids", sj::Value(std::move(arrDone)));
  sj::Array arrNext;
  for(const auto& id : next) arrNext.emplace_back(sj::Value(id));
  data.emplace("next_step_ids", sj::Value(std::move(arrNext)));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_todo_signal(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto planId = args.get("--plan");
  if(planId.empty()) return json_error("missing --plan");
  auto record = state().todo.get_record(planId);
  if(!record) return json_error("plan not found", "not_found");
  SignalRecord sig;
  sig.timestamp = now_iso8601();
  sig.type = args.get("--type");
  sig.stepId = args.get("--step");
  sig.note = args.get("--note");
  sig.artifact = args.get("--artifact");
  sig.reason = args.get("--reason");
  if(sig.type.empty()) return json_error("missing --type");
  record->signals.push_back(sig);
  sj::Object data;
  data.emplace("plan", plan_to_json(record->plan));
  data.emplace("signal", signals_to_json({sig}));
  return json_success(sj::Value(std::move(data)));
}

// ===== Context Commands =====

inline ToolExecutionResult command_ctx_scope(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto taskId = args.get("--task");
  if(taskId.empty()) return json_error("missing --task");
  std::vector<std::string> allow;
  for(const auto& value : args.getList("--allow")) allow.push_back(value);
  std::vector<std::string> deny;
  for(const auto& value : args.getList("--deny")) deny.push_back(value);
  std::vector<std::string> types;
  for(const auto& value : args.getList("--type")) types.push_back(value);
  auto& scope = state().context.set_scope(taskId, std::move(allow), std::move(types), std::move(deny));
  sj::Object obj;
  sj::Array allowArr;
  for(const auto& p : scope.allowPaths) allowArr.emplace_back(sj::Value(p));
  sj::Array denyArr;
  for(const auto& p : scope.denyPaths) denyArr.emplace_back(sj::Value(p));
  sj::Array typeArr;
  for(const auto& t : scope.allowTypes) typeArr.emplace_back(sj::Value(t));
  obj.emplace("task", sj::Value(scope.taskId));
  obj.emplace("allow_paths", sj::Value(std::move(allowArr)));
  obj.emplace("deny_paths", sj::Value(std::move(denyArr)));
  obj.emplace("allow_types", sj::Value(std::move(typeArr)));
  return json_success(sj::Value(std::move(obj)));
}

inline ToolExecutionResult command_ctx_capture(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  ContextEntry entry;
  entry.taskId = args.get("--task");
  entry.type = args.get("--type", "log");
  entry.title = args.get("--title");
  entry.summary = args.get("--summary");
  entry.source = args.get("--source");
  entry.payloadRef = args.get("--payload-ref");
  entry.keywords = split_csv(args.get("--keywords"));
  entry.tags = split_csv(args.get("--tags"));
  for(const auto& path : args.getList("--path")) entry.paths.push_back(path);
  auto& stored = state().context.capture(entry);
  sj::Object data;
  data.emplace("entry_id", sj::Value(stored.id));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_ctx_pin(const ToolExecutionRequest& request, bool pin){
  auto args = parse_args(request.tokens, 1);
  auto ids = args.getList("--entry");
  if(ids.empty()) return json_error("missing --entry");
  auto changed = state().context.pin(ids, pin);
  sj::Array arr;
  for(const auto& id : changed) arr.emplace_back(sj::Value(id));
  sj::Object data;
  data.emplace("entries", sj::Value(std::move(arr)));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_ctx_pack_for_mic(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto task = args.get("--task");
  int tokenCap = static_cast<int>(parse_ll(args.get("--token-cap"), 200));
  auto types = split_csv(args.get("--type-priority"));
  auto [text, used] = state().context.pack_for_mic(task, tokenCap, types);
  sj::Array arr;
  for(const auto& id : used) arr.emplace_back(sj::Value(id));
  sj::Object data;
  data.emplace("text", sj::Value(text));
  data.emplace("entries", sj::Value(std::move(arr)));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_ctx_inject_todo(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  state().context.mic.micText = args.get("--mic-text");
  state().context.mic.sideText = args.get("--side-text");
  state().context.mic.pinned = args.flags.count("--unpinned") == 0;
  state().context.mic.priority = args.get("--priority", "high");
  sj::Object data;
  data.emplace("mic_text", sj::Value(state().context.mic.micText));
  data.emplace("side_text", sj::Value(state().context.mic.sideText));
  data.emplace("pinned", sj::Value(state().context.mic.pinned));
  data.emplace("priority", sj::Value(state().context.mic.priority));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_ctx_placeholder(const std::string& feature){
  sj::Object data;
  data.emplace("feature", sj::Value(feature));
  data.emplace("status", sj::Value("not_enabled"));
  return json_success(sj::Value(std::move(data)));
}

// ===== Guard Commands =====

inline ToolExecutionResult command_guard_fs(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto op = args.get("--op");
  auto path = args.get("--path");
  auto size = parse_ll(args.get("--size"), 0);
  if(op.empty() || path.empty()) return json_error("missing --op or --path");
  auto decision = state().guard.fs_guard(op, path, size);
  sj::Object data;
  data.emplace("allowed", sj::Value(decision.allowed));
  data.emplace("require_snapshot", sj::Value(decision.requireSnapshot));
  data.emplace("reason", sj::Value(decision.reason));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_guard_shell(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto command = args.get("--command");
  if(command.empty()) return json_error("missing --command");
  auto decision = state().guard.shell_guard(command);
  sj::Object data;
  data.emplace("allowed", sj::Value(decision.allowed));
  data.emplace("require_snapshot", sj::Value(decision.requireSnapshot));
  data.emplace("reason", sj::Value(decision.reason));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_guard_net(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto host = args.get("--host");
  auto decision = state().guard.net_guard(host);
  sj::Object data;
  data.emplace("allowed", sj::Value(decision.allowed));
  data.emplace("reason", sj::Value(decision.reason));
  return json_success(sj::Value(std::move(data)));
}

// ===== Exec Commands =====

inline ToolExecutionResult command_exec_shell(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto command = args.get("--command");
  if(command.empty()) return json_error("missing --command");
  auto result = tool::detail::execute_shell(request, command, true);
  sj::Object data;
  data.emplace("exit_code", sj::Value(result.exitCode));
  data.emplace("stdout", sj::Value(result.output));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_exec_python(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  std::string script = args.get("--script");
  std::string code = args.get("--code");
  std::string command;
  if(!script.empty()){
    command = "python3 " + shellEscape(script);
  }else if(!code.empty()){
    auto temp = std::filesystem::temp_directory_path() / (random_id("agent_python_") + ".py");
    std::ofstream ofs(temp);
    ofs << code;
    ofs.close();
    command = "python3 " + shellEscape(temp.string());
  }else{
    return json_error("missing --script or --code");
  }
  auto result = tool::detail::execute_shell(request, command, true);
  sj::Object data;
  data.emplace("exit_code", sj::Value(result.exitCode));
  data.emplace("stdout", sj::Value(result.output));
  return json_success(sj::Value(std::move(data)));
}

// ===== Filesystem snapshot/diff =====

inline ToolExecutionResult command_fs_read(const ToolExecutionRequest& request){
  ToolExecutionRequest forwarded = request;
  forwarded.tokens = request.tokens;
  if(!forwarded.tokens.empty()) forwarded.tokens[0] = "fs.read";
  auto result = tool::FsRead::run(forwarded);
  if(result.exitCode != 0) return result;
  sj::Object data;
  data.emplace("content", sj::Value(result.output));
  if(result.metaJson){
    sj::Parser parser(*result.metaJson);
    data.emplace("meta", parser.parse());
  }
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_fs_write_safe(const ToolExecutionRequest& request){
  ToolExecutionRequest forwarded = request;
  forwarded.tokens = request.tokens;
  if(!forwarded.tokens.empty()) forwarded.tokens[0] = "fs.write";
  auto result = tool::FsWrite::run(forwarded);
  if(result.exitCode != 0) return result;
  sj::Object data;
  if(result.metaJson){
    sj::Parser parser(*result.metaJson);
    data.emplace("meta", parser.parse());
  }
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_fs_snapshot(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto paths = args.getList("--path");
  if(paths.empty()) return json_error("missing --path");
  auto snap = state().fsSnapshots.create(paths, args.get("--reason"));
  sj::Object data;
  data.emplace("snapshot", sj::Value(snap.id));
  data.emplace("file_count", sj::Value(static_cast<long long>(snap.files.size())));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_fs_diff(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto fromId = args.get("--from");
  auto toId = args.get("--to");
  if(fromId.empty() || toId.empty()) return json_error("missing --from or --to");
  auto from = state().fsSnapshots.get(fromId);
  auto to = state().fsSnapshots.get(toId);
  if(!from || !to) return json_error("snapshot not found", "not_found");
  std::set<std::string> allPaths;
  for(auto& kv : from->files) allPaths.insert(kv.first);
  for(auto& kv : to->files) allPaths.insert(kv.first);
  sj::Array added, removed, changed;
  for(const auto& path : allPaths){
    auto itFrom = from->files.find(path);
    auto itTo = to->files.find(path);
    if(itFrom == from->files.end()){
      added.emplace_back(sj::Value(path));
    }else if(itTo == to->files.end()){
      removed.emplace_back(sj::Value(path));
    }else if(itFrom->second != itTo->second){
      changed.emplace_back(sj::Value(path));
    }
  }
  sj::Object data;
  data.emplace("added", sj::Value(std::move(added)));
  data.emplace("removed", sj::Value(std::move(removed)));
  data.emplace("changed", sj::Value(std::move(changed)));
  return json_success(sj::Value(std::move(data)));
}

// ===== Risk & Review =====

inline ToolExecutionResult command_risk_assess(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto planId = args.get("--plan");
  if(planId.empty()) return json_error("missing --plan");
  auto record = state().todo.get_record(planId);
  if(!record) return json_error("plan not found", "not_found");
  sj::Array arr;
  for(const auto& step : record->plan.steps){
    sj::Object obj;
    obj.emplace("step_id", sj::Value(step.id));
    std::string level = step.priority >= 3 ? "high" : (step.priority == 2 ? "medium" : "low");
    obj.emplace("risk", sj::Value(level));
    bool needsReview = level != "low" || step.blocked;
    obj.emplace("need_review", sj::Value(needsReview));
    obj.emplace("reason", sj::Value(step.blocked ? "blocked" : "priority-based"));
    arr.emplace_back(sj::Value(std::move(obj)));
  }
  sj::Object data;
  data.emplace("steps", sj::Value(std::move(arr)));
  return json_success(sj::Value(std::move(data)));
}

inline ToolExecutionResult command_request_review(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto planId = args.get("--plan");
  auto intent = args.get("--intent");
  if(planId.empty() || intent.empty()) return json_error("missing --plan or --intent");
  auto record = state().todo.get_record(planId);
  if(!record) return json_error("plan not found", "not_found");
  sj::Object data;
  data.emplace("plan_id", sj::Value(planId));
  data.emplace("intent", sj::Value(intent));
  data.emplace("summary", sj::Value("Requesting review before executing high-risk change"));
  if(args.has("--step")) data.emplace("step_id", sj::Value(args.get("--step")));
  data.emplace("version", sj::Value(record->plan.version));
  data.emplace("diff", sj::Value(args.get("--diff")));
  data.emplace("rollback", sj::Value(args.get("--rollback", "use snapshot")));
  return json_success(sj::Value(std::move(data)));
}

// ===== Budget & Timer =====

inline ToolExecutionResult command_budget_set(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto task = args.get("--task");
  if(task.empty()) return json_error("missing --task");
  auto& info = state().budgets.set_budget(task,
                                          parse_ll(args.get("--tokens"), 0),
                                          parse_ll(args.get("--time"), 0),
                                          parse_ll(args.get("--requests"), 0));
  return json_success(budget_to_json(info));
}

inline ToolExecutionResult command_budget_meter(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto task = args.get("--task");
  if(task.empty()) return json_error("missing --task");
  state().budgets.meter(task,
                        parse_ll(args.get("--tokens"), 0),
                        parse_ll(args.get("--time"), 0),
                        parse_ll(args.get("--requests"), 0));
  auto* info = state().budgets.get(task);
  return json_success(budget_to_json(*info));
}

inline ToolExecutionResult command_timer(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto task = args.get("--task");
  if(task.empty()) return json_error("missing --task");
  auto& timer = state().timers.create(task, args.get("--step"), parse_ll(args.get("--timeout"), 0));
  return json_success(timer_to_json(timer));
}

// ===== Log & Report =====

inline ToolExecutionResult command_log_event(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto planId = args.get("--plan");
  auto type = args.get("--type");
  if(planId.empty() || type.empty()) return json_error("missing --plan or --type");
  auto message = args.get("--message");
  int version = static_cast<int>(parse_ll(args.get("--version"), 0));
  auto& evt = state().log.add(planId, args.get("--step"), type, message, version);
  return json_success(log_event_to_json(evt));
}

inline ToolExecutionResult command_report_summary(const ToolExecutionRequest& request){
  auto args = parse_args(request.tokens, 1);
  auto planId = args.get("--plan");
  if(planId.empty()) return json_error("missing --plan");
  auto record = state().todo.get_record(planId);
  if(!record) return json_error("plan not found", "not_found");
  std::ostringstream oss;
  oss << "Plan " << record->plan.title << " (" << planId << ")\n";
  oss << "Status:\n";
  for(const auto& step : record->plan.steps){
    oss << "- [" << step.status << "] " << step.id << " " << step.title << "\n";
  }
  if(!record->events.empty()){
    oss << "Events:\n";
    for(const auto& evt : record->events){
      oss << "- (" << evt.timestamp << ") " << evt.type << ": " << evt.detail << "\n";
    }
  }
  sj::Object data;
  data.emplace("summary", sj::Value(oss.str()));
  return json_success(sj::Value(std::move(data)));
}

} // namespace agent

