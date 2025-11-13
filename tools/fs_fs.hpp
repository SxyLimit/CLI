#pragma once

#include "tool_common.hpp"
#include "../utils/agent_commands.hpp"

namespace tool {

inline ToolSpec build_fs_spec(const std::string& name,
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

inline SubcommandSpec fs_subcommand_from(const ToolSpec& spec){
  SubcommandSpec sub;
  auto pos = spec.name.find_last_of('.');
  sub.name = (pos == std::string::npos) ? spec.name : spec.name.substr(pos + 1);
  sub.options = spec.options;
  sub.positional = spec.positional;
  sub.mutexGroups = {};
  sub.handler = nullptr;
  return sub;
}

struct FsFsRead {
  static ToolSpec ui(){
    auto spec = tool::FsRead::ui();
    spec.name = "fs.fs.read";
    spec.summary = "Alias for fs.read in orchestrator namespace";
    set_tool_summary_locale(spec, "en", "Alias for fs.read in orchestrator namespace");
    set_tool_summary_locale(spec, "zh", "编排用命名空间下的 fs.read 别名");
    spec.hidden = true;
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_fs_read(request);
  }
};

struct FsFsWriteSafe {
  static ToolSpec ui(){
    auto spec = tool::FsWrite::ui();
    spec.name = "fs.fs.write_safe";
    spec.summary = "Alias for fs.write";
    set_tool_summary_locale(spec, "en", "Alias for fs.write");
    set_tool_summary_locale(spec, "zh", "fs.write 的别名");
    spec.hidden = true;
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_fs_write_safe(request);
  }
};

struct FsFsSnapshot {
  static ToolSpec ui(){
    return build_fs_spec(
      "fs.fs.snapshot",
      "Create a filesystem snapshot",
      "创建文件系统快照",
      "fs.fs.snapshot --path <path> [--path <path>...] [--reason <text>]",
      "fs.fs.snapshot --path <路径> [--path <路径>...] [--reason <原因>]",
      {
        OptionSpec{"--path", true, {}, nullptr, true, "<path>"},
        OptionSpec{"--reason", true, {}, nullptr, false, "<reason>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_fs_snapshot(request);
  }
};

struct FsFsDiff {
  static ToolSpec ui(){
    return build_fs_spec(
      "fs.fs.diff",
      "Compare two filesystem snapshots",
      "比较两个文件系统快照",
      "fs.fs.diff --from <snapshot> --to <snapshot>",
      "fs.fs.diff --from <快照> --to <快照>",
      {
        OptionSpec{"--from", true, {}, nullptr, true, "<snapshot>"},
        OptionSpec{"--to", true, {}, nullptr, true, "<snapshot>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_fs_diff(request);
  }
};

struct FsFs {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "fs.fs";
    spec.summary = "Manage orchestrator filesystem helpers";
    set_tool_summary_locale(spec, "en", "Manage orchestrator filesystem helpers");
    set_tool_summary_locale(spec, "zh", "管理编排文件系统工具");
    spec.help = "fs.fs <read|write_safe|snapshot|diff> ...";
    set_tool_help_locale(spec, "en", spec.help);
    set_tool_help_locale(spec, "zh", "fs.fs <read|write_safe|snapshot|diff> ...");
    spec.hidden = true;
    spec.requiresExplicitExpose = true;
    spec.subs = {
      fs_subcommand_from(FsFsRead::ui()),
      fs_subcommand_from(FsFsWriteSafe::ui()),
      fs_subcommand_from(FsFsSnapshot::ui()),
      fs_subcommand_from(FsFsDiff::ui())
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    if(request.tokens.size() < 2){
      g_parse_error_cmd = "fs.fs";
      return detail::text_result("usage: fs.fs <subcommand> ...\n", 1);
    }
    const std::string& sub = request.tokens[1];
    ToolExecutionRequest forwarded = request;
    forwarded.tokens.clear();
    forwarded.tokens.push_back("fs.fs." + sub);
    for(size_t i = 2; i < request.tokens.size(); ++i){
      forwarded.tokens.push_back(request.tokens[i]);
    }
    if(sub == "read") return FsFsRead::run(forwarded);
    if(sub == "write_safe") return FsFsWriteSafe::run(forwarded);
    if(sub == "snapshot") return FsFsSnapshot::run(forwarded);
    if(sub == "diff") return FsFsDiff::run(forwarded);
    g_parse_error_cmd = "fs.fs";
    return detail::text_result("unknown fs.fs subcommand: " + sub + "\n", 1);
  }
};

inline ToolDefinition make_fs_fs_tool(){
  ToolDefinition def;
  def.ui = FsFs::ui();
  def.executor = FsFs::run;
  return def;
}

} // namespace tool

