#pragma once

#include "../tool_common.hpp"
#include "fs_common.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <system_error>
#include <string>

namespace tool {

struct FsWriteOptions {
  std::filesystem::path path;
  bool hasContent = false;
  std::string content;
  bool hasContentFile = false;
  std::filesystem::path contentFile;
  std::string mode = "overwrite";
  std::string encoding = "utf-8";
  bool createParents = false;
  std::string eol = "preserve";
  bool backup = false;
  bool atomic = false;
  bool dryRun = false;
};

struct FsWriteResult {
  int exitCode = 0;
  size_t bytesWritten = 0;
  std::string backupPath;
  bool atomicUsed = false;
  std::string hashBefore;
  std::string hashAfter;
  bool created = false;
  std::string errorCode;
  std::string errorMessage;
  uint64_t durationMs = 0;
};

inline std::string normalize_newlines(const std::string& input){
  std::string out;
  out.reserve(input.size());
  for(size_t i = 0; i < input.size(); ++i){
    char ch = input[i];
    if(ch == '\r'){
      if(i + 1 < input.size() && input[i+1] == '\n'){
        ++i;
      }
      out.push_back('\n');
    }else{
      out.push_back(ch);
    }
  }
  return out;
}

inline std::string convert_eol(const std::string& input, const std::string& eol){
  if(eol == "preserve") return input;
  std::string normalized = normalize_newlines(input);
  if(eol == "lf"){
    return normalized;
  }
  if(eol == "crlf"){
    std::string out;
    out.reserve(normalized.size() * 2);
    for(char ch : normalized){
      if(ch == '\n'){
        out.push_back('\r');
        out.push_back('\n');
      }else{
        out.push_back(ch);
      }
    }
    return out;
  }
  return input;
}

inline std::string unique_backup_path(const std::filesystem::path& original){
  auto base = original;
  std::string suffix = ".bak";
  std::filesystem::path candidate = base;
  candidate += suffix;
  int counter = 1;
  while(std::filesystem::exists(candidate)){
    candidate = base;
    candidate += ".bak" + std::to_string(counter++);
  }
  return candidate.string();
}

inline bool write_text_file(const std::filesystem::path& path, const std::string& content, bool append, std::error_code& ec){
  std::ios::openmode mode = std::ios::binary | std::ios::out;
  if(append) mode |= std::ios::app;
  else mode |= std::ios::trunc;
  std::ofstream ofs(path, mode);
  if(!ofs){
    ec = std::make_error_code(std::errc::io_error);
    return false;
  }
  ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
  if(!ofs){
    ec = std::make_error_code(std::errc::io_error);
    return false;
  }
  ofs.flush();
  if(!ofs){
    ec = std::make_error_code(std::errc::io_error);
    return false;
  }
  ec.clear();
  return true;
}

inline FsWriteResult fs_write_execute(const FsWriteOptions& opts, const AgentFsConfig& cfg){
  auto start = std::chrono::steady_clock::now();
  FsWriteResult result;
  std::error_code ec;
  auto resolved = agent_realpath(opts.path, ec);
  if(ec){
    result.exitCode = 1;
    result.errorCode = "cannot_open";
    result.errorMessage = "failed to resolve path";
    return result;
  }
  if(!path_within_sandbox(cfg, resolved)){
    result.exitCode = 1;
    result.errorCode = "denied";
    result.errorMessage = "path outside sandbox";
    return result;
  }
  if(resolved.has_extension() && !path_has_allowed_extension(cfg, resolved)){
    result.exitCode = 1;
    result.errorCode = "denied";
    result.errorMessage = "extension not allowed";
    return result;
  }
  bool existed = std::filesystem::exists(resolved);
  result.created = !existed;
  std::string beforeContent;
  if(existed){
    beforeContent = read_file_to_string(resolved, ec);
    if(ec){
      result.exitCode = 1;
      result.errorCode = "cannot_open";
      result.errorMessage = "failed to read existing file";
      return result;
    }
  }
  result.hashBefore = hash_hex(fnv1a_64(beforeContent));

  std::string writeData;
  if(opts.hasContentFile){
    auto contentResolved = agent_realpath(opts.contentFile, ec);
    if(ec || !path_within_sandbox(cfg, contentResolved)){
      result.exitCode = 1;
      result.errorCode = "denied";
      result.errorMessage = "content file outside sandbox";
      return result;
    }
    std::string fromFile = read_file_to_string(contentResolved, ec);
    if(ec){
      result.exitCode = 1;
      result.errorCode = "cannot_open";
      result.errorMessage = "failed to read content file";
      return result;
    }
    writeData = fromFile;
  }else if(opts.hasContent){
    writeData = opts.content;
  }

  writeData = convert_eol(writeData, opts.eol);
  if(writeData.size() > cfg.maxWriteBytes){
    result.exitCode = 1;
    result.errorCode = "too_large";
    result.errorMessage = "content exceeds allowed limit";
    return result;
  }
  if(opts.encoding != "utf-8" && opts.encoding != "utf8"){
    result.exitCode = 1;
    result.errorCode = "encoding_error";
    result.errorMessage = "only utf-8 encoding is supported";
    return result;
  }

  std::string finalContent;
  if(opts.mode == "append"){
    finalContent = beforeContent;
    finalContent += writeData;
  }else{
    finalContent = writeData;
  }
  result.bytesWritten = writeData.size();
  result.hashAfter = hash_hex(fnv1a_64(finalContent));

  if(opts.dryRun){
    result.exitCode = 0;
    auto end = std::chrono::steady_clock::now();
    result.durationMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    return result;
  }

  if(opts.createParents){
    auto parent = resolved.parent_path();
    if(!parent.empty()){
      std::filesystem::create_directories(parent, ec);
      if(ec){
        result.exitCode = 1;
        result.errorCode = "io_error";
        result.errorMessage = "failed to create parent directories";
        return result;
      }
    }
  }

  if(opts.backup && existed){
    std::string backupPath = unique_backup_path(resolved);
    std::filesystem::path backup = backupPath;
    auto backupParent = backup.parent_path();
    if(!backupParent.empty()){
      std::filesystem::create_directories(backupParent, ec);
      if(ec){
        result.exitCode = 1;
        result.errorCode = "io_error";
        result.errorMessage = "failed to prepare backup directory";
        return result;
      }
    }
    ec.clear();
    std::filesystem::copy_file(resolved, backup, std::filesystem::copy_options::overwrite_existing, ec);
    if(ec){
      result.exitCode = 1;
      result.errorCode = "io_error";
      result.errorMessage = "failed to create backup";
      return result;
    }
    result.backupPath = backupPath;
  }

  if(opts.atomic){
    std::filesystem::path tempPath = resolved;
    tempPath += ".tmp-" + random_session_id();
    bool appendMode = false;
    std::string dataToWrite = finalContent;
    if(!write_text_file(tempPath, dataToWrite, false, ec)){
      std::filesystem::remove(tempPath);
      result.exitCode = 1;
      result.errorCode = "io_error";
      result.errorMessage = "failed to write temp file";
      return result;
    }
    std::filesystem::rename(tempPath, resolved, ec);
    if(ec){
      std::error_code removeEc;
      std::filesystem::remove(resolved, removeEc);
      ec.clear();
      std::filesystem::rename(tempPath, resolved, ec);
    }
    if(ec){
      std::filesystem::remove(tempPath);
      result.exitCode = 1;
      result.errorCode = "io_error";
      result.errorMessage = "failed to commit atomic write";
      return result;
    }
    result.atomicUsed = true;
  }else{
    bool appendMode = (opts.mode == "append");
    if(!write_text_file(resolved, writeData, appendMode, ec)){
      result.exitCode = 1;
      result.errorCode = "io_error";
      result.errorMessage = "failed to write file";
      return result;
    }
  }

  auto end = std::chrono::steady_clock::now();
  result.durationMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
  result.exitCode = 0;
  return result;
}

struct FsWrite {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "fs.write";
    spec.summary = "Write text files with sandbox enforcement";
    spec.hidden = true;
    spec.requiresExplicitExpose = true;
    set_tool_summary_locale(spec, "en", "Write text files with sandbox enforcement");
    set_tool_summary_locale(spec, "zh", "在沙盒内写入文本文件");
    set_tool_help_locale(spec, "en", "fs.write <path> (--content TEXT | --content-file PATH) [--mode overwrite|append] [--encoding utf-8] [--create-parents] [--eol lf|crlf] [--backup] [--atomic] [--dry-run]");
    set_tool_help_locale(spec, "zh", "fs.write <路径> (--content 文本 | --content-file 路径) [--mode overwrite|append] [--encoding utf-8] [--create-parents] [--eol lf|crlf] [--backup] [--atomic] [--dry-run]");
    spec.positional = {tool::positional("<path>", true, PathKind::File, {".py", ".md", ".txt", ".json", ".yaml", ".yml", ".toml", ".html", ".css", ".js"}, false)};
    spec.options = {
      OptionSpec{"--content", true, {}, nullptr, false, "<text>"},
      OptionSpec{"--content-file", true, {}, nullptr, false, "<path>", true, PathKind::File, false, {".py", ".md", ".txt", ".json", ".yaml", ".yml", ".toml", ".html", ".css", ".js"}},
      OptionSpec{"--mode", true, {"overwrite", "append"}, nullptr, false, "<mode>"},
      OptionSpec{"--encoding", true, {"utf-8"}, nullptr, false, "<encoding>"},
      OptionSpec{"--create-parents", false},
      OptionSpec{"--eol", true, {"preserve", "lf", "crlf"}, nullptr, false, "<eol>"},
      OptionSpec{"--backup", false},
      OptionSpec{"--atomic", false},
      OptionSpec{"--dry-run", false}
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 2){
      g_parse_error_cmd = "fs.write";
      return detail::text_result("usage: fs.write <path> (--content TEXT | --content-file PATH) [options]\n", 1);
    }
    auto cfg = default_agent_fs_config();
    FsWriteOptions opts;
    opts.path = args[1];
    for(size_t i = 2; i < args.size(); ++i){
      const std::string& tok = args[i];
      if(tok == "--content"){
        if(i + 1 >= args.size()){
          g_parse_error_cmd = "fs.write";
          return detail::text_result("fs.write: missing value for --content\n", 1);
        }
        opts.hasContent = true;
        opts.content = args[++i];
      }else if(tok == "--content-file"){
        if(i + 1 >= args.size()){
          g_parse_error_cmd = "fs.write";
          return detail::text_result("fs.write: missing value for --content-file\n", 1);
        }
        opts.hasContentFile = true;
        opts.contentFile = args[++i];
      }else if(tok == "--mode"){
        if(i + 1 >= args.size()){
          g_parse_error_cmd = "fs.write";
          return detail::text_result("fs.write: missing value for --mode\n", 1);
        }
        opts.mode = args[++i];
      }else if(tok == "--encoding"){
        if(i + 1 >= args.size()){
          g_parse_error_cmd = "fs.write";
          return detail::text_result("fs.write: missing value for --encoding\n", 1);
        }
        opts.encoding = args[++i];
      }else if(tok == "--create-parents"){
        opts.createParents = true;
      }else if(tok == "--eol"){
        if(i + 1 >= args.size()){
          g_parse_error_cmd = "fs.write";
          return detail::text_result("fs.write: missing value for --eol\n", 1);
        }
        opts.eol = args[++i];
      }else if(tok == "--backup"){
        opts.backup = true;
      }else if(tok == "--atomic"){
        opts.atomic = true;
      }else if(tok == "--dry-run"){
        opts.dryRun = true;
      }else{
        g_parse_error_cmd = "fs.write";
        return detail::text_result("fs.write: unknown option " + tok + "\n", 1);
      }
    }
    if(opts.hasContent == opts.hasContentFile){
      g_parse_error_cmd = "fs.write";
      return detail::text_result("fs.write: specify exactly one of --content or --content-file\n", 1);
    }
    if(opts.mode != "overwrite" && opts.mode != "append"){
      g_parse_error_cmd = "fs.write";
      return detail::text_result("fs.write: --mode must be overwrite or append\n", 1);
    }
    if(opts.eol != "preserve" && opts.eol != "lf" && opts.eol != "crlf"){
      g_parse_error_cmd = "fs.write";
      return detail::text_result("fs.write: --eol must be preserve|lf|crlf\n", 1);
    }

    auto exec = fs_write_execute(opts, cfg);
    ToolExecutionResult out;
    out.exitCode = exec.exitCode;
    if(exec.exitCode != 0){
      g_parse_error_cmd = "fs.write";
      out.output = exec.errorMessage + "\n";
      sj::Object meta;
      meta.emplace("error", sj::Value(exec.errorCode));
      meta.emplace("message", sj::Value(exec.errorMessage));
      meta.emplace("duration_ms", sj::Value(static_cast<long long>(exec.durationMs)));
      out.metaJson = sj::dump(sj::Value(std::move(meta)));
      return out;
    }
    std::ostringstream oss;
    if(opts.dryRun){
      oss << "[dry-run] would write " << exec.bytesWritten << " bytes to " << opts.path << "\n";
    }else{
      oss << "wrote " << exec.bytesWritten << " bytes to " << opts.path << "\n";
    }
    out.output = oss.str();
    sj::Object meta;
    meta.emplace("bytes_written", sj::Value(static_cast<long long>(exec.bytesWritten)));
    meta.emplace("backup_path", sj::Value(exec.backupPath));
    meta.emplace("atomic", sj::Value(exec.atomicUsed));
    meta.emplace("hash_before", sj::Value(exec.hashBefore));
    meta.emplace("hash_after", sj::Value(exec.hashAfter));
    meta.emplace("created", sj::Value(exec.created));
    meta.emplace("duration_ms", sj::Value(static_cast<long long>(exec.durationMs)));
    out.metaJson = sj::dump(sj::Value(std::move(meta)));
    return out;
  }
};

inline ToolDefinition make_fs_write_tool(){
  ToolDefinition def;
  def.ui = FsWrite::ui();
  def.executor = FsWrite::run;
  return def;
}

} // namespace tool

