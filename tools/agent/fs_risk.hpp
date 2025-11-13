#pragma once

#include "../tool_common.hpp"
#include "../../utils/agent_commands.hpp"

namespace tool {

inline ToolSpec build_risk_spec(const std::string& name,
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

inline SubcommandSpec risk_subcommand_from(const ToolSpec& spec){
  SubcommandSpec sub;
  auto pos = spec.name.find_last_of('.');
  sub.name = (pos == std::string::npos) ? spec.name : spec.name.substr(pos + 1);
  sub.options = spec.options;
  sub.positional = spec.positional;
  sub.mutexGroups = {};
  sub.handler = nullptr;
  return sub;
}

struct FsRiskAssess {
  static ToolSpec ui(){
    return build_risk_spec(
      "fs.risk.assess",
      "Assess plan risk levels",
      "评估计划风险等级",
      "fs.risk.assess --plan <id>",
      "fs.risk.assess --plan <标识>",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_risk_assess(request);
  }
};

struct FsRequestReview {
  static ToolSpec ui(){
    return build_risk_spec(
      "fs.request.review",
      "Prepare a review package",
      "生成审阅包",
      "fs.request.review --plan <id> --intent <text> [--step <id>] [--diff <text>] [--rollback <text>]",
      "fs.request.review --plan <标识> --intent <意图> [--step <步骤>] [--diff <差异>] [--rollback <回滚方案>]",
      {
        OptionSpec{"--plan", true, {}, nullptr, true, "<plan>"},
        OptionSpec{"--intent", true, {}, nullptr, true, "<intent>"},
        OptionSpec{"--step", true, {}, nullptr, false, "<step>"},
        OptionSpec{"--diff", true, {}, nullptr, false, "<diff>"},
        OptionSpec{"--rollback", true, {}, nullptr, false, "<rollback>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_request_review(request);
  }
};

struct FsRisk {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "fs.risk";
    spec.summary = "Evaluate plan risks";
    set_tool_summary_locale(spec, "en", "Evaluate plan risks");
    set_tool_summary_locale(spec, "zh", "评估计划风险");
    spec.help = "fs.risk <assess> ...";
    set_tool_help_locale(spec, "en", spec.help);
    set_tool_help_locale(spec, "zh", "fs.risk <assess> ...");
    spec.hidden = true;
    spec.requiresExplicitExpose = true;
    spec.subs = {
      risk_subcommand_from(FsRiskAssess::ui())
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    if(request.tokens.size() < 2){
      g_parse_error_cmd = "fs.risk";
      return detail::text_result("usage: fs.risk <subcommand> ...\n", 1);
    }
    const std::string& sub = request.tokens[1];
    ToolExecutionRequest forwarded = request;
    forwarded.tokens.clear();
    forwarded.tokens.push_back("fs.risk." + sub);
    for(size_t i = 2; i < request.tokens.size(); ++i){
      forwarded.tokens.push_back(request.tokens[i]);
    }
    if(sub == "assess") return FsRiskAssess::run(forwarded);
    g_parse_error_cmd = "fs.risk";
    return detail::text_result("unknown fs.risk subcommand: " + sub + "\n", 1);
  }
};

struct FsRequest {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "fs.request";
    spec.summary = "Manage guarded review requests";
    set_tool_summary_locale(spec, "en", "Manage guarded review requests");
    set_tool_summary_locale(spec, "zh", "管理受控审阅请求");
    spec.help = "fs.request <review> ...";
    set_tool_help_locale(spec, "en", spec.help);
    set_tool_help_locale(spec, "zh", "fs.request <review> ...");
    spec.hidden = true;
    spec.requiresExplicitExpose = true;
    spec.subs = {
      risk_subcommand_from(FsRequestReview::ui())
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    if(request.tokens.size() < 2){
      g_parse_error_cmd = "fs.request";
      return detail::text_result("usage: fs.request <subcommand> ...\n", 1);
    }
    const std::string& sub = request.tokens[1];
    ToolExecutionRequest forwarded = request;
    forwarded.tokens.clear();
    forwarded.tokens.push_back("fs.request." + sub);
    for(size_t i = 2; i < request.tokens.size(); ++i){
      forwarded.tokens.push_back(request.tokens[i]);
    }
    if(sub == "review") return FsRequestReview::run(forwarded);
    g_parse_error_cmd = "fs.request";
    return detail::text_result("unknown fs.request subcommand: " + sub + "\n", 1);
  }
};

inline ToolDefinition make_fs_risk_tool(){
  ToolDefinition def;
  def.ui = FsRisk::ui();
  def.executor = FsRisk::run;
  return def;
}

inline ToolDefinition make_fs_request_tool(){
  ToolDefinition def;
  def.ui = FsRequest::ui();
  def.executor = FsRequest::run;
  return def;
}

} // namespace tool

