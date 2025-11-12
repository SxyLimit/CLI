#pragma once

#include "../tool_common.hpp"
#include "fs_common.hpp"
#include "fs_write.hpp"

#include <filesystem>
#include <sstream>
#include <system_error>

namespace tool {

struct FsCreateOptions {
  std::filesystem::path path;
  bool hasContent = false;
  std::string content;
  bool hasContentFile = false;
  std::filesystem::path contentFile;
  std::string encoding = "utf-8";
  bool createParents = false;
  std::string eol = "preserve";
  bool atomic = false;
  bool dryRun = false;
};

inline ToolExecutionResult fs_create_error(const ToolExecutionRequest& request,
                                           const std::string& message,
                                           const std::string& code){
  set_agent_parse_error(request, "fs.create");
  ToolExecutionResult out;
  out.exitCode = 1;
  out.output = message + "\n";
  sj::Object meta;
  meta.emplace("error", sj::Value(code));
  meta.emplace("message", sj::Value(message));
  meta.emplace("duration_ms", sj::Value(0LL));
  out.metaJson = sj::dump(sj::Value(std::move(meta)));
  return out;
}

struct FsCreate {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "fs.create";
    spec.summary = "Create a new text file in the sandbox";
    spec.hidden = true;
    spec.requiresExplicitExpose = true;
    set_tool_summary_locale(spec, "en", "Create a new text file in the sandbox");
    set_tool_summary_locale(spec, "zh", "在沙盒内创建新的文本文件");
    set_tool_help_locale(spec, "en", "fs.create <path> [--content TEXT | --content-file PATH] [--encoding utf-8] [--create-parents] [--eol lf|crlf] [--atomic] [--dry-run]");
    set_tool_help_locale(spec, "zh", "fs.create <路径> [--content 文本 | --content-file 路径] [--encoding utf-8] [--create-parents] [--eol lf|crlf] [--atomic] [--dry-run]");
    auto allowed = agent_allowed_extensions();
    spec.positional = {tool::positional("<path>", true, PathKind::File, allowed, false)};
    spec.options = {
      OptionSpec{"--content", true, {}, nullptr, false, "<text>"},
      OptionSpec{"--content-file", true, {}, nullptr, false, "<path>", true, PathKind::File, false, allowed},
      OptionSpec{"--encoding", true, {"utf-8"}, nullptr, false, "<encoding>"},
      OptionSpec{"--create-parents", false},
      OptionSpec{"--eol", true, {"preserve", "lf", "crlf"}, nullptr, false, "<eol>"},
      OptionSpec{"--atomic", false},
      OptionSpec{"--dry-run", false}
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 2){
      return fs_create_error(request, "usage: fs.create <path> [options]", "usage");
    }

    FsCreateOptions opts;
    opts.path = args[1];
    for(size_t i = 2; i < args.size(); ++i){
      const std::string& tok = args[i];
      if(tok == "--content"){
        if(i + 1 >= args.size()){
          return fs_create_error(request, "fs.create: missing value for --content", "validation");
        }
        opts.hasContent = true;
        opts.content = args[++i];
      }else if(tok == "--content-file"){
        if(i + 1 >= args.size()){
          return fs_create_error(request, "fs.create: missing value for --content-file", "validation");
        }
        opts.hasContentFile = true;
        opts.contentFile = args[++i];
      }else if(tok == "--encoding"){
        if(i + 1 >= args.size()){
          return fs_create_error(request, "fs.create: missing value for --encoding", "validation");
        }
        opts.encoding = args[++i];
      }else if(tok == "--create-parents"){
        opts.createParents = true;
      }else if(tok == "--eol"){
        if(i + 1 >= args.size()){
          return fs_create_error(request, "fs.create: missing value for --eol", "validation");
        }
        opts.eol = args[++i];
      }else if(tok == "--atomic"){
        opts.atomic = true;
      }else if(tok == "--dry-run"){
        opts.dryRun = true;
      }else{
        return fs_create_error(request, "fs.create: unknown option " + tok, "validation");
      }
    }

    if(opts.hasContent && opts.hasContentFile){
      return fs_create_error(request, "fs.create: choose either --content or --content-file", "validation");
    }

    AgentFsConfig cfg = default_agent_fs_config();
    std::error_code ec;
    auto resolved = agent_realpath(opts.path, ec);
    if(ec){
      return fs_create_error(request, "fs.create: failed to resolve path", "cannot_open");
    }
    if(!path_within_sandbox(cfg, resolved)){
      return fs_create_error(request, "fs.create: path outside sandbox", "denied");
    }
    if(resolved.has_extension() && !path_has_allowed_extension(cfg, resolved)){
      return fs_create_error(request, "fs.create: extension not allowed", "denied");
    }
    if(std::filesystem::exists(resolved)){
      return fs_create_error(request, "fs.create: file already exists", "already_exists");
    }

    FsWriteOptions writeOpts;
    writeOpts.path = opts.path;
    if(opts.hasContentFile){
      writeOpts.hasContentFile = true;
      writeOpts.contentFile = opts.contentFile;
    }else{
      writeOpts.hasContent = true;
      writeOpts.content = opts.hasContent ? opts.content : std::string();
    }
    writeOpts.mode = "overwrite";
    writeOpts.encoding = opts.encoding;
    writeOpts.createParents = opts.createParents;
    writeOpts.eol = opts.eol;
    writeOpts.atomic = opts.atomic;
    writeOpts.dryRun = opts.dryRun;

    auto exec = fs_write_execute(writeOpts, cfg);
    ToolExecutionResult out;
    out.exitCode = exec.exitCode;
    if(exec.exitCode != 0){
      set_agent_parse_error(request, "fs.create");
      out.output = exec.errorMessage + "\n";
      sj::Object meta;
      meta.emplace("error", sj::Value(exec.errorCode));
      meta.emplace("message", sj::Value(exec.errorMessage));
      meta.emplace("duration_ms", sj::Value(static_cast<long long>(exec.durationMs)));
      out.metaJson = sj::dump(sj::Value(std::move(meta)));
      return out;
    }
    if(!exec.created){
      return fs_create_error(request, "fs.create: file already exists", "already_exists");
    }

    std::ostringstream oss;
    if(writeOpts.dryRun){
      oss << "[dry-run] would create " << opts.path << "\n";
    }else{
      oss << "created " << opts.path << " with " << exec.bytesWritten << " bytes\n";
    }
    out.output = oss.str();

    sj::Object meta;
    meta.emplace("bytes_written", sj::Value(static_cast<long long>(exec.bytesWritten)));
    meta.emplace("hash_before", sj::Value(exec.hashBefore));
    meta.emplace("hash_after", sj::Value(exec.hashAfter));
    meta.emplace("atomic", sj::Value(exec.atomicUsed));
    meta.emplace("created", sj::Value(true));
    meta.emplace("duration_ms", sj::Value(static_cast<long long>(exec.durationMs)));
    meta.emplace("dry_run", sj::Value(writeOpts.dryRun));
    out.metaJson = sj::dump(sj::Value(std::move(meta)));
    return out;
  }
};

inline ToolDefinition make_fs_create_tool(){
  ToolDefinition def;
  def.ui = FsCreate::ui();
  def.executor = FsCreate::run;
  return def;
}

} // namespace tool

