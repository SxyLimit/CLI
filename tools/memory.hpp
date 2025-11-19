#pragma once

#include "tool_common.hpp"
#include "../utils/memory.hpp"
#include "../utils/json.hpp"

#include <chrono>
#include <random>
#include <sstream>
#include <map>
#include <fstream>
#include <cstdio>
#include <functional>
#include <iostream>
#include <optional>

namespace tool {

struct MemoryTool {
  struct Paths {
    std::filesystem::path root;
    std::filesystem::path index;
    std::filesystem::path personal;
  };

  static ToolSpec ui();
  static ToolExecutionResult run(const ToolExecutionRequest& request);

private:
  static Paths resolve_paths();
  static bool ensure_enabled(std::string& error);
  static bool load_index(const Paths& paths, MemoryIndex& index, std::string& error);
  static ToolExecutionResult run_init(const ToolExecutionRequest& request, const Paths& paths);
  static ToolExecutionResult run_import(const ToolExecutionRequest& request, const Paths& paths, const std::vector<std::string>& args);
  static ToolExecutionResult run_list(const Paths& paths, const std::vector<std::string>& args);
  static ToolExecutionResult run_show(const Paths& paths, const std::vector<std::string>& args);
  static ToolExecutionResult run_search(const Paths& paths, const std::vector<std::string>& args);
  static ToolExecutionResult run_stats(const Paths& paths);
  static ToolExecutionResult run_note(const ToolExecutionRequest& request, const Paths& paths, const std::vector<std::string>& args);
  static ToolExecutionResult run_query(const ToolExecutionRequest& request, const Paths& paths, const std::vector<std::string>& args);
  static ToolExecutionResult run_index_builder(const ToolExecutionRequest& request, const Paths& paths, const std::vector<std::string>& extraArgs = {});
  static ToolExecutionResult run_python_cli(const ToolExecutionRequest& request,
                                            const Paths& paths,
                                            const std::string& subcommand,
                                            const std::vector<std::string>& passArgs,
                                            const std::optional<std::string>& langOverride = std::nullopt);
  static std::string escape_json(const std::string& value);
  static std::optional<std::string> normalize_path_arg(const Paths& paths, const std::string& raw);
  static std::filesystem::path make_report_path(const Paths& paths);
  static std::optional<sj::Value> load_report_json(const std::filesystem::path& path);
};

inline ToolSpec MemoryTool::ui(){
  ToolSpec spec;
  spec.name = "memory";
  spec.summary = "Manage and query the Memory system";
  set_tool_summary_locale(spec, "zh", "管理和查询 Memory 系统");
  spec.help = "memory init | memory import <path> | memory list [path] | memory show <path> | memory search <keywords...> | memory stats | memory note <text| -e> | memory query <question>";
  set_tool_help_locale(spec, "zh", "memory init | memory import <路径> | memory list [路径] | memory show <路径> | memory search <关键字...> | memory stats | memory note <文本 或 -e> | memory query <问题>");
  spec.subs = {
    SubcommandSpec{"init", {}, {}, {}, nullptr},
    SubcommandSpec{
      "import",
      {
        OptionSpec{"--personal", false},
        OptionSpec{"--category", true, {}, nullptr, false, "<name>"},
        OptionSpec{"--mode", true, {"copy", "link", "mirror"}, nullptr, false, "<mode>"},
        OptionSpec{"--lang", true, {}, nullptr, false, "<code>"},
        OptionSpec{"--force", false}
      },
      {tool::positional("<path>", true, PathKind::Any)},
      {},
      nullptr
    },
    SubcommandSpec{
      "list",
      {
        OptionSpec{"-d", true, {}, nullptr, false, "<depth>"},
        OptionSpec{"--personal-only", false},
        OptionSpec{"--knowledge-only", false}
      },
      {tool::positional("[<path>]")},
      {{"bucket", {"--personal-only", "--knowledge-only"}}},
      nullptr
    },
    SubcommandSpec{
      "show",
      {
        OptionSpec{"--content", false},
        OptionSpec{"--max-bytes", true, {}, nullptr, false, "<bytes>"}
      },
      {tool::positional("<path>")},
      {},
      nullptr
    },
    SubcommandSpec{
      "search",
      {
        OptionSpec{"--scope", true, {"all", "personal", "knowledge"}, nullptr, false, "<scope>"},
        OptionSpec{"--limit", true, {}, nullptr, false, "<N>"},
        OptionSpec{"--in", true, {"summary", "content", "both"}, nullptr, false, "<mode>"}
      },
      {tool::positional("<keywords...>")},
      {},
      nullptr
    },
    SubcommandSpec{"stats", {}, {}, {}, nullptr},
    SubcommandSpec{
      "note",
      {
        OptionSpec{"-e", false},
        OptionSpec{"--editor", false},
        OptionSpec{"--lang", true, {}, nullptr, false, "<code>"}
      },
      {tool::positional("[<text...>]")},
      {},
      nullptr
    },
    SubcommandSpec{
      "query",
      {
        OptionSpec{"--scope", true, {"auto", "personal", "knowledge"}, nullptr, false, "<scope>"},
        OptionSpec{"--limit", true, {}, nullptr, false, "<N>"},
        OptionSpec{"--max-bytes", true, {}, nullptr, false, "<bytes>"}
      },
      {tool::positional("<question...>")},
      {},
      nullptr
    },
  };
  return spec;
}

inline MemoryTool::Paths MemoryTool::resolve_paths(){
  Paths p;
  p.root = memory_root_path();
  p.index = memory_index_path();
  p.personal = memory_personal_path();
  return p;
}

inline bool MemoryTool::ensure_enabled(std::string& error){
  if(!memory_system_enabled()){
    error = "memory system disabled via memory.enabled=false";
    return false;
  }
  return true;
}

inline bool MemoryTool::load_index(const Paths& paths, MemoryIndex& index, std::string& error){
  std::string err;
  if(!std::filesystem::exists(paths.index)){
    error = "memory index not found. Run `memory init` first.";
    return false;
  }
  if(!index.load(paths.index, err)){
    error = err.empty()? "failed to load memory index" : err;
    return false;
  }
  return true;
}

inline ToolExecutionResult MemoryTool::run(const ToolExecutionRequest& request){
  const auto& args = request.tokens;
  if(args.size() < 2){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("usage: memory <init|import|list|show|search|stats|note|query>\n", 1);
  }
  Paths paths = resolve_paths();
  const std::string sub = args[1];
  if(sub == "init") return run_init(request, paths);
  if(sub == "import") return run_import(request, paths, args);
  if(sub == "list") return run_list(paths, args);
  if(sub == "show") return run_show(paths, args);
  if(sub == "search") return run_search(paths, args);
  if(sub == "stats") return run_stats(paths);
  if(sub == "note") return run_note(request, paths, args);
  if(sub == "query") return run_query(request, paths, args);
  g_parse_error_cmd = "memory";
  return tool::detail::text_result("usage: memory <init|import|list|show|search|stats|note|query>\n", 1);
}

inline ToolExecutionResult MemoryTool::run_init(const ToolExecutionRequest& request, const Paths& paths){
  std::error_code ec;
  std::filesystem::create_directories(paths.root, ec);
  ec.clear();
  std::filesystem::create_directories(paths.personal, ec);
  std::filesystem::create_directories(paths.root / "knowledge", ec);
  auto result = run_index_builder(request, paths, {"--force"});
  if(result.exitCode != 0){
    g_parse_error_cmd = "memory";
    return result;
  }
  std::ostringstream oss;
  oss << "Memory initialized at " << paths.root << "\n";
  oss << "Index file: " << paths.index << "\n";
  return tool::detail::text_result(oss.str());
}

inline ToolExecutionResult MemoryTool::run_python_cli(const ToolExecutionRequest& request,
                                                      const Paths& paths,
                                                      const std::string& subcommand,
                                                      const std::vector<std::string>& passArgs,
                                                      const std::optional<std::string>& langOverride){
  std::string cmd = "python3 tools/memory_cli.py";
  cmd += " --root " + shellEscape(paths.root.string());
  cmd += " --index " + shellEscape(paths.index.string());
  cmd += " --personal-subdir " + shellEscape(g_settings.memory.personalSubdir);
  std::string lang = langOverride.has_value()? *langOverride : memory_summary_language();
  cmd += " --lang " + shellEscape(lang);
  cmd += " --min-len " + std::to_string(g_settings.memory.summaryMinLen);
  cmd += " --max-len " + std::to_string(g_settings.memory.summaryMaxLen);
  cmd += " --bootstrap-depth " + std::to_string(g_settings.memory.maxBootstrapDepth);
  cmd += " " + shellEscape(subcommand);
  for(const auto& token : passArgs){
    cmd += " ";
    cmd += shellEscape(token);
  }
  return tool::detail::execute_shell(request, cmd);
}

inline std::filesystem::path MemoryTool::make_report_path(const Paths& paths){
  std::filesystem::path base;
  try{
    base = std::filesystem::temp_directory_path();
  }catch(...){
    base = paths.root;
  }
  auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  for(int i = 0; i < 5; ++i){
    auto candidate = base / ("memory_report_" + std::to_string(stamp + i) + ".json");
    if(!std::filesystem::exists(candidate)){
      return candidate;
    }
  }
  return base / ("memory_report_" + std::to_string(stamp) + ".json");
}

inline std::optional<sj::Value> MemoryTool::load_report_json(const std::filesystem::path& path){
  if(path.empty()) return std::nullopt;
  std::ifstream input(path);
  if(!input) return std::nullopt;
  std::ostringstream oss;
  oss << input.rdbuf();
  auto text = oss.str();
  if(text.empty()) return std::nullopt;
  try{
    sj::Parser parser(text);
    return parser.parse();
  }catch(...){
    return std::nullopt;
  }
}

inline ToolExecutionResult MemoryTool::run_import(const ToolExecutionRequest& request, const Paths& paths, const std::vector<std::string>& args){
  std::string err;
  if(!ensure_enabled(err)){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory import: " + err + "\n", 1);
  }
  if(args.size() < 3){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("usage: memory import <path> [options]\n", 1);
  }
  std::vector<std::string> passArgs;
  std::optional<std::string> langOverride;
  bool literal = false;
  for(size_t i = 2; i < args.size(); ++i){
    const std::string& tok = args[i];
    if(!literal && tok == "--"){
      literal = true;
      continue;
    }
    if(!literal && tok == "--lang"){
      if(i + 1 >= args.size()){
        g_parse_error_cmd = "memory";
        return tool::detail::text_result("memory import: --lang requires a value\n", 1);
      }
      langOverride = args[++i];
      continue;
    }
    passArgs.push_back(tok);
  }
  if(passArgs.empty()){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("usage: memory import <path> [options]\n", 1);
  }
  auto reportPath = make_report_path(paths);
  passArgs.push_back("--report");
  passArgs.push_back(reportPath.string());
  auto execResult = run_python_cli(request, paths, "import", passArgs, langOverride);
  auto report = load_report_json(reportPath);
  std::error_code ec;
  std::filesystem::remove(reportPath, ec);
  if(execResult.exitCode != 0){
    std::string message = "memory import failed";
    if(report && report->isObject()){
      const auto* status = report->find("status");
      if(status && status->isString() && status->asString() == "error"){
        if(const auto* err = report->find("error"); err && err->isString()){
          message += ": " + err->asString();
        }
      }
    }
    message += "\n";
    int exitCode = execResult.exitCode == 0 ? 1 : execResult.exitCode;
    return tool::detail::text_result(message, exitCode);
  }
  long long imported = 0;
  std::string target;
  bool personal = false;
  if(report && report->isObject()){
    const auto* status = report->find("status");
    if(status && status->isString() && status->asString() == "ok"){
      if(const auto* countVal = report->find("imported"); countVal){
        imported = countVal->asInteger(0);
      }
      if(const auto* targetVal = report->find("target"); targetVal && targetVal->isString()){
        target = targetVal->asString();
      }
      if(const auto* personalVal = report->find("personal"); personalVal){
        personal = personalVal->asBool(false);
      }
    }
  }
  std::ostringstream oss;
  oss << "[memory] imported " << imported << (imported == 1 ? " file" : " files");
  if(!target.empty()){
    oss << " into " << target;
  }
  if(personal){
    oss << " (personal)";
  }
  oss << "\n";
  return tool::detail::text_result(oss.str());
}

inline ToolExecutionResult MemoryTool::run_note(const ToolExecutionRequest& request, const Paths& paths, const std::vector<std::string>& args){
  std::string err;
  if(!ensure_enabled(err)){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory note: " + err + "\n", 1);
  }
  std::vector<std::string> passArgs;
  std::optional<std::string> langOverride;
  bool hasEditor = false;
  bool hasText = false;
  bool literal = false;
  for(size_t i = 2; i < args.size(); ++i){
    const std::string& tok = args[i];
    if(!literal && tok == "--"){
      literal = true;
      continue;
    }
    if(!literal && tok == "--lang"){
      if(i + 1 >= args.size()){
        g_parse_error_cmd = "memory";
        return tool::detail::text_result("memory note: --lang requires a value\n", 1);
      }
      langOverride = args[++i];
      continue;
    }
    if(tok == "-e" || tok == "--editor"){
      hasEditor = true;
    }else if(!tok.empty()){
      hasText = true;
    }
    passArgs.push_back(tok);
  }
  if(!hasEditor && !hasText){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("usage: memory note <text> | memory note -e\n", 1);
  }
  auto reportPath = make_report_path(paths);
  passArgs.push_back("--report");
  passArgs.push_back(reportPath.string());
  auto execResult = run_python_cli(request, paths, "note", passArgs, langOverride);
  auto report = load_report_json(reportPath);
  std::error_code ec;
  std::filesystem::remove(reportPath, ec);
  if(execResult.exitCode != 0){
    std::string message = "memory note failed";
    if(report && report->isObject()){
      const auto* status = report->find("status");
      if(status && status->isString() && status->asString() == "error"){
        if(const auto* err = report->find("error"); err && err->isString()){
          message += ": " + err->asString();
        }
      }
    }
    message += "\n";
    int exitCode = execResult.exitCode == 0 ? 1 : execResult.exitCode;
    return tool::detail::text_result(message, exitCode);
  }
  std::string notePath;
  if(report && report->isObject()){
    const auto* status = report->find("status");
    if(status && status->isString() && status->asString() == "ok"){
      if(const auto* pathVal = report->find("path"); pathVal && pathVal->isString()){
        notePath = pathVal->asString();
      }
    }
  }
  if(notePath.empty()){
    notePath = "personal/notes";
  }
  std::string message = "[memory] created personal note " + notePath + "\n";
  return tool::detail::text_result(message);
}

inline ToolExecutionResult MemoryTool::run_list(const Paths& paths, const std::vector<std::string>& args){
  std::string err;
  if(!ensure_enabled(err)){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory list: " + err + "\n", 1);
  }
  MemoryIndex index;
  if(!load_index(paths, index, err)){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory list: " + err + "\n", 1);
  }
  std::string rawPath;
  int depthLimit = 1;
  bool personalOnly = false;
  bool knowledgeOnly = false;
  for(size_t i = 2; i < args.size(); ++i){
    if(args[i] == "-d" && i + 1 < args.size()){
      try{
        depthLimit = std::stoi(args[++i]);
        if(depthLimit < 1) depthLimit = 1;
      }catch(...){ depthLimit = 1; }
    }else if(args[i] == "--personal-only"){
      personalOnly = true;
    }else if(args[i] == "--knowledge-only"){
      knowledgeOnly = true;
    }else if(rawPath.empty()){
      rawPath = args[i];
    }else{
      g_parse_error_cmd = "memory";
      return tool::detail::text_result("memory list: too many positional arguments\n", 1);
    }
  }
  if(personalOnly && knowledgeOnly){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory list: --personal-only and --knowledge-only cannot be combined\n", 1);
  }
  auto allow_bucket = [&](const MemoryNode& node){
    if(personalOnly) return node.bucket == "personal";
    if(knowledgeOnly) return node.bucket == "knowledge";
    return true;
  };
  auto block_subtree = [&](const MemoryNode& node){
    if(node.bucket == "other") return false;
    if(personalOnly) return node.bucket != "personal";
    if(knowledgeOnly) return node.bucket != "knowledge";
    return false;
  };
  std::string normalizedPath;
  if(!rawPath.empty()){
    auto normalized = normalize_path_arg(paths, rawPath);
    if(!normalized.has_value()){
      g_parse_error_cmd = "memory";
      return tool::detail::text_result("memory list: invalid path (outside memory root)\n", 1);
    }
    normalizedPath = *normalized;
  }
  std::ostringstream oss;
  auto render_node = [&](const MemoryNode& node, int indent){
    if(!allow_bucket(node) && !(node.depth == 0 && normalizedPath.empty())) return;
    std::string pad(indent * 2, ' ');
    bool isRoot = node.relPath.empty();
    std::string display = isRoot? std::string("/") : node.relPath;
    if(node.isDir() && !isRoot) display += "/";
    if(node.isDir()){
      oss << pad << "[DIR] " << display << "\n";
    }else{
      oss << pad << display << "\n";
    }
    if(!node.summary.empty()){
      oss << pad << "      —— " << node.summary << "\n";
    }
  };
  if(normalizedPath.empty()){
    if(const MemoryNode* rootNode = index.find("")){
      if(!rootNode->summary.empty()){
        oss << "[ROOT] Memory 根\n      —— " << rootNode->summary << "\n\n";
      }
    }
    auto roots = index.nodesByDepth(1);
    bool printed = false;
    for(const MemoryNode* dir : roots){
      if(!dir || !dir->isDir()) continue;
      if(!allow_bucket(*dir)) continue;
      render_node(*dir, 0);
      auto children = index.childrenOf(dir->relPath);
      for(const MemoryNode* child : children){
        if(!child || !child->isFile()) continue;
        if(!allow_bucket(*child)) continue;
        render_node(*child, 1);
      }
      oss << "\n";
      printed = true;
    }
    if(!printed){
      oss << "[memory] no nodes match the selected filters\n";
    }
  }else{
    const MemoryNode* node = index.find(normalizedPath);
    if(!node){
      g_parse_error_cmd = "memory";
      return tool::detail::text_result("memory list: path not found\n", 1);
    }
    if(node->isDir() && block_subtree(*node)){
      g_parse_error_cmd = "memory";
      return tool::detail::text_result("memory list: path filtered out by bucket selection\n", 1);
    }
    if(!allow_bucket(*node)){
      g_parse_error_cmd = "memory";
      return tool::detail::text_result("memory list: path filtered out by bucket selection\n", 1);
    }
    std::function<void(const MemoryNode&, int, int)> walk;
    walk = [&](const MemoryNode& current, int depth, int maxDepth){
      render_node(current, depth);
      if(!current.isDir()) return;
      if(depth >= maxDepth) return;
      if(block_subtree(current) && depth > 0) return;
      auto children = index.childrenOf(current.relPath);
      for(const MemoryNode* child : children){
        if(!child) continue;
        if(child->isDir() && block_subtree(*child)) continue;
        if(!allow_bucket(*child) && !child->isDir()) continue;
        walk(*child, depth + 1, maxDepth);
      }
    };
    walk(*node, 0, node->isDir()? depthLimit : 0);
  }
  std::string rendered = oss.str();
  if(rendered.empty()){
    rendered = "[memory] no nodes available\n";
  }
  return tool::detail::text_result(rendered);
}

inline ToolExecutionResult MemoryTool::run_show(const Paths& paths, const std::vector<std::string>& args){
  std::string err;
  if(!ensure_enabled(err)){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory show: " + err + "\n", 1);
  }
  if(args.size() < 3){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("usage: memory show <path> [--content] [--max-bytes N]\n", 1);
  }
  bool includeContent = false;
  size_t maxBytes = 8192;
  std::string targetPath;
  for(size_t i = 2; i < args.size(); ++i){
    if(args[i] == "--content"){
      includeContent = true;
    }else if(args[i] == "--max-bytes" && i + 1 < args.size()){
      try{ maxBytes = static_cast<size_t>(std::stoul(args[++i])); }catch(...){ maxBytes = 8192; }
    }else if(targetPath.empty()){
      targetPath = args[i];
    }else{
      g_parse_error_cmd = "memory";
      return tool::detail::text_result("memory show: too many positional arguments\n", 1);
    }
  }
  if(targetPath.empty()){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("usage: memory show <path> [--content]\n", 1);
  }
  auto normalized = normalize_path_arg(paths, targetPath);
  if(!normalized.has_value()){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory show: invalid path (outside memory root)\n", 1);
  }
  MemoryIndex index;
  if(!load_index(paths, index, err)){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory show: " + err + "\n", 1);
  }
  const MemoryNode* node = index.find(*normalized);
  if(!node){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory show: path not found under " + paths.root.string() + "\n", 1);
  }
  std::ostringstream oss;
  oss << (node->relPath.empty()? std::string("/") : node->relPath);
  oss << (node->isDir()? " (dir)" : " (file)") << "\n";
  if(!node->title.empty()) oss << "title: " << node->title << "\n";
  oss << "bucket: " << (node->bucket.empty()? "other" : node->bucket);
  if(node->isPersonal) oss << " (personal)";
  oss << "\n";
  oss << "depth: " << node->depth << ", eager_expose=" << (node->eagerExpose? "true" : "false") << "\n";
  if(node->sizeBytes) oss << "size_bytes: " << node->sizeBytes << ", token_est: " << node->tokenEstimate << "\n";
  if(!node->summary.empty()) oss << node->summary << "\n";
  if(!node->tags.empty()) oss << "tags: " << join(node->tags, ", ") << "\n";
  if(!node->sourceImportPath.empty()){
    oss << "source: " << node->sourceImportPath;
    if(!node->sourceImportMode.empty()) oss << " (" << node->sourceImportMode << ")";
    oss << "\n";
  }
  if(!node->createdAt.empty()) oss << "created_at: " << node->createdAt << "\n";
  if(!node->updatedAt.empty()) oss << "updated_at: " << node->updatedAt << "\n";
  if(includeContent && !node->isFile()){
    oss << "[memory] --content is only valid for file nodes\n";
  }
  if(includeContent && node->isFile()){
    std::filesystem::path full = paths.root / node->relPath;
    std::ifstream in(full);
    if(!in.good()){
      oss << "[error] failed to read file content\n";
    }else{
      std::string buffer;
      buffer.resize(maxBytes);
      in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      buffer.resize(static_cast<size_t>(in.gcount()));
      oss << "-----\n" << buffer << (buffer.empty()? "" : "\n") << "-----\n";
    }
  }
  return tool::detail::text_result(oss.str());
}

inline ToolExecutionResult MemoryTool::run_search(const Paths& paths, const std::vector<std::string>& args){
  std::string err;
  if(!ensure_enabled(err)){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory search: " + err + "\n", 1);
  }
  MemoryIndex index;
  if(!load_index(paths, index, err)){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory search: " + err + "\n", 1);
  }
  std::string scope = "all";
  size_t limit = 10;
  std::string mode = "summary";
  std::vector<std::string> keywords;
  for(size_t i = 2; i < args.size(); ++i){
    if(args[i] == "--scope" && i + 1 < args.size()){
      scope = args[++i];
    }else if(args[i] == "--limit" && i + 1 < args.size()){
      try{ limit = static_cast<size_t>(std::stoul(args[++i])); }catch(...){ limit = 10; }
    }else if(args[i] == "--in" && i + 1 < args.size()){
      mode = args[++i];
    }else{
      keywords.push_back(args[i]);
    }
  }
  if(keywords.empty()){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("usage: memory search <keywords...> [--scope all|personal|knowledge] [--limit N] [--in summary|content|both]\n", 1);
  }
  std::string query = join(keywords, " ");
  MemorySearchScope searchScope = MemorySearchScope::All;
  if(scope == "personal") searchScope = MemorySearchScope::Personal;
  else if(scope == "knowledge") searchScope = MemorySearchScope::Knowledge;
  bool inSummary = true;
  bool inContent = false;
  if(mode == "content"){ inSummary = false; inContent = true; }
  else if(mode == "both"){ inSummary = true; inContent = true; }
  auto hits = index.search(query, searchScope, inSummary, inContent, limit, paths.root);
  if(hits.empty()){
    return tool::detail::text_result("[memory] no matching nodes\n");
  }
  std::ostringstream oss;
  for(const auto& hit : hits){
    if(!hit.node) continue;
    oss << hit.node->relPath << " (score=" << hit.score << ")\n";
    if(!hit.node->summary.empty()) oss << "  " << hit.node->summary << "\n";
  }
  return tool::detail::text_result(oss.str());
}

inline ToolExecutionResult MemoryTool::run_stats(const Paths& paths){
  std::string err;
  if(!ensure_enabled(err)){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory stats: " + err + "\n", 1);
  }
  MemoryIndex index;
  if(!load_index(paths, index, err)){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory stats: " + err + "\n", 1);
  }
  size_t files = 0, dirs = 0, personal = 0, knowledge = 0, other = 0;
  std::map<int, size_t> depthCounts;
  uint64_t tokens = 0;
  std::string latestUpdate;
  const auto& allNodes = index.nodes();
  for(const auto& node : allNodes){
    if(node.isDir()) ++dirs; else ++files;
    if(node.bucket == "personal") ++personal;
    else if(node.bucket == "knowledge") ++knowledge;
    else ++other;
    depthCounts[node.depth]++;
    tokens += node.tokenEstimate;
    if(!node.updatedAt.empty() && node.updatedAt > latestUpdate) latestUpdate = node.updatedAt;
  }
  std::ostringstream oss;
  oss << "Total nodes: " << allNodes.size() << " (dirs=" << dirs << ", files=" << files << ")\n";
  oss << "Buckets: personal=" << personal << ", knowledge=" << knowledge << ", other=" << other << "\n";
  oss << "Estimated tokens: " << tokens << "\n";
  oss << "Latest updated_at: " << (latestUpdate.empty()? "n/a" : latestUpdate) << "\n";
  oss << "Depth distribution:\n";
  if(depthCounts.empty()){
    oss << "  (no nodes indexed)\n";
  }else{
    for(const auto& kv : depthCounts){
      oss << "  depth " << kv.first << ": " << kv.second << "\n";
    }
  }
  return tool::detail::text_result(oss.str());
}

inline std::string MemoryTool::escape_json(const std::string& value){
  std::string out;
  out.reserve(value.size() + 8);
  for(char ch : value){
    switch(ch){
      case '\\': out += "\\\\"; break;
      case '\"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if(static_cast<unsigned char>(ch) < 0x20){
          char buf[7];
          std::snprintf(buf, sizeof(buf), "\\u%04x", ch & 0xFF);
          out += buf;
        }else{
          out.push_back(ch);
        }
        break;
    }
  }
  return out;
}

inline ToolExecutionResult MemoryTool::run_query(const ToolExecutionRequest& request,
                                                 const Paths& paths,
                                                 const std::vector<std::string>& args){
  std::string err;
  if(!ensure_enabled(err)){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory query: " + err + "\n", 1);
  }
  MemoryIndex index;
  if(!load_index(paths, index, err)){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("memory query: " + err + "\n", 1);
  }
  std::string scope = "auto";
  size_t limit = 5;
  size_t maxBytes = 8192;
  std::vector<std::string> questionParts;
  for(size_t i = 2; i < args.size(); ++i){
    if(args[i] == "--scope" && i + 1 < args.size()){
      scope = args[++i];
    }else if(args[i] == "--limit" && i + 1 < args.size()){
      try{ limit = static_cast<size_t>(std::stoul(args[++i])); }catch(...){ limit = 5; }
    }else if(args[i] == "--max-bytes" && i + 1 < args.size()){
      try{ maxBytes = static_cast<size_t>(std::stoul(args[++i])); }catch(...){ maxBytes = 8192; }
    }else{
      questionParts.push_back(args[i]);
    }
  }
  if(questionParts.empty()){
    g_parse_error_cmd = "memory";
    return tool::detail::text_result("usage: memory query <question> [--scope auto|personal|knowledge] [--limit N]\n", 1);
  }
  std::string question = join(questionParts, " ");
  auto contains_personal_hint = [&](const std::string& text){
    static const std::vector<std::string> hints{"我", "我的", "我们", "习惯", "偏好", "上次", "之前"};
    for(const auto& hint : hints){
      if(text.find(hint) != std::string::npos) return true;
    }
    return false;
  };
  MemorySearchScope searchScope = MemorySearchScope::All;
  bool autoMode = scope == "auto";
  if(scope == "personal") searchScope = MemorySearchScope::Personal;
  else if(scope == "knowledge") searchScope = MemorySearchScope::Knowledge;
  else if(scope == "auto" && contains_personal_hint(question)) searchScope = MemorySearchScope::Personal;
  auto hits = index.search(question, searchScope, true, false, std::max(limit, static_cast<size_t>(8)), paths.root);
  if(autoMode && searchScope == MemorySearchScope::Personal && hits.empty()){
    searchScope = MemorySearchScope::All;
    hits = index.search(question, searchScope, true, false, std::max(limit, static_cast<size_t>(8)), paths.root);
  }
  if(hits.empty()){
    return tool::detail::text_result("[memory] no related documents found\n");
  }
  if(hits.size() > limit) hits.resize(limit);
  std::ostringstream doc;
  doc << "{\n  \"question\": \"" << escape_json(question) << "\",\n  \"language\": \"" << escape_json(memory_summary_language()) << "\",\n  \"documents\": [\n";
  size_t personalHits = 0, knowledgeHits = 0;
  for(size_t i = 0; i < hits.size(); ++i){
    const MemoryNode* node = hits[i].node;
    if(!node) continue;
    if(node->bucket == "personal") ++personalHits;
    else if(node->bucket == "knowledge") ++knowledgeHits;
    doc << "    {\"path\": \"" << escape_json(node->relPath) << "\", \"summary\": \"" << escape_json(node->summary) << "\", \"is_personal\": " << (node->isPersonal? "true" : "false") << ",";
    if(node->isFile()){
      std::filesystem::path file = paths.root / node->relPath;
      std::ifstream in(file);
      std::string content;
      if(in.good()){
        content.resize(maxBytes);
        in.read(content.data(), static_cast<std::streamsize>(content.size()));
        content.resize(static_cast<size_t>(in.gcount()));
      }
      doc << " \"content\": \"" << escape_json(content) << "\"";
    }else{
      doc << " \"content\": \"\"";
    }
    doc << "}";
    if(i + 1 < hits.size()) doc << ",";
    doc << "\n";
  }
  doc << "  ]\n}\n";
  auto tmp = std::filesystem::temp_directory_path() / ("memory-query-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".json");
  {
    std::ofstream out(tmp);
    out << doc.str();
  }
  bool interactive = !request.silent;
  std::string startLine = interactive ? std::string(ansi::YELLOW) + "[Q]" + ansi::RESET + " (memory) 正在检索记忆并调用 LLM...\n" : "[Q] (memory) 正在检索记忆并调用 LLM...\n";
  std::string endLine = interactive ? std::string(ansi::RED) + "[Q]" + ansi::RESET + " (memory) 完成（命中 " + std::to_string(personalHits) + " 条 personal，" + std::to_string(knowledgeHits) + " 条 knowledge）。\n"
                                    : "[Q] (memory) 完成（命中 " + std::to_string(personalHits) + " 条 personal，" + std::to_string(knowledgeHits) + " 条 knowledge）。\n";
  std::string cmd = "python3 tools/memory_query.py --input " + shellEscape(tmp.string());
  ToolExecutionResult scriptResult;
  if(interactive){
    std::cout << startLine;
    std::cout.flush();
    scriptResult = tool::detail::execute_shell(request, cmd);
    std::cout << endLine;
    std::cout.flush();
  }else{
    scriptResult = tool::detail::execute_shell(request, cmd);
    ToolExecutionResult combined;
    combined.exitCode = scriptResult.exitCode;
    combined.output = startLine + scriptResult.output + endLine;
    combined.display = combined.output;
    scriptResult = combined;
  }
  std::error_code ec;
  std::filesystem::remove(tmp, ec);
  if(!interactive){
    return scriptResult;
  }
  ToolExecutionResult result;
  result.exitCode = scriptResult.exitCode;
  return result;
}

inline ToolExecutionResult MemoryTool::run_index_builder(const ToolExecutionRequest& request,
                                                         const Paths& paths,
                                                         const std::vector<std::string>& extraArgs){
  std::string cmd = "python3 tools/memory_build_index.py";
  cmd += " --root " + shellEscape(paths.root.string());
  cmd += " --index " + shellEscape(paths.index.string());
  cmd += " --personal-subdir " + shellEscape(g_settings.memory.personalSubdir);
  cmd += " --lang " + shellEscape(memory_summary_language());
  cmd += " --min-len " + std::to_string(g_settings.memory.summaryMinLen);
  cmd += " --max-len " + std::to_string(g_settings.memory.summaryMaxLen);
  cmd += " --bootstrap-depth " + std::to_string(g_settings.memory.maxBootstrapDepth);
  for(const auto& extra : extraArgs){
    cmd += " ";
    cmd += shellEscape(extra);
  }
  return tool::detail::execute_shell(request, cmd);
}

inline std::optional<std::string> MemoryTool::normalize_path_arg(const Paths& paths, const std::string& raw){
  if(raw.empty()) return std::string();
  std::string value = raw;
  while(value.size() > 1 && value.back() == '/') value.pop_back();
  if(value == "/" || value == "./" || value == ".") return std::string();
  std::filesystem::path candidate(value);
  auto sanitize = [](const std::string& input){
    std::string out = input;
    while(!out.empty() && out.front() == '/') out.erase(out.begin());
    while(!out.empty() && out.back() == '/') out.pop_back();
    if(out == ".") out.clear();
    return out;
  };
  auto contains_parent = [](const std::string& s){
    if(s.rfind("..", 0) == 0) return true;
    return s.find("/..") != std::string::npos;
  };
  if(candidate.is_absolute()){
    std::error_code ec;
    auto canonicalRoot = std::filesystem::weakly_canonical(paths.root, ec);
    if(ec) canonicalRoot = std::filesystem::absolute(paths.root, ec);
    if(ec) return std::nullopt;
    auto canonicalCandidate = std::filesystem::weakly_canonical(candidate, ec);
    if(ec) canonicalCandidate = std::filesystem::absolute(candidate, ec);
    if(ec) return std::nullopt;
    auto rel = canonicalCandidate.lexically_relative(canonicalRoot);
    std::string normalized = sanitize(rel.generic_string());
    if(contains_parent(normalized)) return std::nullopt;
    return normalized;
  }
  auto normalizedPath = candidate.lexically_normal();
  std::string normalized = sanitize(normalizedPath.generic_string());
  if(contains_parent(normalized)) return std::nullopt;
  return normalized;
}

inline ToolDefinition make_memory_tool(){
  ToolDefinition def;
  def.ui = MemoryTool::ui();
  def.executor = MemoryTool::run;
  return def;
}

} // namespace tool
