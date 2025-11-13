#pragma once

#include "tool_common.hpp"
#include "../utils/agent_commands.hpp"

namespace tool {

inline ToolSpec build_guard_spec(const std::string& name,
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

inline SubcommandSpec guard_subcommand_from(const ToolSpec& spec){
  SubcommandSpec sub;
  auto pos = spec.name.find_last_of('.');
  sub.name = (pos == std::string::npos) ? spec.name : spec.name.substr(pos + 1);
  sub.options = spec.options;
  sub.positional = spec.positional;
  sub.mutexGroups = {};
  sub.handler = nullptr;
  return sub;
}

struct FsGuardFs {
  static ToolSpec ui(){
    return build_guard_spec(
      "fs.guard.fs",
      "Check a filesystem operation",
      "检查文件系统操作",
      "fs.guard.fs --op read|write --path <path> [--size <bytes>]",
      "fs.guard.fs --op read|write --path <路径> [--size <字节数>]",
      {
        OptionSpec{"--op", true, {"read", "write"}, nullptr, true, "<op>"},
        OptionSpec{"--path", true, {}, nullptr, true, "<path>"},
        OptionSpec{"--size", true, {}, nullptr, false, "<bytes>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_guard_fs(request);
  }
};

struct FsGuardShell {
  static ToolSpec ui(){
    return build_guard_spec(
      "fs.guard.shell",
      "Check a shell command",
      "检查 shell 命令",
      "fs.guard.shell --command <text>",
      "fs.guard.shell --command <命令>",
      {
        OptionSpec{"--command", true, {}, nullptr, true, "<command>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_guard_shell(request);
  }
};

struct FsGuardNet {
  static ToolSpec ui(){
    return build_guard_spec(
      "fs.guard.net",
      "Check a network request",
      "检查网络请求",
      "fs.guard.net --host <host>",
      "fs.guard.net --host <主机>",
      {
        OptionSpec{"--host", true, {}, nullptr, true, "<host>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_guard_net(request);
  }
};

struct FsGuard {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "fs.guard";
    spec.summary = "Assess guarded operations";
    set_tool_summary_locale(spec, "en", "Assess guarded operations");
    set_tool_summary_locale(spec, "zh", "评估受保护的操作");
    spec.help = "fs.guard <fs|shell|net> ...";
    set_tool_help_locale(spec, "en", spec.help);
    set_tool_help_locale(spec, "zh", "fs.guard <fs|shell|net> ...");
    spec.hidden = true;
    spec.requiresExplicitExpose = true;
    spec.subs = {
      guard_subcommand_from(FsGuardFs::ui()),
      guard_subcommand_from(FsGuardShell::ui()),
      guard_subcommand_from(FsGuardNet::ui())
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    if(request.tokens.size() < 2){
      g_parse_error_cmd = "fs.guard";
      return detail::text_result("usage: fs.guard <subcommand> ...\n", 1);
    }
    const std::string& sub = request.tokens[1];
    ToolExecutionRequest forwarded = request;
    forwarded.tokens.clear();
    forwarded.tokens.push_back("fs.guard." + sub);
    for(size_t i = 2; i < request.tokens.size(); ++i){
      forwarded.tokens.push_back(request.tokens[i]);
    }
    if(sub == "fs") return FsGuardFs::run(forwarded);
    if(sub == "shell") return FsGuardShell::run(forwarded);
    if(sub == "net") return FsGuardNet::run(forwarded);
    g_parse_error_cmd = "fs.guard";
    return detail::text_result("unknown fs.guard subcommand: " + sub + "\n", 1);
  }
};

inline ToolDefinition make_fs_guard_tool(){
  ToolDefinition def;
  def.ui = FsGuard::ui();
  def.executor = FsGuard::run;
  return def;
}

} // namespace tool

