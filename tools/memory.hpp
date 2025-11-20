#pragma once

#include "tool_common.hpp"
#include "../utils/memory.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

namespace tool {

namespace {

inline std::string scope_from_flag(bool personalOnly, bool knowledgeOnly){
  if(personalOnly) return "personal";
  if(knowledgeOnly) return "knowledge";
  return "all";
}

inline ToolExecutionResult rebuild_memory_index(const MemoryConfig& cfg, const std::string& langOverride){
  std::ostringstream cmd;
  cmd << "python3 tools/memory_build_index.py"
      << " --root " << shellEscape(cfg.root)
      << " --index " << shellEscape(cfg.indexFile)
      << " --personal " << shellEscape(cfg.personalSubdir)
      << " --min-len " << cfg.summaryMinLen
      << " --max-len " << cfg.summaryMaxLen;
  if(!langOverride.empty()){
    cmd << " --lang " << shellEscape(langOverride);
  }
  return detail::execute_shell(ToolExecutionRequest{}, cmd.str());
}

inline bool ensure_memory_paths(const MemoryConfig& cfg, std::string& message){
  (void)message;
  std::error_code ec;
  std::filesystem::create_directories(cfg.root, ec);
  std::filesystem::create_directories(std::filesystem::path(cfg.root) / cfg.personalSubdir, ec);
  return true;
}

inline ToolExecutionResult handle_memory_init(const MemoryConfig& cfg){
  std::string msg;
  ensure_memory_paths(cfg, msg);
  auto res = rebuild_memory_index(cfg, cfg.summaryLang);
  std::ostringstream oss;
  oss << "Memory root: " << cfg.root << "\n";
  oss << "Index: " << cfg.indexFile << "\n";
  if(!res.output.empty()) oss << res.output;
  return detail::text_result(oss.str(), res.exitCode);
}

inline bool is_supported_memory_file(const std::filesystem::path& p){
  auto ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
  return ext == ".md" || ext == ".txt";
}

inline size_t copy_memory_file(const std::filesystem::path& src, const std::filesystem::path& dst, const std::string& mode){
  std::error_code ec;
  std::filesystem::create_directories(dst.parent_path(), ec);
  if(mode == "link"){
    std::filesystem::remove(dst, ec);
    std::filesystem::create_symlink(src, dst, ec);
  }else{
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
  }
  return 1;
}

inline size_t import_from_source(const std::filesystem::path& src,
                                 const std::filesystem::path& destRoot,
                                 const std::string& mode){
  size_t count = 0;
  if(std::filesystem::is_regular_file(src)){
    if(!is_supported_memory_file(src)) return 0;
    count += copy_memory_file(src, destRoot / src.filename(), mode);
    return count;
  }
  if(std::filesystem::is_directory(src)){
    auto base = src.filename();
    std::filesystem::path prefix = destRoot;
    if(destRoot.filename() != base) prefix /= base;
    for(auto& entry : std::filesystem::recursive_directory_iterator(src)){
      if(entry.is_regular_file() && is_supported_memory_file(entry.path())){
        auto rel = std::filesystem::relative(entry.path(), src);
        count += copy_memory_file(entry.path(), prefix / rel, mode);
      }
    }
  }
  return count;
}

inline std::string default_category_for(const std::filesystem::path& src){
  if(std::filesystem::is_directory(src)){
    return src.filename().string();
  }
  return "misc";
}

inline ToolExecutionResult handle_memory_import(const std::vector<std::string>& args, const MemoryConfig& cfg){
  if(args.size() < 3){
    g_parse_error_cmd = "memory";
    return detail::text_result("usage: memory import <src> [--category <name>] [--personal] [--mode copy|link|mirror] [--lang <code>]\n", 1);
  }
  std::string srcPath;
  std::string category;
  bool personal = false;
  std::string mode = "copy";
  std::string langOverride;
  for(size_t i = 2; i < args.size(); ++i){
    const std::string& tok = args[i];
    if(tok == "--category" && i + 1 < args.size()){
      category = args[++i];
    }else if(tok == "--personal"){
      personal = true;
    }else if(tok == "--mode" && i + 1 < args.size()){
      mode = args[++i];
    }else if(tok == "--lang" && i + 1 < args.size()){
      langOverride = args[++i];
    }else if(tok.size() && tok[0] == '-'){
      continue;
    }else if(srcPath.empty()){
      srcPath = tok;
    }
  }
  if(srcPath.empty()){
    g_parse_error_cmd = "memory";
    return detail::text_result("memory import: missing <src>\n", 1);
  }
  if(mode != "copy" && mode != "link" && mode != "mirror") mode = "copy";
  MemoryConfig effective = cfg;
  if(!langOverride.empty()) effective.summaryLang = langOverride;
  ensure_memory_paths(effective, srcPath);
  std::filesystem::path src(srcPath);
  if(category.empty()) category = default_category_for(src);
  std::filesystem::path destRoot = std::filesystem::path(effective.root) / (personal ? effective.personalSubdir : "knowledge") / category;
  size_t count = import_from_source(src, destRoot, mode);
  auto res = rebuild_memory_index(effective, effective.summaryLang);
  std::ostringstream oss;
  oss << "Imported " << count << " files into " << destRoot << "\n";
  oss << res.output;
  return detail::text_result(oss.str(), res.exitCode);
}

inline ToolExecutionResult handle_memory_list(const std::vector<std::string>& args, const MemoryConfig& cfg){
  std::string path;
  int depth = 1;
  bool personalOnly = false, knowledgeOnly = false;
  for(size_t i = 2; i < args.size(); ++i){
    if(args[i] == "-d" && i + 1 < args.size()){
      depth = std::stoi(args[++i]);
    }else if(args[i] == "--personal-only"){
      personalOnly = true;
    }else if(args[i] == "--knowledge-only"){
      knowledgeOnly = true;
    }else if(path.empty()){
      path = args[i];
    }
  }
  MemoryIndex index;
  if(!index.load(cfg)){
    return detail::text_result("memory index missing, run `memory init` first\n", 1);
  }
  std::ostringstream oss;
  auto scope = scope_from_flag(personalOnly, knowledgeOnly);
  if(path.empty()){
    auto dirs = index.children_of("", depth + 1, true, true, scope == "all" ? std::nullopt : std::optional<std::string>(scope));
    for(const auto& node : dirs){
      if(node.depth == 1 && node.kind == "dir"){
        oss << "[DIR] " << node.relPath << "/\n    —— " << node.summary << "\n";
        for(const auto& child : index.children_of(node.relPath, 1, false, true, scope == "all" ? std::nullopt : std::optional<std::string>(scope))){
          oss << "    " << child.relPath << "\n        —— " << child.summary << "\n";
        }
        oss << "\n";
      }
    }
  }else{
    auto entries = index.children_of(path, depth, true, true, scope == "all" ? std::nullopt : std::optional<std::string>(scope));
    if(const MemoryNode* self = index.find(path)){
      oss << self->relPath << (self->kind == "dir" ? "/" : "") << " —— " << self->summary << "\n";
    }
    for(const auto& node : entries){
      oss << node.relPath << (node.kind == "dir" ? "/" : "") << "\n    —— " << node.summary << "\n";
    }
  }
  return detail::text_result(oss.str());
}

inline ToolExecutionResult handle_memory_show(const std::vector<std::string>& args, const MemoryConfig& cfg){
  if(args.size() < 3){
    g_parse_error_cmd = "memory";
    return detail::text_result("usage: memory show <path> [--content] [--max-bytes N]\n", 1);
  }
  std::string target = args[2];
  bool showContent = false;
  size_t maxBytes = 8192;
  for(size_t i = 3; i < args.size(); ++i){
    if(args[i] == "--content") showContent = true;
    else if(args[i] == "--max-bytes" && i + 1 < args.size()){
      maxBytes = static_cast<size_t>(std::stoul(args[++i]));
    }
  }
  MemoryIndex index;
  if(!index.load(cfg)){
    return detail::text_result("memory index missing, run `memory init` first\n", 1);
  }
  const MemoryNode* node = index.find(target);
  if(!node){
    g_parse_error_cmd = "memory";
    return detail::text_result("memory show: unknown path\n", 1);
  }
  std::ostringstream oss;
  oss << node->relPath << (node->kind == "dir" ? "/" : "") << "\n";
  oss << "kind: " << node->kind << ", bucket: " << node->bucket << ", personal: " << (node->isPersonal ? "yes" : "no") << "\n";
  oss << "summary: " << node->summary << "\n";
  if(showContent && node->kind == "file"){
    bool truncated = false;
    std::string content = index.read_content(node->relPath, maxBytes, truncated);
    oss << "\n" << content;
    if(truncated) oss << "\n... [truncated]";
    oss << "\n";
  }
  return detail::text_result(oss.str());
}

inline ToolExecutionResult handle_memory_search(const std::vector<std::string>& args, const MemoryConfig& cfg){
  if(args.size() < 3){
    g_parse_error_cmd = "memory";
    return detail::text_result("usage: memory search <keywords...> [--scope all|personal|knowledge] [--limit N] [--in summary|content|both]\n", 1);
  }
  std::string scope = "all";
  size_t limit = 10;
  std::string inWhat = "summary";
  std::vector<std::string> keywords;
  for(size_t i = 2; i < args.size(); ++i){
    if(args[i] == "--scope" && i + 1 < args.size()) scope = args[++i];
    else if(args[i] == "--limit" && i + 1 < args.size()) limit = static_cast<size_t>(std::stoul(args[++i]));
    else if(args[i] == "--in" && i + 1 < args.size()) inWhat = args[++i];
    else keywords.push_back(args[i]);
  }
  MemoryIndex index;
  if(!index.load(cfg)){
    return detail::text_result("memory index missing, run `memory init` first\n", 1);
  }
  std::string query = join(keywords, " ");
  bool inSummary = (inWhat == "summary" || inWhat == "both");
  bool inContent = (inWhat == "content" || inWhat == "both");
  auto results = index.search(query, scope, limit, inSummary, inContent);
  std::ostringstream oss;
  for(const auto& node : results){
    oss << node.relPath << " [" << node.bucket << "] score=" << node.tokenEst << "\n    " << node.summary << "\n";
  }
  if(results.empty()) oss << "No matches.\n";
  return detail::text_result(oss.str());
}

inline ToolExecutionResult handle_memory_stats(const MemoryConfig& cfg){
  MemoryIndex index;
  if(!index.load(cfg)){
    return detail::text_result("memory index missing, run `memory init` first\n", 1);
  }
  auto st = index.stats();
  std::ostringstream oss;
  oss << "Nodes: " << st.nodeCount << " (files " << st.fileCount << ", dirs " << st.dirCount << ")\n";
  oss << "Personal: " << st.personalCount << ", knowledge: " << st.knowledgeCount << "\n";
  oss << "Max depth: " << st.maxDepth << "\n";
  oss << "Token estimate: " << st.totalTokens << "\n";
  return detail::text_result(oss.str());
}

inline ToolExecutionResult handle_memory_note(const std::vector<std::string>& args, const MemoryConfig& cfg){
  if(args.size() < 3){
    g_parse_error_cmd = "memory";
    return detail::text_result("usage: memory note <text>\n", 1);
  }
  std::string text;
  for(size_t i = 2; i < args.size(); ++i){
    if(i > 2) text += " ";
    text += args[i];
  }
  auto now = memory_now_iso();
  std::string filename = now.substr(0, 10);
  filename += "-";
  filename += now.substr(11, 8);
  std::replace(filename.begin(), filename.end(), ':', '-');
  filename += ".md";
  std::filesystem::path dir = std::filesystem::path(cfg.root) / cfg.personalSubdir / "notes";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::filesystem::path full = dir / filename;
  std::ofstream out(full);
  out << text << "\n";
  out.close();
  auto res = rebuild_memory_index(cfg, cfg.summaryLang);
  std::ostringstream oss;
  oss << "Created note: " << full << "\n";
  oss << res.output;
  return detail::text_result(oss.str(), res.exitCode);
}

inline bool likely_personal_question(const std::string& question){
  static const std::vector<std::string> hints = {"我", "我的", "之前", "习惯", "偏好", "上次"};
  for(const auto& h : hints){
    if(question.find(h) != std::string::npos) return true;
  }
  return false;
}

inline ToolExecutionResult handle_memory_query(const std::vector<std::string>& args, const MemoryConfig& cfg){
  if(args.size() < 3){
    g_parse_error_cmd = "memory";
    return detail::text_result("usage: memory query <question> [--scope auto|personal|knowledge] [--limit N] [--max-bytes M]\n", 1);
  }
  std::string scope = "auto";
  size_t limit = 5;
  size_t maxBytes = 8192;
  std::string question;
  for(size_t i = 2; i < args.size(); ++i){
    if(args[i] == "--scope" && i + 1 < args.size()) scope = args[++i];
    else if(args[i] == "--limit" && i + 1 < args.size()) limit = static_cast<size_t>(std::stoul(args[++i]));
    else if(args[i] == "--max-bytes" && i + 1 < args.size()) maxBytes = static_cast<size_t>(std::stoul(args[++i]));
    else{
      if(!question.empty()) question += " ";
      question += args[i];
    }
  }
  MemoryIndex index;
  if(!index.load(cfg)){
    return detail::text_result("memory index missing, run `memory init` first\n", 1);
  }
  std::string effectiveScope = scope;
  if(scope == "auto"){
    effectiveScope = likely_personal_question(question) ? "personal" : "all";
  }
  std::ostringstream oss;
  oss << ansi::YELLOW << "[Q]" << ansi::RESET << " (memory) 正在检索记忆并生成回答...\n";
  auto results = index.search(question, effectiveScope, limit, true, true);
  size_t personalHits = 0, knowledgeHits = 0;
  std::ostringstream context;
  int docId = 1;
  for(const auto& node : results){
    bool truncated = false;
    std::string content;
    if(node.kind == "file") content = index.read_content(node.relPath, maxBytes, truncated);
    if(node.bucket == "personal") ++personalHits; else if(node.bucket == "knowledge") ++knowledgeHits;
    context << "=== DOC " << docId++ << ": " << node.relPath << " ===\n";
    context << (content.empty() ? node.summary : content) << "\n\n";
  }
  std::ostringstream answer;
  answer << "问题: " << question << "\n";
  if(results.empty()){
    answer << "记忆中没有找到相关内容。";
  }else{
    answer << "根据记忆中的笔记整理：\n" << context.str();
  }
  oss << answer.str();
  oss << "\n" << ansi::RED << "[Q]" << ansi::RESET << " (memory) 完成（命中 " << personalHits << " 条 personal，" << knowledgeHits << " 条 knowledge）。\n";
  return detail::text_result(oss.str());
}

} // namespace

struct Memory {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "memory";
    spec.summary = "Manage the MyCLI memory system";
    set_tool_summary_locale(spec, "en", "Manage the MyCLI memory system");
    set_tool_summary_locale(spec, "zh", "管理 MyCLI 记忆系统");
    spec.subs = {
      SubcommandSpec{"init", {}, {}, {}, nullptr},
      SubcommandSpec{"import", {}, {positional("<src>")}, {}, nullptr},
      SubcommandSpec{"list", {}, {positional("[<path>]")}, {}, nullptr},
      SubcommandSpec{"show", {}, {positional("<path>")}, {}, nullptr},
      SubcommandSpec{"search", {}, {positional("<keywords...>")}, {}, nullptr},
      SubcommandSpec{"stats", {}, {}, {}, nullptr},
      SubcommandSpec{"note", {}, {positional("<text>")}, {}, nullptr},
      SubcommandSpec{"query", {}, {positional("<question>")}, {}, nullptr}
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 2){
      g_parse_error_cmd = "memory";
      return detail::text_result("usage: memory <init|import|list|show|search|stats|note|query>\n", 1);
    }
    MemoryConfig cfg = memory_config_from_settings();
    if(!cfg.enabled){
      return detail::text_result("memory system disabled via settings\n", 1);
    }
    const std::string sub = args[1];
    if(sub == "init") return handle_memory_init(cfg);
    if(sub == "import") return handle_memory_import(args, cfg);
    if(sub == "list") return handle_memory_list(args, cfg);
    if(sub == "show") return handle_memory_show(args, cfg);
    if(sub == "search") return handle_memory_search(args, cfg);
    if(sub == "stats") return handle_memory_stats(cfg);
    if(sub == "note") return handle_memory_note(args, cfg);
    if(sub == "query") return handle_memory_query(args, cfg);
    g_parse_error_cmd = "memory";
    return detail::text_result("unknown memory subcommand\n", 1);
  }
};

inline ToolDefinition make_memory_tool(){
  ToolDefinition def;
  def.ui = Memory::ui();
  def.executor = Memory::run;
  return def;
}

} // namespace tool

