#pragma once

#include "tool_common.hpp"
#include "../utils/agent_commands.hpp"

namespace tool {

inline ToolSpec build_admin_spec(const std::string& name,
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

inline SubcommandSpec admin_subcommand_from(const ToolSpec& spec){
  SubcommandSpec sub;
  auto pos = spec.name.find_last_of('.');
  sub.name = (pos == std::string::npos) ? spec.name : spec.name.substr(pos + 1);
  sub.options = spec.options;
  sub.positional = spec.positional;
  sub.mutexGroups = {};
  sub.handler = nullptr;
  return sub;
}

struct FsBudgetSet {
  static ToolSpec ui(){
    return build_admin_spec(
      "fs.budget.set",
      "Set task budgets",
      "设定任务预算",
      "fs.budget.set --task <id> [--tokens <n>] [--time <ms>] [--requests <n>]",
      "fs.budget.set --task <标识> [--tokens <数量>] [--time <毫秒>] [--requests <次数>]",
      {
        OptionSpec{"--task", true, {}, nullptr, true, "<task>"},
        OptionSpec{"--tokens", true, {}, nullptr, false, "<tokens>"},
        OptionSpec{"--time", true, {}, nullptr, false, "<time>"},
        OptionSpec{"--requests", true, {}, nullptr, false, "<requests>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_budget_set(request);
  }
};

struct FsBudgetMeter {
  static ToolSpec ui(){
    return build_admin_spec(
      "fs.budget.meter",
      "Meter budget usage",
      "记录预算消耗",
      "fs.budget.meter --task <id> [--tokens <n>] [--time <ms>] [--requests <n>]",
      "fs.budget.meter --task <标识> [--tokens <数量>] [--time <毫秒>] [--requests <次数>]",
      {
        OptionSpec{"--task", true, {}, nullptr, true, "<task>"},
        OptionSpec{"--tokens", true, {}, nullptr, false, "<tokens>"},
        OptionSpec{"--time", true, {}, nullptr, false, "<time>"},
        OptionSpec{"--requests", true, {}, nullptr, false, "<requests>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_budget_meter(request);
  }
};

struct FsTimer {
  static ToolSpec ui(){
    return build_admin_spec(
      "fs.timer",
      "Start a timer",
      "设置计时器",
      "fs.timer --task <id> [--step <id>] --timeout <seconds>",
      "fs.timer --task <标识> [--step <步骤>] --timeout <秒数>",
      {
        OptionSpec{"--task", true, {}, nullptr, true, "<task>"},
        OptionSpec{"--step", true, {}, nullptr, false, "<step>"},
        OptionSpec{"--timeout", true, {}, nullptr, true, "<seconds>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_timer(request);
  }
};

struct FsLogEvent {
  static ToolSpec ui(){
    return build_admin_spec(
      "fs.log.event",
      "Record a log event",
      "记录日志事件",
      "fs.log.event --plan <id> --type <text> [--step <id>] [--message <text>] [--version <n>]",
      "fs.log.event --plan <标识> --type <类型> [--step <步骤>] [--message <信息>] [--version <版本>]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--type", true, {}, nullptr, true, "<type>"},
        OptionSpec{"--step", true, {}, nullptr, false, "<step>"},
        OptionSpec{"--message", true, {}, nullptr, false, "<message>"},
        OptionSpec{"--version", true, {}, nullptr, false, "<version>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_log_event(request);
  }
};

struct FsReportSummary {
  static ToolSpec ui(){
    return build_admin_spec(
      "fs.report.summary",
      "Generate a task summary",
      "生成任务总结",
      "fs.report.summary --plan <id>",
      "fs.report.summary --plan <标识>",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_report_summary(request);
  }
};

struct FsBudget {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "fs.budget";
    spec.summary = "Manage task budgets";
    set_tool_summary_locale(spec, "en", "Manage task budgets");
    set_tool_summary_locale(spec, "zh", "管理任务预算");
    spec.help = "fs.budget <set|meter> ...";
    set_tool_help_locale(spec, "en", spec.help);
    set_tool_help_locale(spec, "zh", "fs.budget <set|meter> ...");
    spec.hidden = true;
    spec.requiresExplicitExpose = true;
    spec.subs = {
      admin_subcommand_from(FsBudgetSet::ui()),
      admin_subcommand_from(FsBudgetMeter::ui())
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    if(request.tokens.size() < 2){
      g_parse_error_cmd = "fs.budget";
      return detail::text_result("usage: fs.budget <subcommand> ...\n", 1);
    }
    const std::string& sub = request.tokens[1];
    ToolExecutionRequest forwarded = request;
    forwarded.tokens.clear();
    forwarded.tokens.push_back("fs.budget." + sub);
    for(size_t i = 2; i < request.tokens.size(); ++i){
      forwarded.tokens.push_back(request.tokens[i]);
    }
    if(sub == "set") return FsBudgetSet::run(forwarded);
    if(sub == "meter") return FsBudgetMeter::run(forwarded);
    g_parse_error_cmd = "fs.budget";
    return detail::text_result("unknown fs.budget subcommand: " + sub + "\n", 1);
  }
};

struct FsLog {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "fs.log";
    spec.summary = "Record orchestration logs";
    set_tool_summary_locale(spec, "en", "Record orchestration logs");
    set_tool_summary_locale(spec, "zh", "记录编排日志");
    spec.help = "fs.log <event> ...";
    set_tool_help_locale(spec, "en", spec.help);
    set_tool_help_locale(spec, "zh", "fs.log <event> ...");
    spec.hidden = true;
    spec.requiresExplicitExpose = true;
    spec.subs = {admin_subcommand_from(FsLogEvent::ui())};
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    if(request.tokens.size() < 2){
      g_parse_error_cmd = "fs.log";
      return detail::text_result("usage: fs.log <subcommand> ...\n", 1);
    }
    const std::string& sub = request.tokens[1];
    ToolExecutionRequest forwarded = request;
    forwarded.tokens.clear();
    forwarded.tokens.push_back("fs.log." + sub);
    for(size_t i = 2; i < request.tokens.size(); ++i){
      forwarded.tokens.push_back(request.tokens[i]);
    }
    if(sub == "event") return FsLogEvent::run(forwarded);
    g_parse_error_cmd = "fs.log";
    return detail::text_result("unknown fs.log subcommand: " + sub + "\n", 1);
  }
};

struct FsReport {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "fs.report";
    spec.summary = "Generate orchestration reports";
    set_tool_summary_locale(spec, "en", "Generate orchestration reports");
    set_tool_summary_locale(spec, "zh", "生成编排报告");
    spec.help = "fs.report <summary> ...";
    set_tool_help_locale(spec, "en", spec.help);
    set_tool_help_locale(spec, "zh", "fs.report <summary> ...");
    spec.hidden = true;
    spec.requiresExplicitExpose = true;
    spec.subs = {admin_subcommand_from(FsReportSummary::ui())};
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    if(request.tokens.size() < 2){
      g_parse_error_cmd = "fs.report";
      return detail::text_result("usage: fs.report <subcommand> ...\n", 1);
    }
    const std::string& sub = request.tokens[1];
    ToolExecutionRequest forwarded = request;
    forwarded.tokens.clear();
    forwarded.tokens.push_back("fs.report." + sub);
    for(size_t i = 2; i < request.tokens.size(); ++i){
      forwarded.tokens.push_back(request.tokens[i]);
    }
    if(sub == "summary") return FsReportSummary::run(forwarded);
    g_parse_error_cmd = "fs.report";
    return detail::text_result("unknown fs.report subcommand: " + sub + "\n", 1);
  }
};

inline ToolDefinition make_fs_budget_tool(){
  ToolDefinition def;
  def.ui = FsBudget::ui();
  def.executor = FsBudget::run;
  return def;
}

inline ToolDefinition make_fs_timer_tool(){ ToolDefinition def; def.ui = FsTimer::ui(); def.executor = FsTimer::run; return def; }

inline ToolDefinition make_fs_log_tool(){
  ToolDefinition def;
  def.ui = FsLog::ui();
  def.executor = FsLog::run;
  return def;
}

inline ToolDefinition make_fs_report_tool(){
  ToolDefinition def;
  def.ui = FsReport::ui();
  def.executor = FsReport::run;
  return def;
}

} // namespace tool

