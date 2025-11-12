#pragma once

#include "../tool_common.hpp"
#include "fs_common.hpp"

#include <filesystem>
#include <chrono>
#include <sstream>
#include <set>
#include <vector>
#include <functional>
#include <system_error>
#include <string>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace tool {

struct FsTreeOptions {
  std::filesystem::path root;
  size_t depth = 3;
  bool includeHidden = false;
  bool followSymlinks = false;
  std::vector<std::filesystem::path> ignoreFiles;
  std::set<std::string> extensions;
  std::string format = "json";
  size_t maxEntries = 1024;
};

struct FsTreeNode {
  std::string path;
  std::string type;
  uintmax_t size = 0;
  std::string ext;
  std::time_t mtime = 0;
  std::vector<FsTreeNode> children;
};

struct FsTreeResult {
  int exitCode = 0;
  FsTreeNode root;
  bool truncated = false;
  size_t entries = 0;
  std::string errorCode;
  std::string errorMessage;
  uint64_t durationMs = 0;
};

struct IgnoreRule {
  std::string pattern;
  bool prefix = false;
};

inline std::vector<IgnoreRule> load_ignore_rules(const std::vector<std::filesystem::path>& files){
  std::vector<IgnoreRule> rules;
  for(const auto& path : files){
    std::error_code ec;
    std::filesystem::path resolved = agent_realpath(path, ec);
    if(ec) continue;
    std::ifstream ifs(resolved);
    if(!ifs) continue;
    std::string line;
    while(std::getline(ifs, line)){
      if(!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
      std::string trimmed;
      size_t start = 0;
      while(start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) ++start;
      size_t end = line.size();
      while(end > start && std::isspace(static_cast<unsigned char>(line[end-1]))) --end;
      trimmed = line.substr(start, end - start);
      if(trimmed.empty()) continue;
      if(trimmed[0] == '#') continue;
      IgnoreRule rule;
      if(!trimmed.empty() && trimmed.back() == '/'){
        rule.prefix = true;
        trimmed.pop_back();
      }
      rule.pattern = trimmed;
      rules.push_back(rule);
    }
  }
  return rules;
}

inline bool path_hidden(const std::filesystem::path& p){
  for(const auto& part : p){
    std::string name = part.string();
    if(!name.empty() && name[0] == '.') return true;
  }
  return false;
}

inline std::string relative_path_string(const std::filesystem::path& root, const std::filesystem::path& current){
  std::error_code ec;
  auto rel = std::filesystem::relative(current, root, ec);
  if(ec) return current.filename().string();
  std::string str = rel.generic_string();
  if(str.empty()) return ".";
  return str;
}

inline bool matches_extension(const std::set<std::string>& extFilter, const std::filesystem::path& path){
  if(extFilter.empty()) return true;
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch){ return static_cast<char>(std::tolower(ch)); });
  return extFilter.count(ext) > 0;
}

inline bool should_ignore(const std::vector<IgnoreRule>& rules, const std::string& relPath){
  for(const auto& rule : rules){
    if(rule.prefix){
      if(relPath == rule.pattern || startsWith(relPath, rule.pattern + "/")) return true;
    }else{
      if(relPath == rule.pattern) return true;
    }
  }
  return false;
}

inline uintmax_t entry_size(const std::filesystem::directory_entry& entry){
  std::error_code ec;
  auto size = entry.file_size(ec);
  if(ec) return 0;
  return size;
}

inline std::time_t entry_mtime(const std::filesystem::directory_entry& entry){
  std::error_code ec;
  auto ftime = entry.last_write_time(ec);
  if(ec) return 0;
  auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
  return std::chrono::system_clock::to_time_t(sctp);
}

inline FsTreeResult fs_tree_execute(const FsTreeOptions& opts, const AgentFsConfig& cfg){
  auto start = std::chrono::steady_clock::now();
  FsTreeResult result;
  std::error_code ec;
  auto resolved = agent_realpath(opts.root, ec);
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
    result.errorMessage = "root does not exist";
    return result;
  }
  if(!std::filesystem::is_directory(resolved)){
    result.exitCode = 1;
    result.errorCode = "validation";
    result.errorMessage = "root is not a directory";
    return result;
  }
  auto rules = load_ignore_rules(opts.ignoreFiles);
  result.root.path = ".";
  result.root.type = "dir";
  result.root.size = 0;
  result.root.mtime = entry_mtime(std::filesystem::directory_entry(resolved));

  std::function<void(const std::filesystem::path&, FsTreeNode&, size_t)> walk;
  walk = [&](const std::filesystem::path& current, FsTreeNode& node, size_t depth){
    if(depth == 0) return;
    std::error_code itEc;
    std::filesystem::directory_iterator it(current, itEc);
    if(itEc){
      return;
    }
    for(const auto& entry : it){
      if(result.entries >= opts.maxEntries){
        result.truncated = true;
        break;
      }
      auto rel = relative_path_string(resolved, entry.path());
      if(!opts.includeHidden && path_hidden(entry.path().lexically_relative(resolved))){
        continue;
      }
      if(should_ignore(rules, rel)) continue;
      std::error_code statusEc;
      bool isDir = entry.is_directory(statusEc);
      if(statusEc) isDir = false;
      bool isSymlink = entry.is_symlink(statusEc);
      if(statusEc) isSymlink = false;
      if(isSymlink && !opts.followSymlinks) isDir = false;
      if(!isDir && !matches_extension(opts.extensions, entry.path())) continue;
      FsTreeNode child;
      child.path = rel;
      if(isDir){
        child.type = "dir";
      }else if(isSymlink){
        child.type = "symlink";
      }else{
        child.type = "file";
      }
      child.size = entry_size(entry);
      child.ext = entry.path().extension().string();
      std::transform(child.ext.begin(), child.ext.end(), child.ext.begin(), [](unsigned char ch){ return static_cast<char>(std::tolower(ch)); });
      child.mtime = entry_mtime(entry);
      ++result.entries;
      if(isDir && depth > 1){
        walk(entry.path(), child, depth - 1);
      }
      node.children.push_back(std::move(child));
    }
  };

  walk(resolved, result.root, opts.depth);

  auto end = std::chrono::steady_clock::now();
  result.durationMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
  return result;
}

inline std::string render_tree_text(const FsTreeNode& node, const std::filesystem::path& base, int indent = 0){
  std::ostringstream oss;
  std::string indentStr(indent, ' ');
  if(indent == 0){
    oss << base << "\n";
  }
  for(const auto& child : node.children){
    oss << indentStr << "- " << child.path << " (" << child.type << ")\n";
    if(!child.children.empty()){
      oss << render_tree_text(child, base, indent + 2);
    }
  }
  return oss.str();
}

struct FsTree {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "fs.tree";
    spec.summary = "List directory tree in sandbox";
    spec.hidden = true;
    spec.requiresExplicitExpose = true;
    set_tool_summary_locale(spec, "en", "List directory tree in sandbox");
    set_tool_summary_locale(spec, "zh", "列出沙盒目录树");
    set_tool_help_locale(spec, "en", "fs.tree <root> [--depth N] [--include-hidden] [--follow-symlinks] [--ignore-file PATH] [--ext .py,.md] [--format json|text] [--max-entries N]");
    set_tool_help_locale(spec, "zh", "fs.tree <根目录> [--depth N] [--include-hidden] [--follow-symlinks] [--ignore-file 路径] [--ext .py,.md] [--format json|text] [--max-entries N]");
    spec.positional = {tool::positional("<root>", true, PathKind::Dir, {}, true)};
    spec.options = {
      OptionSpec{"--depth", true, {}, nullptr, false, "<levels>"},
      OptionSpec{"--include-hidden", false},
      OptionSpec{"--follow-symlinks", false},
      OptionSpec{"--ignore-file", true, {}, nullptr, false, "<path>", true, PathKind::File, false, {}},
      OptionSpec{"--ext", true, {}, nullptr, false, "<exts>"},
      OptionSpec{"--format", true, {"json", "text"}, nullptr, false, "<format>"},
      OptionSpec{"--max-entries", true, {}, nullptr, false, "<count>"}
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    if(request.tokens.size() < 2){
      g_parse_error_cmd = "fs.tree";
      return detail::text_result("usage: fs.tree <root> [options]\n", 1);
    }
    FsTreeOptions opts;
    auto cfg = default_agent_fs_config();
    opts.maxEntries = cfg.maxTreeEntries;
    opts.root = request.tokens[1];
    for(size_t i = 2; i < request.tokens.size(); ++i){
      const std::string& tok = request.tokens[i];
      if(tok == "--depth"){
        if(i + 1 >= request.tokens.size()){
          g_parse_error_cmd = "fs.tree";
          return detail::text_result("fs.tree: missing value for --depth\n", 1);
        }
        size_t value = 0;
        if(!parse_size_arg(request.tokens[++i], value)){
          g_parse_error_cmd = "fs.tree";
          return detail::text_result("fs.tree: invalid depth\n", 1);
        }
        opts.depth = value;
      }else if(tok == "--include-hidden"){
        opts.includeHidden = true;
      }else if(tok == "--follow-symlinks"){
        opts.followSymlinks = true;
      }else if(tok == "--ignore-file"){
        if(i + 1 >= request.tokens.size()){
          g_parse_error_cmd = "fs.tree";
          return detail::text_result("fs.tree: missing value for --ignore-file\n", 1);
        }
        opts.ignoreFiles.push_back(request.tokens[++i]);
      }else if(tok == "--ext"){
        if(i + 1 >= request.tokens.size()){
          g_parse_error_cmd = "fs.tree";
          return detail::text_result("fs.tree: missing value for --ext\n", 1);
        }
        std::string exts = request.tokens[++i];
        std::stringstream ss(exts);
        std::string ext;
        while(std::getline(ss, ext, ',')){
          if(ext.empty()) continue;
          if(ext.front() != '.') ext.insert(ext.begin(), '.');
          std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch){ return static_cast<char>(std::tolower(ch)); });
          opts.extensions.insert(ext);
        }
      }else if(tok == "--format"){
        if(i + 1 >= request.tokens.size()){
          g_parse_error_cmd = "fs.tree";
          return detail::text_result("fs.tree: missing value for --format\n", 1);
        }
        opts.format = request.tokens[++i];
      }else if(tok == "--max-entries"){
        if(i + 1 >= request.tokens.size()){
          g_parse_error_cmd = "fs.tree";
          return detail::text_result("fs.tree: missing value for --max-entries\n", 1);
        }
        size_t value = 0;
        if(!parse_size_arg(request.tokens[++i], value)){
          g_parse_error_cmd = "fs.tree";
          return detail::text_result("fs.tree: invalid max entries\n", 1);
        }
        opts.maxEntries = std::min(value, cfg.maxTreeEntries);
      }else{
        g_parse_error_cmd = "fs.tree";
        return detail::text_result("fs.tree: unknown option " + tok + "\n", 1);
      }
    }
    if(opts.format != "json" && opts.format != "text"){
      g_parse_error_cmd = "fs.tree";
      return detail::text_result("fs.tree: --format must be json or text\n", 1);
    }
    if(opts.maxEntries == 0) opts.maxEntries = 1;
    if(opts.depth == 0) opts.depth = 1;

    auto exec = fs_tree_execute(opts, cfg);
    ToolExecutionResult out;
    out.exitCode = exec.exitCode;
    if(exec.exitCode != 0){
      g_parse_error_cmd = "fs.tree";
      out.output = exec.errorMessage + "\n";
      sj::Object meta;
      meta.emplace("error", sj::Value(exec.errorCode));
      meta.emplace("message", sj::Value(exec.errorMessage));
      meta.emplace("duration_ms", sj::Value(static_cast<long long>(exec.durationMs)));
      out.metaJson = sj::dump(sj::Value(std::move(meta)));
      return out;
    }

    if(opts.format == "text"){
      out.output = render_tree_text(exec.root, opts.root);
    }else{
      std::function<sj::Value(const FsTreeNode&)> toJson;
      toJson = [&](const FsTreeNode& node){
        sj::Object obj;
        obj.emplace("path", sj::Value(node.path));
        obj.emplace("type", sj::Value(node.type));
        obj.emplace("size", sj::Value(static_cast<long long>(node.size)));
        obj.emplace("ext", sj::Value(node.ext));
        obj.emplace("mtime", sj::Value(static_cast<long long>(node.mtime)));
        sj::Array childArray;
        for(const auto& child : node.children){
          childArray.push_back(toJson(child));
        }
        obj.emplace("children", sj::Value(std::move(childArray)));
        return sj::Value(std::move(obj));
      };
      sj::Object rootObj;
      rootObj.emplace("nodes", sj::Value(sj::Array{toJson(exec.root)}));
      sj::Object meta;
      meta.emplace("truncated", sj::Value(exec.truncated));
      meta.emplace("entries", sj::Value(static_cast<long long>(exec.entries)));
      meta.emplace("duration_ms", sj::Value(static_cast<long long>(exec.durationMs)));
      rootObj.emplace("meta", sj::Value(std::move(meta)));
      out.output = sj::dump(sj::Value(std::move(rootObj)), 2);
    }

    sj::Object meta;
    meta.emplace("truncated", sj::Value(exec.truncated));
    meta.emplace("entries", sj::Value(static_cast<long long>(exec.entries)));
    meta.emplace("duration_ms", sj::Value(static_cast<long long>(exec.durationMs)));
    out.metaJson = sj::dump(sj::Value(std::move(meta)));
    return out;
  }
};

inline ToolDefinition make_fs_tree_tool(){
  ToolDefinition def;
  def.ui = FsTree::ui();
  def.executor = FsTree::run;
  return def;
}

} // namespace tool

