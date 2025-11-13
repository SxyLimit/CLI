#pragma once

#include "tool_common.hpp"
#include "../utils/agent_commands.hpp"

namespace tool {

inline ToolSpec build_todo_spec(const std::string& name,
                                const std::string& summaryEn,
                                const std::string& summaryZh,
                                const std::string& helpEn,
                                const std::string& helpZh,
                                std::vector<OptionSpec> options = {}){
  ToolSpec spec;
  spec.name = name;
  spec.summary = summaryEn;
  set_tool_summary_locale(spec, "en", summaryEn);
  set_tool_summary_locale(spec, "zh", summaryZh);
  spec.help = helpEn;
  set_tool_help_locale(spec, "en", helpEn);
  set_tool_help_locale(spec, "zh", helpZh);
  spec.options = std::move(options);
  spec.hidden = true;
  spec.requiresExplicitExpose = true;
  return spec;
}

inline SubcommandSpec todo_subcommand_from(const ToolSpec& spec){
  SubcommandSpec sub;
  auto pos = spec.name.find_last_of('.');
  sub.name = (pos == std::string::npos) ? spec.name : spec.name.substr(pos + 1);
  sub.options = spec.options;
  sub.positional = spec.positional;
  sub.mutexGroups = {};
  sub.handler = nullptr;
  return sub;
}

struct FsTodoPlan {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.plan",
      "Create or regenerate a plan",
      "创建或重新生成计划",
      "fs.todo.plan --goal <text> [--title <text>] [--plan-id <id>] [--mode minimal|full]",
      "fs.todo.plan --goal <目标> [--title <标题>] [--plan-id <标识>] [--mode minimal|full]",
      {
        OptionSpec{"--goal", true, {}, nullptr, true, "<goal>"},
        OptionSpec{"--title", true, {}, nullptr, false, "<title>"},
        OptionSpec{"--plan-id", true, {}, nullptr, false, "<id>"},
        OptionSpec{"--mode", true, {"minimal", "full"}, nullptr, false, "<mode>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_plan(request);
  }
};

struct FsTodoView {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.view",
      "View the full plan",
      "查看计划详情",
      "fs.todo.view --plan <id> [--include-history]",
      "fs.todo.view --plan <标识> [--include-history]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<id>"},
        OptionSpec{"--include-history", false}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_view(request);
  }
};

struct FsTodoUpdate {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.update",
      "Update metadata for a single step",
      "更新单个步骤的元信息",
      "fs.todo.update --plan <id> --expected-version <n> --step <id> [--title <text>] [--description <text>] [--priority <n>] [--owner <name>] [--acceptance <text>] [--estimate <hours>] [--add-tag <tag>...] [--remove-tag <tag>...]",
      "fs.todo.update --plan <标识> --expected-version <版本> --step <步骤> [--title <标题>] [--description <描述>] [--priority <优先级>] [--owner <负责人>] [--acceptance <验收标准>] [--estimate <工时>] [--add-tag <标签>...] [--remove-tag <标签>...]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--expected-version", true, {}, nullptr, true, "<version>"},
        OptionSpec{"--step", true, {}, nullptr, true, "<step>"},
        OptionSpec{"--title", true, {}, nullptr, false, "<title>"},
        OptionSpec{"--description", true, {}, nullptr, false, "<description>"},
        OptionSpec{"--priority", true, {}, nullptr, false, "<priority>"},
        OptionSpec{"--owner", true, {}, nullptr, false, "<owner>"},
        OptionSpec{"--acceptance", true, {}, nullptr, false, "<text>"},
        OptionSpec{"--estimate", true, {}, nullptr, false, "<hours>"},
        OptionSpec{"--add-tag", true, {}, nullptr, false, "<tag>"},
        OptionSpec{"--remove-tag", true, {}, nullptr, false, "<tag>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_update(request);
  }
};

struct FsTodoAdd {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.add",
      "Insert a new plan step",
      "新增计划步骤",
      "fs.todo.add --plan <id> --expected-version <n> --title <text> [--description <text>] [--priority <n>] [--status pending|running|done|blocked] [--owner <name>] [--acceptance <text>] [--estimate <hours>] [--depends a,b] [--tags x,y] [--after <step>]",
      "fs.todo.add --plan <标识> --expected-version <版本> --title <标题> [--description <描述>] [--priority <优先级>] [--status pending|running|done|blocked] [--owner <负责人>] [--acceptance <验收标准>] [--estimate <工时>] [--depends a,b] [--tags x,y] [--after <步骤>]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--expected-version", true, {}, nullptr, true, "<version>"},
        OptionSpec{"--title", true, {}, nullptr, true, "<title>"},
        OptionSpec{"--description", true, {}, nullptr, false, "<description>"},
        OptionSpec{"--priority", true, {}, nullptr, false, "<priority>"},
        OptionSpec{"--status", true, {"pending", "running", "done", "blocked"}, nullptr, false, "<status>"},
        OptionSpec{"--owner", true, {}, nullptr, false, "<owner>"},
        OptionSpec{"--acceptance", true, {}, nullptr, false, "<text>"},
        OptionSpec{"--estimate", true, {}, nullptr, false, "<hours>"},
        OptionSpec{"--depends", true, {}, nullptr, false, "a,b"},
        OptionSpec{"--tags", true, {}, nullptr, false, "x,y"},
        OptionSpec{"--after", true, {}, nullptr, false, "<step>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_add(request);
  }
};

struct FsTodoRemove {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.remove",
      "Remove steps from the plan",
      "删除计划步骤",
      "fs.todo.remove --plan <id> --expected-version <n> --step <id> [--step <id>...]",
      "fs.todo.remove --plan <标识> --expected-version <版本> --step <步骤> [--step <步骤>...]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--expected-version", true, {}, nullptr, true, "<version>"},
        OptionSpec{"--step", true, {}, nullptr, true, "<step>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_remove(request);
  }
};

struct FsTodoReorder {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.reorder",
      "Reorder plan steps",
      "重新排列步骤顺序",
      "fs.todo.reorder --plan <id> --expected-version <n> --order <id1,id2,...>",
      "fs.todo.reorder --plan <标识> --expected-version <版本> --order <id1,id2,...>",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--expected-version", true, {}, nullptr, true, "<version>"},
        OptionSpec{"--order", true, {}, nullptr, true, "<sequence>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_reorder(request);
  }
};

struct FsTodoDepSet {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.dep.set",
      "Replace the dependency list for a step",
      "替换步骤依赖列表",
      "fs.todo.dep.set --plan <id> --expected-version <n> --step <id> --deps <a,b,...>",
      "fs.todo.dep.set --plan <标识> --expected-version <版本> --step <步骤> --deps <a,b,...>",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--expected-version", true, {}, nullptr, true, "<version>"},
        OptionSpec{"--step", true, {}, nullptr, true, "<step>"},
        OptionSpec{"--deps", true, {}, nullptr, false, "a,b"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_dep_set(request);
  }
};

struct FsTodoDepAdd {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.dep.add",
      "Add dependencies to a step",
      "为步骤新增依赖",
      "fs.todo.dep.add --plan <id> --expected-version <n> --step <id> --deps <a,b,...>",
      "fs.todo.dep.add --plan <标识> --expected-version <版本> --step <步骤> --deps <a,b,...>",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--expected-version", true, {}, nullptr, true, "<version>"},
        OptionSpec{"--step", true, {}, nullptr, true, "<step>"},
        OptionSpec{"--deps", true, {}, nullptr, false, "a,b"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_dep_add(request);
  }
};

struct FsTodoDepRemove {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.dep.remove",
      "Remove dependencies from a step",
      "移除步骤依赖",
      "fs.todo.dep.remove --plan <id> --expected-version <n> --step <id> --deps <a,b,...>",
      "fs.todo.dep.remove --plan <标识> --expected-version <版本> --step <步骤> --deps <a,b,...>",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--expected-version", true, {}, nullptr, true, "<version>"},
        OptionSpec{"--step", true, {}, nullptr, true, "<step>"},
        OptionSpec{"--deps", true, {}, nullptr, false, "a,b"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_dep_remove(request);
  }
};

struct FsTodoSplit {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.split",
      "Split a complex step into multiple child steps",
      "将复杂步骤拆分为多个子步骤",
      "fs.todo.split --plan <id> --expected-version <n> --step <id> --child <title::description> [--child ...] [--keep-parent]",
      "fs.todo.split --plan <标识> --expected-version <版本> --step <步骤> --child <标题::描述> [--child ...] [--keep-parent]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--expected-version", true, {}, nullptr, true, "<version>"},
        OptionSpec{"--step", true, {}, nullptr, true, "<step>"},
        OptionSpec{"--child", true, {}, nullptr, true, "<title::description>"},
        OptionSpec{"--keep-parent", false}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_split(request);
  }
};

struct FsTodoMerge {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.merge",
      "Merge several steps into one",
      "合并多个步骤",
      "fs.todo.merge --plan <id> --expected-version <n> --steps <a,b,...> [--title <text>] [--description <text>] [--priority <n>] [--owner <name>] [--acceptance <text>]",
      "fs.todo.merge --plan <标识> --expected-version <版本> --steps <a,b,...> [--title <标题>] [--description <描述>] [--priority <优先级>] [--owner <负责人>] [--acceptance <验收标准>]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--expected-version", true, {}, nullptr, true, "<version>"},
        OptionSpec{"--steps", true, {}, nullptr, true, "a,b"},
        OptionSpec{"--title", true, {}, nullptr, false, "<title>"},
        OptionSpec{"--description", true, {}, nullptr, false, "<description>"},
        OptionSpec{"--priority", true, {}, nullptr, false, "<priority>"},
        OptionSpec{"--owner", true, {}, nullptr, false, "<owner>"},
        OptionSpec{"--acceptance", true, {}, nullptr, false, "<text>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_merge(request);
  }
};

struct FsTodoMark {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.mark",
      "Change step status",
      "标记步骤状态",
      "fs.todo.mark --plan <id> --expected-version <n> --step <id> --status pending|running|done|blocked [--reason <text>] [--artifact <path>]",
      "fs.todo.mark --plan <标识> --expected-version <版本> --step <步骤> --status pending|running|done|blocked [--reason <原因>] [--artifact <产物>]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--expected-version", true, {}, nullptr, true, "<version>"},
        OptionSpec{"--step", true, {}, nullptr, true, "<step>"},
        OptionSpec{"--status", true, {"pending", "running", "done", "blocked"}, nullptr, true, "<status>"},
        OptionSpec{"--reason", true, {}, nullptr, false, "<reason>"},
        OptionSpec{"--artifact", true, {}, nullptr, false, "<artifact>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_mark(request);
  }
};

struct FsTodoChecklist {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.checklist",
      "Manage checklist items for a step",
      "管理步骤的检查清单",
      "fs.todo.checklist --plan <id> --expected-version <n> --step <id> --op add|remove|toggle|rename [--item <id>] [--text <text>]",
      "fs.todo.checklist --plan <标识> --expected-version <版本> --step <步骤> --op add|remove|toggle|rename [--item <ID>] [--text <文本>]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--expected-version", true, {}, nullptr, true, "<version>"},
        OptionSpec{"--step", true, {}, nullptr, true, "<step>"},
        OptionSpec{"--op", true, {"add", "remove", "toggle", "rename"}, nullptr, true, "<op>"},
        OptionSpec{"--item", true, {}, nullptr, false, "<item>"},
        OptionSpec{"--text", true, {}, nullptr, false, "<text>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_checklist(request);
  }
};

struct FsTodoAnnotate {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.annotate",
      "Add annotations to a step",
      "为步骤添加注释",
      "fs.todo.annotate --plan <id> --expected-version <n> --step <id> [--note <text>] [--artifacts-add <path>] [--artifacts-remove <path>] [--links-add <step>]",
      "fs.todo.annotate --plan <标识> --expected-version <版本> --step <步骤> [--note <注释>] [--artifacts-add <产物>] [--artifacts-remove <产物>] [--links-add <关联步骤>]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--expected-version", true, {}, nullptr, true, "<version>"},
        OptionSpec{"--step", true, {}, nullptr, true, "<step>"},
        OptionSpec{"--note", true, {}, nullptr, false, "<note>"},
        OptionSpec{"--artifacts-add", true, {}, nullptr, false, "<artifact>"},
        OptionSpec{"--artifacts-remove", true, {}, nullptr, false, "<artifact>"},
        OptionSpec{"--links-add", true, {}, nullptr, false, "<step>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_annotate(request);
  }
};

struct FsTodoBlock {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.block",
      "Mark a step as explicitly blocked",
      "显式阻塞步骤",
      "fs.todo.block --plan <id> --expected-version <n> --step <id> --reason <text>",
      "fs.todo.block --plan <标识> --expected-version <版本> --step <步骤> --reason <原因>",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--expected-version", true, {}, nullptr, true, "<version>"},
        OptionSpec{"--step", true, {}, nullptr, true, "<step>"},
        OptionSpec{"--reason", true, {}, nullptr, true, "<reason>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_block(request, true);
  }
};

struct FsTodoUnblock {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.unblock",
      "Clear the blocked state of a step",
      "取消步骤阻塞",
      "fs.todo.unblock --plan <id> --expected-version <n> --step <id>",
      "fs.todo.unblock --plan <标识> --expected-version <版本> --step <步骤>",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--expected-version", true, {}, nullptr, true, "<version>"},
        OptionSpec{"--step", true, {}, nullptr, true, "<step>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_block(request, false);
  }
};

struct FsTodoSnapshot {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.snapshot",
      "Create a plan snapshot",
      "创建计划快照",
      "fs.todo.snapshot --plan <id> [--reason <text>]",
      "fs.todo.snapshot --plan <标识> [--reason <原因>]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--reason", true, {}, nullptr, false, "<reason>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_snapshot(request);
  }
};

struct FsTodoHistory {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.history",
      "List plan events",
      "查看计划历史事件",
      "fs.todo.history --plan <id> [--limit <n>]",
      "fs.todo.history --plan <标识> [--limit <数量>]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--limit", true, {}, nullptr, false, "<count>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_history(request);
  }
};

struct FsTodoUndo {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.undo",
      "Undo plan operations",
      "撤销计划变更",
      "fs.todo.undo --plan <id> [--steps <n>]",
      "fs.todo.undo --plan <标识> [--steps <数量>]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--steps", true, {}, nullptr, false, "<count>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_undo(request, false);
  }
};

struct FsTodoRedo {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.redo",
      "Redo plan operations",
      "重做计划变更",
      "fs.todo.redo --plan <id> [--steps <n>]",
      "fs.todo.redo --plan <标识> [--steps <数量>]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--steps", true, {}, nullptr, false, "<count>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_undo(request, true);
  }
};

struct FsTodoBrief {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.brief",
      "Generate a MIC summary",
      "生成 MIC 摘要",
      "fs.todo.brief --plan <id> [--k-done <n>] [--k-next <n>] [--token-cap <n>]",
      "fs.todo.brief --plan <标识> [--k-done <数量>] [--k-next <数量>] [--token-cap <上限>]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--k-done", true, {}, nullptr, false, "<count>"},
        OptionSpec{"--k-next", true, {}, nullptr, false, "<count>"},
        OptionSpec{"--token-cap", true, {}, nullptr, false, "<tokens>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_brief(request);
  }
};

struct FsTodoSignal {
  static ToolSpec ui(){
    return build_todo_spec(
      "fs.todo.signal",
      "Record the next orchestration signal",
      "记录编排信号",
      "fs.todo.signal --plan <id> --type <START|COMPLETE|BLOCKED|SWITCH|REPLAN_REQUEST> [--step <id>] [--note <text>] [--artifact <path>] [--reason <text>]",
      "fs.todo.signal --plan <标识> --type <START|COMPLETE|BLOCKED|SWITCH|REPLAN_REQUEST> [--step <步骤>] [--note <说明>] [--artifact <产物>] [--reason <原因>]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--type", true, {"START", "COMPLETE", "BLOCKED", "SWITCH", "REPLAN_REQUEST"}, nullptr, true, "<type>"},
        OptionSpec{"--step", true, {}, nullptr, false, "<step>"},
        OptionSpec{"--note", true, {}, nullptr, false, "<note>"},
        OptionSpec{"--artifact", true, {}, nullptr, false, "<artifact>"},
        OptionSpec{"--reason", true, {}, nullptr, false, "<reason>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_todo_signal(request);
  }
};

struct FsTodo {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "fs.todo";
    spec.summary = "Manage orchestrator plans";
    set_tool_summary_locale(spec, "en", "Manage orchestrator plans");
    set_tool_summary_locale(spec, "zh", "管理编排计划");
    spec.help = "fs.todo <plan|view|update|add|remove|reorder|dep.set|dep.add|dep.remove|split|merge|mark|checklist|annotate|block|unblock|snapshot|history|undo|redo|brief|signal> ...";
    set_tool_help_locale(spec, "en", spec.help);
    set_tool_help_locale(spec, "zh", "fs.todo <plan|view|update|add|remove|reorder|dep.set|dep.add|dep.remove|split|merge|mark|checklist|annotate|block|unblock|snapshot|history|undo|redo|brief|signal> ...");
    spec.hidden = true;
    spec.requiresExplicitExpose = true;
    spec.subs = {
      todo_subcommand_from(FsTodoPlan::ui()),
      todo_subcommand_from(FsTodoView::ui()),
      todo_subcommand_from(FsTodoUpdate::ui()),
      todo_subcommand_from(FsTodoAdd::ui()),
      todo_subcommand_from(FsTodoRemove::ui()),
      todo_subcommand_from(FsTodoReorder::ui()),
      todo_subcommand_from(FsTodoDepSet::ui()),
      todo_subcommand_from(FsTodoDepAdd::ui()),
      todo_subcommand_from(FsTodoDepRemove::ui()),
      todo_subcommand_from(FsTodoSplit::ui()),
      todo_subcommand_from(FsTodoMerge::ui()),
      todo_subcommand_from(FsTodoMark::ui()),
      todo_subcommand_from(FsTodoChecklist::ui()),
      todo_subcommand_from(FsTodoAnnotate::ui()),
      todo_subcommand_from(FsTodoBlock::ui()),
      todo_subcommand_from(FsTodoUnblock::ui()),
      todo_subcommand_from(FsTodoSnapshot::ui()),
      todo_subcommand_from(FsTodoHistory::ui()),
      todo_subcommand_from(FsTodoUndo::ui()),
      todo_subcommand_from(FsTodoRedo::ui()),
      todo_subcommand_from(FsTodoBrief::ui()),
      todo_subcommand_from(FsTodoSignal::ui())
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    if(request.tokens.size() < 2){
      g_parse_error_cmd = "fs.todo";
      return detail::text_result("usage: fs.todo <subcommand> ...\n", 1);
    }
    const std::string& sub = request.tokens[1];
    ToolExecutionRequest forwarded = request;
    forwarded.tokens.clear();
    forwarded.tokens.push_back("fs.todo." + sub);
    for(size_t i = 2; i < request.tokens.size(); ++i){
      forwarded.tokens.push_back(request.tokens[i]);
    }
    if(sub == "plan") return FsTodoPlan::run(forwarded);
    if(sub == "view") return FsTodoView::run(forwarded);
    if(sub == "update") return FsTodoUpdate::run(forwarded);
    if(sub == "add") return FsTodoAdd::run(forwarded);
    if(sub == "remove") return FsTodoRemove::run(forwarded);
    if(sub == "reorder") return FsTodoReorder::run(forwarded);
    if(sub == "dep.set") return FsTodoDepSet::run(forwarded);
    if(sub == "dep.add") return FsTodoDepAdd::run(forwarded);
    if(sub == "dep.remove") return FsTodoDepRemove::run(forwarded);
    if(sub == "split") return FsTodoSplit::run(forwarded);
    if(sub == "merge") return FsTodoMerge::run(forwarded);
    if(sub == "mark") return FsTodoMark::run(forwarded);
    if(sub == "checklist") return FsTodoChecklist::run(forwarded);
    if(sub == "annotate") return FsTodoAnnotate::run(forwarded);
    if(sub == "block") return FsTodoBlock::run(forwarded);
    if(sub == "unblock") return FsTodoUnblock::run(forwarded);
    if(sub == "snapshot") return FsTodoSnapshot::run(forwarded);
    if(sub == "history") return FsTodoHistory::run(forwarded);
    if(sub == "undo") return FsTodoUndo::run(forwarded);
    if(sub == "redo") return FsTodoRedo::run(forwarded);
    if(sub == "brief") return FsTodoBrief::run(forwarded);
    if(sub == "signal") return FsTodoSignal::run(forwarded);
    g_parse_error_cmd = "fs.todo";
    return detail::text_result("unknown fs.todo subcommand: " + sub + "\n", 1);
  }
};

inline ToolDefinition make_fs_todo_tool(){
  ToolDefinition def;
  def.ui = FsTodo::ui();
  def.executor = FsTodo::run;
  return def;
}

} // namespace tool

