#pragma once

#include "../globals.hpp"
#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace agent {

inline std::string now_iso8601(){
  using namespace std::chrono;
  auto now = system_clock::now();
  auto time = system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buffer);
}

struct ParsedArgs {
  std::unordered_map<std::string, std::vector<std::string>> values;
  std::unordered_set<std::string> flags;
  std::vector<std::string> positionals;

  bool has(const std::string& key) const {
    return values.find(key) != values.end();
  }

  const std::vector<std::string>& getList(const std::string& key) const {
    static const std::vector<std::string> kEmpty;
    auto it = values.find(key);
    if(it == values.end()) return kEmpty;
    return it->second;
  }

  std::string get(const std::string& key, const std::string& fallback = {}) const {
    auto it = values.find(key);
    if(it == values.end() || it->second.empty()) return fallback;
    return it->second.front();
  }
};

inline ParsedArgs parse_args(const std::vector<std::string>& tokens, size_t startIndex = 1){
  ParsedArgs parsed;
  for(size_t i = startIndex; i < tokens.size(); ++i){
    const std::string& tok = tokens[i];
    if(tok.rfind("--", 0) == 0){
      if(i + 1 < tokens.size() && tokens[i + 1].rfind("--", 0) != 0){
        parsed.values[tok].push_back(tokens[i + 1]);
        ++i;
      }else{
        parsed.flags.insert(tok);
      }
    }else{
      parsed.positionals.push_back(tok);
    }
  }
  return parsed;
}

inline ToolExecutionResult json_result(const sj::Value& value, int exitCode = 0){
  ToolExecutionResult result;
  result.exitCode = exitCode;
  result.output = sj::dump(value);
  result.display = result.output;
  return result;
}

inline ToolExecutionResult json_success(sj::Value data){
  sj::Object root;
  root.emplace("ok", sj::Value(true));
  root.emplace("data", std::move(data));
  return json_result(sj::Value(std::move(root)));
}

inline ToolExecutionResult json_error(const std::string& message,
                                      const std::string& code = "bad_request",
                                      int exitCode = 1){
  sj::Object root;
  root.emplace("ok", sj::Value(false));
  root.emplace("error", sj::Value(message));
  root.emplace("code", sj::Value(code));
  return json_result(sj::Value(std::move(root)), exitCode);
}

inline long long parse_ll(const std::string& text, long long fallback = 0){
  if(text.empty()) return fallback;
  try{
    size_t idx = 0;
    long long value = std::stoll(text, &idx, 10);
    if(idx != text.size()) return fallback;
    return value;
  }catch(...){
    return fallback;
  }
}

inline double parse_double(const std::string& text, double fallback = 0.0){
  if(text.empty()) return fallback;
  try{
    size_t idx = 0;
    double value = std::stod(text, &idx);
    if(idx != text.size()) return fallback;
    return value;
  }catch(...){
    return fallback;
  }
}

inline std::vector<std::string> split_csv(const std::string& text){
  std::vector<std::string> out;
  std::string current;
  for(char ch : text){
    if(ch == ','){
      if(!current.empty()){
        out.push_back(current);
        current.clear();
      }
    }else{
      current.push_back(ch);
    }
  }
  if(!current.empty()) out.push_back(current);
  for(auto& item : out){
    size_t begin = 0;
    while(begin < item.size() && std::isspace(static_cast<unsigned char>(item[begin]))){ ++begin; }
    size_t end = item.size();
    while(end > begin && std::isspace(static_cast<unsigned char>(item[end-1]))){ --end; }
    item = item.substr(begin, end - begin);
  }
  return out;
}

inline std::string join_csv(const std::vector<std::string>& items){
  std::string out;
  for(size_t i = 0; i < items.size(); ++i){
    if(i) out.push_back(',');
    out += items[i];
  }
  return out;
}

inline std::string random_id(const std::string& prefix){
  static std::mt19937_64 rng{std::random_device{}()};
  static std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream oss;
  oss << prefix << std::hex << dist(rng);
  return oss.str();
}

struct ChecklistItem {
  std::string id;
  std::string text;
  bool done = false;
  std::string createdAt;
  std::string updatedAt;
};

struct StepNote {
  std::string id;
  std::string text;
  std::string timestamp;
};

struct Step {
  std::string id;
  std::string title;
  std::string description;
  std::string status = "pending";
  int priority = 0;
  std::vector<std::string> dependencies;
  std::vector<std::string> tags;
  std::string owner;
  bool hasEstimate = false;
  double estimateHours = 0.0;
  std::string acceptance;
  std::vector<ChecklistItem> checklist;
  std::vector<std::string> artifacts;
  std::vector<std::string> links;
  std::vector<StepNote> notes;
  bool blocked = false;
  std::string blockReason;
  bool virtualParent = false;
  std::string parentId;
};

struct Plan {
  std::string id;
  std::string title;
  std::string goal;
  int version = 1;
  std::string createdAt;
  std::string updatedAt;
  std::vector<Step> steps;
  std::string mode = "minimal";
  int nextStepSeq = 1;
};

struct PlanSnapshotRecord {
  std::string snapshotId;
  std::string reason;
  std::string createdAt;
  Plan plan;
};

struct PlanEvent {
  std::string eventId;
  std::string type;
  std::string detail;
  std::string timestamp;
  int version = 0;
};

struct SignalRecord {
  std::string timestamp;
  std::string type;
  std::string stepId;
  std::string note;
  std::string artifact;
  std::string reason;
};

struct PlanRecord {
  Plan plan;
  std::vector<Plan> undoStack;
  std::vector<Plan> redoStack;
  std::vector<PlanSnapshotRecord> snapshots;
  std::vector<PlanEvent> events;
  std::vector<SignalRecord> signals;
};

inline sj::Value checklist_to_json(const std::vector<ChecklistItem>& list){
  sj::Array arr;
  for(const auto& item : list){
    sj::Object obj;
    obj.emplace("id", sj::Value(item.id));
    obj.emplace("text", sj::Value(item.text));
    obj.emplace("done", sj::Value(item.done));
    obj.emplace("created_at", sj::Value(item.createdAt));
    obj.emplace("updated_at", sj::Value(item.updatedAt));
    arr.emplace_back(sj::Value(std::move(obj)));
  }
  return sj::Value(std::move(arr));
}

inline sj::Value notes_to_json(const std::vector<StepNote>& notes){
  sj::Array arr;
  for(const auto& note : notes){
    sj::Object obj;
    obj.emplace("id", sj::Value(note.id));
    obj.emplace("text", sj::Value(note.text));
    obj.emplace("timestamp", sj::Value(note.timestamp));
    arr.emplace_back(sj::Value(std::move(obj)));
  }
  return sj::Value(std::move(arr));
}

inline sj::Value step_to_json(const Step& step){
  sj::Object obj;
  obj.emplace("id", sj::Value(step.id));
  obj.emplace("title", sj::Value(step.title));
  obj.emplace("description", sj::Value(step.description));
  obj.emplace("status", sj::Value(step.status));
  obj.emplace("priority", sj::Value(step.priority));
  sj::Array deps;
  for(const auto& dep : step.dependencies) deps.emplace_back(sj::Value(dep));
  obj.emplace("dependencies", sj::Value(std::move(deps)));
  sj::Array tags;
  for(const auto& tag : step.tags) tags.emplace_back(sj::Value(tag));
  obj.emplace("tags", sj::Value(std::move(tags)));
  obj.emplace("owner", sj::Value(step.owner));
  if(step.hasEstimate){
    obj.emplace("estimate_hours", sj::Value(step.estimateHours));
  }
  obj.emplace("acceptance", sj::Value(step.acceptance));
  obj.emplace("checklist", checklist_to_json(step.checklist));
  sj::Array artifacts;
  for(const auto& art : step.artifacts) artifacts.emplace_back(sj::Value(art));
  obj.emplace("artifacts", sj::Value(std::move(artifacts)));
  sj::Array links;
  for(const auto& link : step.links) links.emplace_back(sj::Value(link));
  obj.emplace("links", sj::Value(std::move(links)));
  obj.emplace("notes", notes_to_json(step.notes));
  obj.emplace("blocked", sj::Value(step.blocked));
  obj.emplace("block_reason", sj::Value(step.blockReason));
  obj.emplace("virtual_parent", sj::Value(step.virtualParent));
  obj.emplace("parent_id", sj::Value(step.parentId));
  return sj::Value(std::move(obj));
}

inline sj::Value plan_to_json(const Plan& plan){
  sj::Object obj;
  obj.emplace("id", sj::Value(plan.id));
  obj.emplace("title", sj::Value(plan.title));
  obj.emplace("goal", sj::Value(plan.goal));
  obj.emplace("version", sj::Value(plan.version));
  obj.emplace("created_at", sj::Value(plan.createdAt));
  obj.emplace("updated_at", sj::Value(plan.updatedAt));
  obj.emplace("mode", sj::Value(plan.mode));
  obj.emplace("next_seq", sj::Value(plan.nextStepSeq));
  sj::Array arr;
  for(const auto& step : plan.steps){
    arr.emplace_back(step_to_json(step));
  }
  obj.emplace("steps", sj::Value(std::move(arr)));
  return sj::Value(std::move(obj));
}

class TodoManager {
public:
  Plan& create_plan(const std::string& goal,
                    const std::string& title,
                    const std::string& planId,
                    const std::string& mode){
    Plan plan;
    plan.id = planId.empty() ? random_id("plan-") : planId;
    plan.goal = goal;
    plan.title = title.empty() ? goal : title;
    plan.version = 1;
    plan.createdAt = now_iso8601();
    plan.updatedAt = plan.createdAt;
    if(!mode.empty()) plan.mode = mode;
    PlanRecord record;
    record.plan = plan;
    auto inserted = plans_.emplace(plan.id, std::move(record));
    return inserted.first->second.plan;
  }

  PlanRecord* get_record(const std::string& planId){
    auto it = plans_.find(planId);
    if(it == plans_.end()) return nullptr;
    return &it->second;
  }

  const PlanRecord* get_record(const std::string& planId) const {
    auto it = plans_.find(planId);
    if(it == plans_.end()) return nullptr;
    return &it->second;
  }

  Plan* get_plan(const std::string& planId){
    auto record = get_record(planId);
    if(!record) return nullptr;
    return &record->plan;
  }

  const Plan* get_plan(const std::string& planId) const {
    auto record = get_record(planId);
    if(!record) return nullptr;
    return &record->plan;
  }

  void push_undo(PlanRecord& record){
    record.undoStack.push_back(record.plan);
    record.redoStack.clear();
  }

  void record_event(PlanRecord& record, const std::string& type, const std::string& detail){
    PlanEvent evt;
    evt.eventId = random_id("evt-");
    evt.type = type;
    evt.detail = detail;
    evt.timestamp = now_iso8601();
    evt.version = record.plan.version;
    record.events.push_back(std::move(evt));
  }

  Step* find_step(Plan& plan, const std::string& stepId){
    for(auto& step : plan.steps){
      if(step.id == stepId) return &step;
    }
    return nullptr;
  }

  const Step* find_step(const Plan& plan, const std::string& stepId) const {
    for(const auto& step : plan.steps){
      if(step.id == stepId) return &step;
    }
    return nullptr;
  }

  bool has_dependency_cycle(const Plan& plan) const {
    std::unordered_map<std::string, int> states;
    for(const auto& step : plan.steps){
      states[step.id] = 0;
    }
    std::function<bool(const Step&)> dfs = [&](const Step& node){
      auto& state = states[node.id];
      if(state == 1) return true;
      if(state == 2) return false;
      state = 1;
      for(const auto& depId : node.dependencies){
        auto it = states.find(depId);
        if(it == states.end()) continue;
        const Step* dep = find_step(plan, depId);
        if(dep && dfs(*dep)) return true;
      }
      state = 2;
      return false;
    };
    for(const auto& step : plan.steps){
      if(dfs(step)) return true;
    }
    return false;
  }

  std::vector<std::string> dependency_cycle(const Plan& plan) const {
    std::unordered_map<std::string, int> states;
    std::vector<std::string> path;
    std::vector<std::string> cycle;
    for(const auto& step : plan.steps){
      states[step.id] = 0;
    }
    std::function<bool(const Step&)> dfs = [&](const Step& node){
      states[node.id] = 1;
      path.push_back(node.id);
      for(const auto& depId : node.dependencies){
        auto it = states.find(depId);
        if(it == states.end()) continue;
        const Step* dep = find_step(plan, depId);
        if(!dep) continue;
        if(states[dep->id] == 0){
          if(dfs(*dep)) return true;
        }else if(states[dep->id] == 1){
          auto itCycle = std::find(path.begin(), path.end(), dep->id);
          if(itCycle != path.end()){
            cycle.assign(itCycle, path.end());
            return true;
          }
        }
      }
      path.pop_back();
      states[node.id] = 2;
      return false;
    };
    for(const auto& step : plan.steps){
      if(states[step.id] == 0){
        if(dfs(step)) break;
      }
    }
    return cycle;
  }

  std::unordered_map<std::string, PlanRecord> plans_;
};

struct ScopeInfo {
  std::string taskId;
  std::vector<std::string> allowPaths;
  std::vector<std::string> denyPaths;
  std::vector<std::string> allowTypes;
};

struct ContextEntry {
  std::string id;
  std::string taskId;
  std::string type;
  std::string title;
  std::string summary;
  std::vector<std::string> paths;
  std::vector<std::string> tags;
  std::vector<std::string> keywords;
  std::string createdAt;
  std::string updatedAt;
  bool pinned = false;
  bool tainted = false;
  std::string source;
  std::string payloadRef;
  int usageCount = 0;
};

struct InjectedMicState {
  std::string micText;
  std::string sideText;
  bool pinned = true;
  std::string priority = "high";
};

class ContextManager {
public:
  ScopeInfo& set_scope(const std::string& taskId,
                       std::vector<std::string> allow,
                       std::vector<std::string> types,
                       std::vector<std::string> deny){
    ScopeInfo scope;
    scope.taskId = taskId;
    scope.allowPaths = std::move(allow);
    scope.allowTypes = std::move(types);
    scope.denyPaths = std::move(deny);
    scopes_[taskId] = scope;
    return scopes_[taskId];
  }

  const ScopeInfo* get_scope(const std::string& taskId) const {
    auto it = scopes_.find(taskId);
    if(it == scopes_.end()) return nullptr;
    return &it->second;
  }

  ContextEntry& capture(ContextEntry entry){
    if(entry.id.empty()) entry.id = random_id("ctx-");
    entry.createdAt = entry.createdAt.empty() ? now_iso8601() : entry.createdAt;
    entry.updatedAt = now_iso8601();
    auto inserted = entries_.emplace(entry.id, std::move(entry));
    order_.push_back(inserted.first->first);
    return inserted.first->second;
  }

  ContextEntry* get_entry(const std::string& entryId){
    auto it = entries_.find(entryId);
    if(it == entries_.end()) return nullptr;
    return &it->second;
  }

  std::vector<std::string> pin(const std::vector<std::string>& ids, bool value){
    std::vector<std::string> changed;
    for(const auto& id : ids){
      auto it = entries_.find(id);
      if(it == entries_.end()) continue;
      if(it->second.pinned != value){
        it->second.pinned = value;
        it->second.updatedAt = now_iso8601();
        changed.push_back(id);
      }
    }
    return changed;
  }

  std::pair<std::string, std::vector<std::string>> pack_for_mic(const std::string& taskId,
                                                                int tokenCap,
                                                                const std::vector<std::string>& typePriority){
    struct CandidateRef {
      ContextEntry* entry;
    };
    std::vector<CandidateRef> candidates;
    for(auto& id : order_){
      auto it = entries_.find(id);
      if(it == entries_.end()) continue;
      if(!taskId.empty() && it->second.taskId != taskId) continue;
      if(it->second.tainted) continue;
      candidates.push_back({&it->second});
    }
    auto typeRank = [&](const std::string& type){
      for(size_t i = 0; i < typePriority.size(); ++i){
        if(typePriority[i] == type) return static_cast<int>(i);
      }
      return static_cast<int>(typePriority.size());
    };
    std::stable_sort(candidates.begin(), candidates.end(), [&](const CandidateRef& a, const CandidateRef& b){
      if(a.entry->pinned != b.entry->pinned) return a.entry->pinned > b.entry->pinned;
      int rankA = typeRank(a.entry->type);
      int rankB = typeRank(b.entry->type);
      if(rankA != rankB) return rankA < rankB;
      return a.entry->updatedAt > b.entry->updatedAt;
    });
    std::ostringstream oss;
    std::vector<std::string> used;
    int maxChars = tokenCap <= 0 ? 400 : tokenCap * 4;
    for(const auto& cand : candidates){
      std::string line = "- [" + cand.entry->type + "] " + cand.entry->title + ": " + cand.entry->summary;
      if(!cand.entry->tags.empty()){
        line += " (" + join_csv(cand.entry->tags) + ")";
      }
      line += "\n";
      auto currentPos = oss.tellp();
      long long cursor = currentPos < 0 ? 0 : static_cast<long long>(currentPos);
      if(cursor + static_cast<long long>(line.size()) > maxChars) break;
      oss << line;
      used.push_back(cand.entry->id);
      cand.entry->usageCount += 1;
      cand.entry->updatedAt = now_iso8601();
    }
    return {oss.str(), used};
  }

  std::unordered_map<std::string, ScopeInfo> scopes_;
  std::unordered_map<std::string, ContextEntry> entries_;
  std::vector<std::string> order_;
  InjectedMicState mic;
};

struct GuardDecision {
  bool allowed = true;
  bool requireSnapshot = false;
  std::string reason;
};

struct GuardManager {
  GuardDecision fs_guard(const std::string& op,
                         const std::string& path,
                         long long size) const {
    GuardDecision decision;
    if(path.empty()){
      decision.allowed = false;
      decision.reason = "empty path";
      return decision;
    }
    std::error_code ec;
    auto resolved = std::filesystem::weakly_canonical(path, ec);
    if(ec){
      decision.allowed = false;
      decision.reason = "invalid path";
      return decision;
    }
    auto cwd = std::filesystem::current_path();
    auto rel = std::filesystem::relative(resolved, cwd, ec);
    if(ec || rel.empty() || rel.string().rfind("..", 0) == 0){
      decision.allowed = false;
      decision.reason = "outside workspace";
      return decision;
    }
    if(op == "write"){ 
      decision.requireSnapshot = true;
      if(size > 2 * 1024 * 1024){
        decision.allowed = false;
        decision.reason = "write too large";
      }
    }
    decision.reason = decision.allowed ? "ok" : decision.reason;
    return decision;
  }

  GuardDecision shell_guard(const std::string& command) const {
    GuardDecision decision;
    if(command.find("rm -rf /") != std::string::npos){
      decision.allowed = false;
      decision.reason = "dangerous command";
      return decision;
    }
    if(command.find("; rm") != std::string::npos){
      decision.requireSnapshot = true;
    }
    decision.reason = "ok";
    return decision;
  }

  GuardDecision net_guard(const std::string& host) const {
    GuardDecision decision;
    if(host == "" || host == "localhost" || host == "127.0.0.1"){
      decision.reason = "ok";
      return decision;
    }
    decision.allowed = false;
    decision.reason = "network disabled";
    return decision;
  }
};

struct BudgetInfo {
  std::string taskId;
  long long tokenLimit = 0;
  long long timeLimit = 0;
  long long requestLimit = 0;
  long long tokensUsed = 0;
  long long timeUsed = 0;
  long long requestsUsed = 0;
};

class BudgetManager {
public:
  BudgetInfo& set_budget(const std::string& taskId,
                         long long tokenLimit,
                         long long timeLimit,
                         long long requestLimit){
    BudgetInfo info;
    info.taskId = taskId;
    info.tokenLimit = tokenLimit;
    info.timeLimit = timeLimit;
    info.requestLimit = requestLimit;
    auto inserted = budgets_.emplace(taskId, info);
    if(!inserted.second){
      inserted.first->second.tokenLimit = tokenLimit;
      inserted.first->second.timeLimit = timeLimit;
      inserted.first->second.requestLimit = requestLimit;
    }
    return budgets_[taskId];
  }

  BudgetInfo* get(const std::string& taskId){
    auto it = budgets_.find(taskId);
    if(it == budgets_.end()) return nullptr;
    return &it->second;
  }

  void meter(const std::string& taskId,
             long long tokens,
             long long timeSpent,
             long long requests){
    auto it = budgets_.find(taskId);
    if(it == budgets_.end()){
      BudgetInfo info;
      info.taskId = taskId;
      budgets_[taskId] = info;
      it = budgets_.find(taskId);
    }
    it->second.tokensUsed += tokens;
    it->second.timeUsed += timeSpent;
    it->second.requestsUsed += requests;
  }

  std::unordered_map<std::string, BudgetInfo> budgets_;
};

struct TimerInfo {
  std::string timerId;
  std::string taskId;
  std::string stepId;
  long long timeoutSeconds = 0;
  std::string createdAt;
};

class TimerManager {
public:
  TimerInfo& create(const std::string& taskId,
                    const std::string& stepId,
                    long long timeoutSeconds){
    TimerInfo info;
    info.timerId = random_id("timer-");
    info.taskId = taskId;
    info.stepId = stepId;
    info.timeoutSeconds = timeoutSeconds;
    info.createdAt = now_iso8601();
    timers_[info.timerId] = info;
    return timers_[info.timerId];
  }

  std::unordered_map<std::string, TimerInfo> timers_;
};

struct LogEvent {
  std::string eventId;
  std::string planId;
  std::string stepId;
  std::string type;
  std::string message;
  std::string timestamp;
  int version = 0;
};

class EventLog {
public:
  LogEvent& add(const std::string& planId,
                const std::string& stepId,
                const std::string& type,
                const std::string& message,
                int version){
    LogEvent evt;
    evt.eventId = random_id("log-");
    evt.planId = planId;
    evt.stepId = stepId;
    evt.type = type;
    evt.message = message;
    evt.timestamp = now_iso8601();
    evt.version = version;
    events_.push_back(evt);
    return events_.back();
  }

  std::vector<LogEvent> events_;
};

struct FileSnapshot {
  std::string id;
  std::string createdAt;
  std::string reason;
  std::map<std::string, std::string> files;
};

class SnapshotStore {
public:
  FileSnapshot& create(const std::vector<std::string>& paths, const std::string& reason){
    FileSnapshot snap;
    snap.id = random_id("snap-");
    snap.createdAt = now_iso8601();
    snap.reason = reason;
    for(const auto& path : paths){
      std::error_code ec;
      auto resolved = std::filesystem::weakly_canonical(path, ec);
      if(ec) continue;
      if(std::filesystem::is_directory(resolved)){
        for(auto& entry : std::filesystem::recursive_directory_iterator(resolved, ec)){
          if(ec) break;
          if(entry.is_regular_file()){ 
            std::ifstream ifs(entry.path(), std::ios::binary);
            if(!ifs) continue;
            std::ostringstream oss;
            oss << ifs.rdbuf();
            snap.files[entry.path().string()] = oss.str();
          }
        }
      }else if(std::filesystem::is_regular_file(resolved)){
        std::ifstream ifs(resolved, std::ios::binary);
        if(!ifs) continue;
        std::ostringstream oss;
        oss << ifs.rdbuf();
        snap.files[resolved.string()] = oss.str();
      }
    }
    snapshots_[snap.id] = snap;
    return snapshots_[snap.id];
  }

  FileSnapshot* get(const std::string& id){
    auto it = snapshots_.find(id);
    if(it == snapshots_.end()) return nullptr;
    return &it->second;
  }

  std::unordered_map<std::string, FileSnapshot> snapshots_;
};

struct AgentState {
  TodoManager todo;
  ContextManager context;
  GuardManager guard;
  BudgetManager budgets;
  TimerManager timers;
  EventLog log;
  SnapshotStore fsSnapshots;
};

inline AgentState& state(){
  static AgentState g_state;
  return g_state;
}

// ===== Command helpers =====

inline bool check_version(Plan& plan, int expected){
  return expected == plan.version;
}

inline void bump_version(Plan& plan){
  ++plan.version;
  plan.updatedAt = now_iso8601();
}

inline std::string ensure_step_id(Plan& plan){
  std::ostringstream oss;
  oss << "step-" << plan.nextStepSeq++;
  return oss.str();
}

inline void remove_step_from_dependencies(Plan& plan, const std::string& removedId){
  for(auto& step : plan.steps){
    auto& deps = step.dependencies;
    deps.erase(std::remove(deps.begin(), deps.end(), removedId), deps.end());
  }
}

inline std::vector<std::string> collect_blockers(const Plan& plan){
  std::vector<std::string> blockers;
  for(const auto& step : plan.steps){
    if(step.blocked){
      blockers.push_back(step.id);
    }
  }
  return blockers;
}

inline std::vector<std::string> find_done(const Plan& plan){
  std::vector<std::string> out;
  for(const auto& step : plan.steps){
    if(step.status == "done") out.push_back(step.id);
  }
  return out;
}

inline Step* find_first_running(Plan& plan){
  for(auto& step : plan.steps){
    if(step.status == "running") return &step;
  }
  return nullptr;
}

inline Step* find_first_pending(Plan& plan){
  for(auto& step : plan.steps){
    if(step.status == "pending") return &step;
  }
  return nullptr;
}

inline bool dependencies_done(const Plan& plan, const Step& step){
  for(const auto& depId : step.dependencies){
    const Step* dep = state().todo.find_step(plan, depId);
    if(dep && dep->status != "done") return false;
  }
  return true;
}

inline sj::Value events_to_json(const std::vector<PlanEvent>& events){
  sj::Array arr;
  for(const auto& evt : events){
    sj::Object obj;
    obj.emplace("id", sj::Value(evt.eventId));
    obj.emplace("type", sj::Value(evt.type));
    obj.emplace("detail", sj::Value(evt.detail));
    obj.emplace("timestamp", sj::Value(evt.timestamp));
    obj.emplace("version", sj::Value(evt.version));
    arr.emplace_back(sj::Value(std::move(obj)));
  }
  return sj::Value(std::move(arr));
}

inline sj::Value signals_to_json(const std::vector<SignalRecord>& signals){
  sj::Array arr;
  for(const auto& sig : signals){
    sj::Object obj;
    obj.emplace("timestamp", sj::Value(sig.timestamp));
    obj.emplace("type", sj::Value(sig.type));
    obj.emplace("step_id", sj::Value(sig.stepId));
    obj.emplace("note", sj::Value(sig.note));
    obj.emplace("artifact", sj::Value(sig.artifact));
    obj.emplace("reason", sj::Value(sig.reason));
    arr.emplace_back(sj::Value(std::move(obj)));
  }
  return sj::Value(std::move(arr));
}

inline sj::Value plan_snapshot_to_json(const PlanSnapshotRecord& snap){
  sj::Object obj;
  obj.emplace("id", sj::Value(snap.snapshotId));
  obj.emplace("created_at", sj::Value(snap.createdAt));
  obj.emplace("reason", sj::Value(snap.reason));
  obj.emplace("plan", plan_to_json(snap.plan));
  return sj::Value(std::move(obj));
}

inline sj::Value budget_to_json(const BudgetInfo& info){
  sj::Object obj;
  obj.emplace("task_id", sj::Value(info.taskId));
  obj.emplace("token_limit", sj::Value(info.tokenLimit));
  obj.emplace("time_limit", sj::Value(info.timeLimit));
  obj.emplace("request_limit", sj::Value(info.requestLimit));
  obj.emplace("tokens_used", sj::Value(info.tokensUsed));
  obj.emplace("time_used", sj::Value(info.timeUsed));
  obj.emplace("requests_used", sj::Value(info.requestsUsed));
  return sj::Value(std::move(obj));
}

inline sj::Value timer_to_json(const TimerInfo& info){
  sj::Object obj;
  obj.emplace("id", sj::Value(info.timerId));
  obj.emplace("task_id", sj::Value(info.taskId));
  obj.emplace("step_id", sj::Value(info.stepId));
  obj.emplace("timeout_seconds", sj::Value(info.timeoutSeconds));
  obj.emplace("created_at", sj::Value(info.createdAt));
  return sj::Value(std::move(obj));
}

inline sj::Value log_event_to_json(const LogEvent& evt){
  sj::Object obj;
  obj.emplace("id", sj::Value(evt.eventId));
  obj.emplace("plan_id", sj::Value(evt.planId));
  obj.emplace("step_id", sj::Value(evt.stepId));
  obj.emplace("type", sj::Value(evt.type));
  obj.emplace("message", sj::Value(evt.message));
  obj.emplace("timestamp", sj::Value(evt.timestamp));
  obj.emplace("version", sj::Value(evt.version));
  return sj::Value(std::move(obj));
}

} // namespace agent

