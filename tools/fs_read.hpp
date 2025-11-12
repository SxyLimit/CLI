#pragma once

#include "tool_common.hpp"
#include "fs_common.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <vector>

namespace tool {

struct FsReadOptions {
  std::filesystem::path path;
  std::string encoding = "utf-8";
  size_t maxBytes = 4096;
  bool hasHead = false;
  size_t headLines = 0;
  bool hasTail = false;
  size_t tailLines = 0;
  bool withLineNumbers = false;
  bool hashOnly = false;
  bool hasOffset = false;
  size_t offset = 0;
  bool hasLength = false;
  size_t length = 0;
};

struct FsReadResult {
  int exitCode = 0;
  std::string content;
  bool truncated = false;
  size_t bytesTotal = 0;
  size_t bytesReturned = 0;
  size_t rangeOffset = 0;
  size_t rangeLength = 0;
  std::string hash;
  std::string errorCode;
  std::string errorMessage;
  uint64_t durationMs = 0;
};

inline FsReadResult fs_read_execute(const FsReadOptions& options, const AgentFsConfig& cfg){
  auto start = std::chrono::steady_clock::now();
  FsReadResult result;
  std::error_code ec;
  auto resolved = agent_realpath(options.path, ec);
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
  if(!std::filesystem::exists(resolved)){
    result.exitCode = 1;
    result.errorCode = "cannot_open";
    result.errorMessage = "file not found";
    return result;
  }
  if(!std::filesystem::is_regular_file(resolved)){
    result.exitCode = 1;
    result.errorCode = "validation";
    result.errorMessage = "path is not a regular file";
    return result;
  }
  if(!path_has_allowed_extension(cfg, resolved)){
    result.exitCode = 1;
    result.errorCode = "denied";
    result.errorMessage = "extension not allowed";
    return result;
  }
  std::ifstream file(resolved, std::ios::binary);
  if(!file){
    result.exitCode = 1;
    result.errorCode = "cannot_open";
    result.errorMessage = "failed to open file";
    return result;
  }
  file.seekg(0, std::ios::end);
  auto fileSize = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  result.bytesTotal = fileSize;

  size_t readOffset = 0;
  size_t readLength = fileSize;
  if(options.hasOffset){
    readOffset = std::min(options.offset, fileSize);
  }
  if(options.hasLength){
    readLength = std::min(options.length, fileSize - readOffset);
  }else{
    readLength = fileSize - readOffset;
  }

  if(options.hasHead || options.hasTail){
    // line-based reading
    std::string content;
    file.seekg(0, std::ios::beg);
    std::string line;
    std::vector<std::string> lines;
    while(std::getline(file, line)){
      if(!line.empty() && line.back() == '\r') line.pop_back();
      lines.push_back(line);
    }
    if(options.hasHead){
      size_t count = std::min(options.headLines, lines.size());
      std::ostringstream oss;
      for(size_t i = 0; i < count; ++i){
        if(options.withLineNumbers){
          oss << (i + 1) << ": " << lines[i] << "\n";
        }else{
          oss << lines[i] << "\n";
        }
      }
      content = oss.str();
      if(content.size() > options.maxBytes){
        content.resize(options.maxBytes);
        result.truncated = true;
      }else{
        result.truncated = (count < lines.size());
      }
      result.bytesReturned = content.size();
      result.rangeOffset = 0;
      result.rangeLength = result.bytesReturned;
      std::string hashInput = content;
      result.hash = hash_hex(fnv1a_64(hashInput));
      if(options.hashOnly){
        result.content.clear();
      }else{
        result.content = std::move(content);
      }
    }else{
      size_t count = std::min(options.tailLines, lines.size());
      std::ostringstream oss;
      size_t startIdx = lines.size() - count;
      for(size_t i = 0; i < count; ++i){
        size_t lineNo = startIdx + i + 1;
        if(options.withLineNumbers){
          oss << lineNo << ": " << lines[startIdx + i] << "\n";
        }else{
          oss << lines[startIdx + i] << "\n";
        }
      }
      std::string contentTail = oss.str();
      if(contentTail.size() > options.maxBytes){
        contentTail.resize(options.maxBytes);
        result.truncated = true;
      }else{
        result.truncated = (count < lines.size());
      }
      result.bytesReturned = contentTail.size();
      result.rangeOffset = 0;
      result.rangeLength = result.bytesReturned;
      std::string hashInput = contentTail;
      result.hash = hash_hex(fnv1a_64(hashInput));
      if(options.hashOnly){
        result.content.clear();
      }else{
        result.content = std::move(contentTail);
      }
    }
  }else{
    if(readOffset > 0) file.seekg(static_cast<std::streamoff>(readOffset), std::ios::beg);
    size_t toRead = std::min(readLength, options.maxBytes);
    std::string buffer;
    buffer.resize(toRead);
    file.read(&buffer[0], static_cast<std::streamsize>(toRead));
    std::streamsize actuallyRead = file.gcount();
    buffer.resize(static_cast<size_t>(actuallyRead));
    result.truncated = (readLength > static_cast<size_t>(actuallyRead));
    result.bytesReturned = buffer.size();
    result.rangeOffset = readOffset;
    result.rangeLength = static_cast<size_t>(actuallyRead);
    std::string hashInput = buffer;
    if(options.withLineNumbers && !options.hashOnly){
      std::istringstream iss(buffer);
      std::ostringstream oss;
      std::string line;
      size_t lineNo = 0;
      while(std::getline(iss, line)){
        if(!line.empty() && line.back() == '\r') line.pop_back();
        oss << (++lineNo) << ": " << line << "\n";
      }
      result.content = oss.str();
      hashInput = result.content;
    }else if(options.withLineNumbers && options.hashOnly){
      std::istringstream iss(buffer);
      std::ostringstream oss;
      std::string line;
      size_t lineNo = 0;
      while(std::getline(iss, line)){
        if(!line.empty() && line.back() == '\r') line.pop_back();
        oss << (++lineNo) << ": " << line << "\n";
      }
      hashInput = oss.str();
    }else{
      if(!options.hashOnly){
        result.content = buffer;
      }
    }
    result.hash = hash_hex(fnv1a_64(hashInput));
    if(options.hashOnly){
      result.content.clear();
    }
  }

  auto end = std::chrono::steady_clock::now();
  auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  if(durationMs < 0) durationMs = 0;
  result.durationMs = static_cast<uint64_t>(durationMs);
  return result;
}

struct FsRead {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "fs.read";
    spec.summary = "Read file content with sandbox enforcement";
    spec.hidden = true;
    spec.requiresExplicitExpose = true;
    set_tool_summary_locale(spec, "en", "Read file content with sandbox enforcement");
    set_tool_summary_locale(spec, "zh", "在沙盒内读取文件内容");
    set_tool_help_locale(spec, "en", "fs.read <path> [--encoding utf-8] [--max-bytes N] [--head N|--tail N] [--offset N --length N] [--with-line-numbers] [--hash-only]");
    set_tool_help_locale(spec, "zh", "fs.read <路径> [--encoding utf-8] [--max-bytes N] [--head N|--tail N] [--offset N --length N] [--with-line-numbers] [--hash-only]");
    spec.positional = {tool::positional("<path>", true, PathKind::File, {".py", ".md", ".txt", ".json", ".yaml", ".yml", ".toml", ".html", ".css", ".js"}, false)};
    spec.options = {
      OptionSpec{"--encoding", true, {"utf-8"}, nullptr, false, "<encoding>"},
      OptionSpec{"--max-bytes", true, {}, nullptr, false, "<bytes>"},
      OptionSpec{"--head", true, {}, nullptr, false, "<lines>"},
      OptionSpec{"--tail", true, {}, nullptr, false, "<lines>"},
      OptionSpec{"--offset", true, {}, nullptr, false, "<offset>"},
      OptionSpec{"--length", true, {}, nullptr, false, "<length>"},
      OptionSpec{"--with-line-numbers", false},
      OptionSpec{"--hash-only", false}
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    FsReadOptions opts;
    auto cfg = default_agent_fs_config();
    opts.maxBytes = cfg.maxReadBytes;
    const auto& args = request.tokens;
    if(args.size() < 2){
      g_parse_error_cmd = "fs.read";
      return detail::text_result("usage: fs.read <path> [options]\n", 1);
    }
    opts.path = args[1];
    for(size_t i = 2; i < args.size(); ++i){
      const std::string& tok = args[i];
      if(tok == "--encoding"){
        if(i + 1 >= args.size()){
          g_parse_error_cmd = "fs.read";
          return detail::text_result("fs.read: missing value for --encoding\n", 1);
        }
        opts.encoding = args[++i];
      }else if(tok == "--max-bytes"){
        if(i + 1 >= args.size()){
          g_parse_error_cmd = "fs.read";
          return detail::text_result("fs.read: missing value for --max-bytes\n", 1);
        }
        size_t value = 0;
        if(!parse_size_arg(args[++i], value)){
          g_parse_error_cmd = "fs.read";
          return detail::text_result("fs.read: invalid --max-bytes\n", 1);
        }
        opts.maxBytes = std::min(value, cfg.maxReadBytes);
      }else if(tok == "--head"){
        if(i + 1 >= args.size()){
          g_parse_error_cmd = "fs.read";
          return detail::text_result("fs.read: missing value for --head\n", 1);
        }
        opts.hasHead = true;
        if(!parse_size_arg(args[++i], opts.headLines)){
          g_parse_error_cmd = "fs.read";
          return detail::text_result("fs.read: invalid --head value\n", 1);
        }
      }else if(tok == "--tail"){
        if(i + 1 >= args.size()){
          g_parse_error_cmd = "fs.read";
          return detail::text_result("fs.read: missing value for --tail\n", 1);
        }
        opts.hasTail = true;
        if(!parse_size_arg(args[++i], opts.tailLines)){
          g_parse_error_cmd = "fs.read";
          return detail::text_result("fs.read: invalid --tail value\n", 1);
        }
      }else if(tok == "--offset"){
        if(i + 1 >= args.size()){
          g_parse_error_cmd = "fs.read";
          return detail::text_result("fs.read: missing value for --offset\n", 1);
        }
        opts.hasOffset = true;
        if(!parse_size_arg(args[++i], opts.offset)){
          g_parse_error_cmd = "fs.read";
          return detail::text_result("fs.read: invalid --offset value\n", 1);
        }
      }else if(tok == "--length"){
        if(i + 1 >= args.size()){
          g_parse_error_cmd = "fs.read";
          return detail::text_result("fs.read: missing value for --length\n", 1);
        }
        opts.hasLength = true;
        if(!parse_size_arg(args[++i], opts.length)){
          g_parse_error_cmd = "fs.read";
          return detail::text_result("fs.read: invalid --length value\n", 1);
        }
      }else if(tok == "--with-line-numbers"){
        opts.withLineNumbers = true;
      }else if(tok == "--hash-only"){
        opts.hashOnly = true;
      }else{
        g_parse_error_cmd = "fs.read";
        return detail::text_result("fs.read: unknown option " + tok + "\n", 1);
      }
    }
    if(opts.hasHead && opts.hasTail){
      g_parse_error_cmd = "fs.read";
      return detail::text_result("fs.read: --head and --tail are mutually exclusive\n", 1);
    }
    if(opts.encoding != "utf-8" && opts.encoding != "utf8"){
      g_parse_error_cmd = "fs.read";
      return detail::text_result("fs.read: only utf-8 encoding is supported\n", 1);
    }
    auto execResult = fs_read_execute(opts, cfg);
    ToolExecutionResult out;
    out.exitCode = execResult.exitCode;
    if(execResult.exitCode != 0){
      g_parse_error_cmd = "fs.read";
      out.output = execResult.errorMessage + "\n";
      sj::Object meta;
      meta.emplace("error", sj::Value(execResult.errorCode));
      meta.emplace("message", sj::Value(execResult.errorMessage));
      meta.emplace("duration_ms", sj::Value(static_cast<long long>(execResult.durationMs)));
      out.metaJson = sj::dump(sj::Value(std::move(meta)));
      return out;
    }
    out.output = execResult.content;
    sj::Object meta;
    meta.emplace("truncated", sj::Value(execResult.truncated));
    meta.emplace("bytes_total", sj::Value(static_cast<long long>(execResult.bytesTotal)));
    meta.emplace("bytes_returned", sj::Value(static_cast<long long>(execResult.bytesReturned)));
    sj::Object range;
    range.emplace("offset", sj::Value(static_cast<long long>(execResult.rangeOffset)));
    range.emplace("length", sj::Value(static_cast<long long>(execResult.rangeLength)));
    meta.emplace("range", sj::Value(std::move(range)));
    meta.emplace("hash", sj::Value(execResult.hash));
    meta.emplace("duration_ms", sj::Value(static_cast<long long>(execResult.durationMs)));
    out.metaJson = sj::dump(sj::Value(std::move(meta)));
    return out;
  }
};

inline ToolDefinition make_fs_read_tool(){
  ToolDefinition def;
  def.ui = FsRead::ui();
  def.executor = FsRead::run;
  return def;
}

} // namespace tool

