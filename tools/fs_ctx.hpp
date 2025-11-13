#pragma once

#include "tool_common.hpp"
#include "../utils/agent_commands.hpp"

namespace tool {

inline ToolSpec build_ctx_spec(const std::string& name,
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

inline SubcommandSpec ctx_subcommand_from(const ToolSpec& spec){
  SubcommandSpec sub;
  auto pos = spec.name.find_last_of('.');
  sub.name = (pos == std::string::npos) ? spec.name : spec.name.substr(pos + 1);
  sub.options = spec.options;
  sub.positional = spec.positional;
  sub.mutexGroups = {};
  sub.handler = nullptr;
  return sub;
}

struct FsCtxScope {
  static ToolSpec ui(){
    return build_ctx_spec(
      "fs.ctx.scope",
      "Configure task scope",
      "配置任务作用域",
      "fs.ctx.scope --task <id> [--allow <path>]... [--deny <path>]... [--type <kind>]...",
      "fs.ctx.scope --task <标识> [--allow <路径>]... [--deny <路径>]... [--type <类型>]...",
      {
        OptionSpec{"--task", true, {}, nullptr, true, "<task>"},
        OptionSpec{"--allow", true, {}, nullptr, false, "<path>"},
        OptionSpec{"--deny", true, {}, nullptr, false, "<path>"},
        OptionSpec{"--type", true, {}, nullptr, false, "<type>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_ctx_scope(request);
  }
};

struct FsCtxCapture {
  static ToolSpec ui(){
    return build_ctx_spec(
      "fs.ctx.capture",
      "Capture a context entry",
      "登记上下文条目",
      "fs.ctx.capture --task <id> --type <kind> --title <text> --summary <text> [--path <path>]... [--tags <a,b>] [--keywords <k1,k2>] [--source <id>] [--payload-ref <ref>]",
      "fs.ctx.capture --task <标识> --type <类别> --title <标题> --summary <摘要> [--path <路径>]... [--tags <标签列表>] [--keywords <关键词>] [--source <来源>] [--payload-ref <引用>]",
      {
        OptionSpec{"--task", true, {}, nullptr, true, "<task>"},
        OptionSpec{"--type", true, {}, nullptr, false, "<type>"},
        OptionSpec{"--title", true, {}, nullptr, true, "<title>"},
        OptionSpec{"--summary", true, {}, nullptr, true, "<summary>"},
        OptionSpec{"--path", true, {}, nullptr, false, "<path>"},
        OptionSpec{"--tags", true, {}, nullptr, false, "a,b"},
        OptionSpec{"--keywords", true, {}, nullptr, false, "k1,k2"},
        OptionSpec{"--source", true, {}, nullptr, false, "<source>"},
        OptionSpec{"--payload-ref", true, {}, nullptr, false, "<ref>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_ctx_capture(request);
  }
};

struct FsCtxPin {
  static ToolSpec ui(){
    return build_ctx_spec(
      "fs.ctx.pin",
      "Pin context entries",
      "固定上下文条目",
      "fs.ctx.pin --entry <id> [--entry <id>...]",
      "fs.ctx.pin --entry <标识> [--entry <标识>...]",
      {
        OptionSpec{"--entry", true, {}, nullptr, true, "<entry>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_ctx_pin(request, true);
  }
};

struct FsCtxUnpin {
  static ToolSpec ui(){
    return build_ctx_spec(
      "fs.ctx.unpin",
      "Unpin context entries",
      "取消固定上下文条目",
      "fs.ctx.unpin --entry <id> [--entry <id>...]",
      "fs.ctx.unpin --entry <标识> [--entry <标识>...]",
      {
        OptionSpec{"--entry", true, {}, nullptr, true, "<entry>"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_ctx_pin(request, false);
  }
};

struct FsCtxPackForMic {
  static ToolSpec ui(){
    return build_ctx_spec(
      "fs.ctx.pack_for_mic",
      "Assemble side context",
      "生成 side context",
      "fs.ctx.pack_for_mic --task <id> [--token-cap <n>] [--type-priority <t1,t2>]",
      "fs.ctx.pack_for_mic --task <标识> [--token-cap <上限>] [--type-priority <类型顺序>]",
      {
        OptionSpec{"--task", true, {}, nullptr, false, "<task>"},
        OptionSpec{"--token-cap", true, {}, nullptr, false, "<tokens>"},
        OptionSpec{"--type-priority", true, {}, nullptr, false, "t1,t2"}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_ctx_pack_for_mic(request);
  }
};

struct FsCtxInjectTodo {
  static ToolSpec ui(){
    return build_ctx_spec(
      "fs.ctx.inject_todo",
      "Inject MIC text",
      "注入 MIC 文本",
      "fs.ctx.inject_todo --mic-text <text> [--side-text <text>] [--priority <level>] [--unpinned]",
      "fs.ctx.inject_todo --mic-text <正文> [--side-text <补充>] [--priority <优先级>] [--unpinned]",
      {
        OptionSpec{"--mic-text", true, {}, nullptr, true, "<text>"},
        OptionSpec{"--side-text", true, {}, nullptr, false, "<text>"},
        OptionSpec{"--priority", true, {}, nullptr, false, "<priority>"},
        OptionSpec{"--unpinned", false}
      }
    );
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    return agent::command_ctx_inject_todo(request);
  }
};

template<const char* Name>
struct FsCtxPlaceholder {
  static ToolSpec ui(){
    return build_ctx_spec(
      Name,
      "Placeholder command",
      "占位命令",
      std::string(Name) + " (placeholder)",
      std::string(Name) + " (占位)");
  }

  static ToolExecutionResult run(const ToolExecutionRequest&){
    return agent::command_ctx_placeholder(Name);
  }
};

inline constexpr char kCtxIngest[] = "fs.ctx.ingest";
inline constexpr char kCtxEmbed[] = "fs.ctx.embed";
inline constexpr char kCtxSearch[] = "fs.ctx.search";
inline constexpr char kCtxFetch[] = "fs.ctx.fetch";
inline constexpr char kCtxSummarize[] = "fs.ctx.summarize";
inline constexpr char kCtxCompress[] = "fs.ctx.compress";
inline constexpr char kCtxDepgraph[] = "fs.ctx.depgraph";
inline constexpr char kCtxWatch[] = "fs.ctx.watch";
inline constexpr char kCtxEviction[] = "fs.ctx.eviction";
inline constexpr char kCtxTrace[] = "fs.ctx.trace";
inline constexpr char kCtxOverlay[] = "fs.ctx.overlay";

using FsCtxIngest = FsCtxPlaceholder<kCtxIngest>;
using FsCtxEmbed = FsCtxPlaceholder<kCtxEmbed>;
using FsCtxSearch = FsCtxPlaceholder<kCtxSearch>;
using FsCtxFetch = FsCtxPlaceholder<kCtxFetch>;
using FsCtxSummarize = FsCtxPlaceholder<kCtxSummarize>;
using FsCtxCompress = FsCtxPlaceholder<kCtxCompress>;
using FsCtxDepgraph = FsCtxPlaceholder<kCtxDepgraph>;
using FsCtxWatch = FsCtxPlaceholder<kCtxWatch>;
using FsCtxEviction = FsCtxPlaceholder<kCtxEviction>;
using FsCtxTrace = FsCtxPlaceholder<kCtxTrace>;
using FsCtxOverlay = FsCtxPlaceholder<kCtxOverlay>;

struct FsCtx {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "fs.ctx";
    spec.summary = "Manage orchestrator context entries";
    set_tool_summary_locale(spec, "en", "Manage orchestrator context entries");
    set_tool_summary_locale(spec, "zh", "管理编排上下文条目");
    spec.help = "fs.ctx <scope|capture|pin|unpin|pack_for_mic|inject_todo|ingest|embed|search|fetch|summarize|compress|depgraph|watch|eviction|trace|overlay> ...";
    set_tool_help_locale(spec, "en", spec.help);
    set_tool_help_locale(spec, "zh", "fs.ctx <scope|capture|pin|unpin|pack_for_mic|inject_todo|ingest|embed|search|fetch|summarize|compress|depgraph|watch|eviction|trace|overlay> ...");
    spec.hidden = true;
    spec.requiresExplicitExpose = true;
    spec.subs = {
      ctx_subcommand_from(FsCtxScope::ui()),
      ctx_subcommand_from(FsCtxCapture::ui()),
      ctx_subcommand_from(FsCtxPin::ui()),
      ctx_subcommand_from(FsCtxUnpin::ui()),
      ctx_subcommand_from(FsCtxPackForMic::ui()),
      ctx_subcommand_from(FsCtxInjectTodo::ui()),
      ctx_subcommand_from(FsCtxIngest::ui()),
      ctx_subcommand_from(FsCtxEmbed::ui()),
      ctx_subcommand_from(FsCtxSearch::ui()),
      ctx_subcommand_from(FsCtxFetch::ui()),
      ctx_subcommand_from(FsCtxSummarize::ui()),
      ctx_subcommand_from(FsCtxCompress::ui()),
      ctx_subcommand_from(FsCtxDepgraph::ui()),
      ctx_subcommand_from(FsCtxWatch::ui()),
      ctx_subcommand_from(FsCtxEviction::ui()),
      ctx_subcommand_from(FsCtxTrace::ui()),
      ctx_subcommand_from(FsCtxOverlay::ui())
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    if(request.tokens.size() < 2){
      g_parse_error_cmd = "fs.ctx";
      return detail::text_result("usage: fs.ctx <subcommand> ...\n", 1);
    }
    const std::string& sub = request.tokens[1];
    ToolExecutionRequest forwarded = request;
    forwarded.tokens.clear();
    forwarded.tokens.push_back("fs.ctx." + sub);
    for(size_t i = 2; i < request.tokens.size(); ++i){
      forwarded.tokens.push_back(request.tokens[i]);
    }
    if(sub == "scope") return FsCtxScope::run(forwarded);
    if(sub == "capture") return FsCtxCapture::run(forwarded);
    if(sub == "pin") return FsCtxPin::run(forwarded);
    if(sub == "unpin") return FsCtxUnpin::run(forwarded);
    if(sub == "pack_for_mic") return FsCtxPackForMic::run(forwarded);
    if(sub == "inject_todo") return FsCtxInjectTodo::run(forwarded);
    if(sub == "ingest") return FsCtxIngest::run(forwarded);
    if(sub == "embed") return FsCtxEmbed::run(forwarded);
    if(sub == "search") return FsCtxSearch::run(forwarded);
    if(sub == "fetch") return FsCtxFetch::run(forwarded);
    if(sub == "summarize") return FsCtxSummarize::run(forwarded);
    if(sub == "compress") return FsCtxCompress::run(forwarded);
    if(sub == "depgraph") return FsCtxDepgraph::run(forwarded);
    if(sub == "watch") return FsCtxWatch::run(forwarded);
    if(sub == "eviction") return FsCtxEviction::run(forwarded);
    if(sub == "trace") return FsCtxTrace::run(forwarded);
    if(sub == "overlay") return FsCtxOverlay::run(forwarded);
    g_parse_error_cmd = "fs.ctx";
    return detail::text_result("unknown fs.ctx subcommand: " + sub + "\n", 1);
  }
};

inline ToolDefinition make_fs_ctx_tool(){
  ToolDefinition def;
  def.ui = FsCtx::ui();
  def.executor = FsCtx::run;
  return def;
}

} // namespace tool

