#pragma once

#include "../globals.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_set>

inline bool is_valid_memory_char(char c){
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

inline std::string sanitize_memory_component(const std::string& in){
  std::string out;
  char last = 0;
  for(unsigned char uc : in){
    char c = static_cast<char>(uc);
    if(is_valid_memory_char(c)){
      out.push_back(c);
      last = c;
    }else if(last != '-' && last != '_'){
      out.push_back('-');
      last = '-';
    }
  }
  while(out.size() > 1 && (out.front() == '-' || out.front() == '_')) out.erase(out.begin());
  while(out.size() > 1 && (out.back() == '-' || out.back() == '_')) out.pop_back();
  if(out.empty()) out = "untitled";
  return out;
}

inline std::string sanitize_memory_filename(const std::string& name){
  std::filesystem::path p(name);
  std::string ext = p.extension().string();
  std::string stem = sanitize_memory_component(p.stem().string());
  if(ext.empty() && name == p.filename().string()) return stem;
  return stem + ext;
}

inline std::filesystem::path sanitize_memory_relative(const std::filesystem::path& rel){
  std::filesystem::path out;
  for(const auto& part : rel){
    if(part == "." || part == "..") continue;
    std::string candidate = part.string();
    std::string ext = part.extension().string();
    if(!ext.empty()){
      out /= sanitize_memory_component(part.stem().string()) + ext;
    }else{
      out /= sanitize_memory_component(candidate);
    }
  }
  return out;
}

struct MemoryNode {
  std::string id;
  std::string kind;
  std::string relPath;
  std::string parent;
  int depth = 0;
  std::string title;
  std::string summary;
  bool isPersonal = false;
  std::string bucket = "other";
  bool eagerExpose = false;
  std::vector<std::string> children;
  long long sizeBytes = -1;
  long long tokenEst = -1;
};

struct MemoryStats {
  size_t nodeCount = 0;
  size_t fileCount = 0;
  size_t dirCount = 0;
  size_t personalCount = 0;
  size_t knowledgeCount = 0;
  int maxDepth = 0;
  long long totalTokens = 0;
};

inline MemoryConfig memory_config_from_settings(){
  MemoryConfig cfg = g_settings.memory;
  if(cfg.root.empty()) cfg.root = config_home() + "/memory";
  if(cfg.indexFile.empty()) cfg.indexFile = cfg.root + "/memory_index.jsonl";
  if(cfg.personalSubdir.empty()) cfg.personalSubdir = "personal";
  cfg.personalSubdir = sanitize_memory_component(cfg.personalSubdir);
  if(cfg.summaryLang.empty()) cfg.summaryLang = g_settings.language;
  if(cfg.summaryMinLen <= 0) cfg.summaryMinLen = 50;
  if(cfg.summaryMaxLen <= 0) cfg.summaryMaxLen = 100;
  if(cfg.maxBootstrapDepth <= 0) cfg.maxBootstrapDepth = 1;
  return cfg;
}

inline std::string memory_parent_of(const std::string& relPath){
  auto pos = relPath.find_last_of('/') ;
  if(pos == std::string::npos) return "";
  if(pos == 0) return "";
  return relPath.substr(0, pos);
}

inline int memory_depth_of(const std::string& relPath){
  if(relPath.empty()) return 0;
  return static_cast<int>(std::count(relPath.begin(), relPath.end(), '/') + 1);
}

class MemoryIndex {
public:
  bool load(const MemoryConfig& cfg){
    return load(cfg.indexFile, cfg.root);
  }

  bool load(const std::string& indexPath, const std::string& rootPath){
    root_ = rootPath;
    nodes_.clear();
    std::ifstream in(indexPath);
    if(!in.good()) return false;
    std::string line;
    while(std::getline(in, line)){
      if(line.empty()) continue;
      try{
        sj::Parser parser(line);
        sj::Value val = parser.parse();
        if(!val.isObject()) continue;
        const auto& obj = val.asObject();
        MemoryNode node;
        if(auto it = obj.find("id"); it != obj.end() && it->second.isString()) node.id = it->second.asString();
        if(auto it = obj.find("rel_path"); it != obj.end() && it->second.isString()) node.relPath = it->second.asString();
        if(node.id.empty()) node.id = node.relPath;
        if(node.relPath.empty()) node.relPath = node.id;
        if(auto it = obj.find("parent"); it != obj.end() && it->second.isString()) node.parent = it->second.asString();
        if(node.parent.empty()) node.parent = memory_parent_of(node.relPath);
        if(auto it = obj.find("depth"); it != obj.end()) node.depth = static_cast<int>(it->second.asInteger(memory_depth_of(node.relPath)));
        else node.depth = memory_depth_of(node.relPath);
        if(auto it = obj.find("kind"); it != obj.end() && it->second.isString()) node.kind = it->second.asString();
        if(node.kind.empty()) node.kind = "file";
        if(auto it = obj.find("title"); it != obj.end() && it->second.isString()) node.title = it->second.asString();
        if(node.title.empty()) node.title = basenameOf(node.relPath);
        if(auto it = obj.find("summary"); it != obj.end() && it->second.isString()) node.summary = it->second.asString();
        if(auto it = obj.find("is_personal"); it != obj.end()) node.isPersonal = it->second.asBool(false);
        if(auto it = obj.find("bucket"); it != obj.end() && it->second.isString()) node.bucket = it->second.asString();
        if(node.bucket.empty()) node.bucket = node.isPersonal ? "personal" : "knowledge";
        if(auto it = obj.find("eager_expose"); it != obj.end()) node.eagerExpose = it->second.asBool(false);
        if(auto it = obj.find("size_bytes"); it != obj.end()) node.sizeBytes = it->second.asInteger(-1);
        if(auto it = obj.find("token_est"); it != obj.end()) node.tokenEst = it->second.asInteger(-1);
        if(auto it = obj.find("children"); it != obj.end() && it->second.isArray()){
          for(const auto& c : it->second.asArray()){
            if(c.isString()) node.children.push_back(c.asString());
          }
        }
        nodes_[node.relPath] = std::move(node);
      }catch(const std::exception&){
        continue;
      }
    }
    if(nodes_.find("") == nodes_.end()){
      MemoryNode root;
      root.id = root.relPath = "";
      root.kind = "dir";
      root.bucket = "other";
      root.title = "Memory";
      nodes_[root.relPath] = root;
    }
    return true;
  }

  const MemoryNode* find(const std::string& relPath) const {
    auto it = nodes_.find(relPath);
    if(it == nodes_.end()) return nullptr;
    return &it->second;
  }

  std::vector<MemoryNode> children_of(const std::string& relPath,
                                      int maxDepth,
                                      bool includeDirs,
                                      bool includeFiles,
                                      const std::optional<std::string>& scope = std::nullopt) const {
    std::vector<MemoryNode> out;
    int baseDepth = memory_depth_of(relPath);
    for(const auto& kv : nodes_){
      const MemoryNode& node = kv.second;
      if(node.relPath == relPath) continue;
      if(!relPath.empty()){
        if(node.relPath.size() <= relPath.size()) continue;
        if(node.relPath.rfind(relPath, 0) != 0) continue;
        if(node.relPath[relPath.size()] != '/') continue;
      }
      int delta = node.depth - baseDepth;
      if(delta <= 0 || delta > maxDepth) continue;
      if(scope){
        if(*scope == "personal" && node.bucket != "personal") continue;
        if(*scope == "knowledge" && node.bucket != "knowledge") continue;
      }
      if(node.kind == "dir" && !includeDirs) continue;
      if(node.kind == "file" && !includeFiles) continue;
      out.push_back(node);
    }
    std::sort(out.begin(), out.end(), [](const MemoryNode& a, const MemoryNode& b){
      if(a.depth != b.depth) return a.depth < b.depth;
      return a.relPath < b.relPath;
    });
    return out;
  }

  std::string read_content(const std::string& relPath, size_t maxBytes, bool& truncated) const {
    truncated = false;
    std::filesystem::path full = std::filesystem::path(root_) / relPath;
    std::ifstream in(full, std::ios::binary);
    if(!in.good()) return "";
    std::string data;
    data.resize(maxBytes);
    in.read(&data[0], static_cast<std::streamsize>(maxBytes));
    std::streamsize got = in.gcount();
    data.resize(static_cast<size_t>(got));
    if(in.peek() != EOF){
      truncated = true;
    }
    return data;
  }

  std::vector<MemoryNode> search(const std::string& query,
                                 const std::string& scope,
                                 size_t limit,
                                 bool inSummary,
                                 bool inContent) const {
    std::vector<MemoryNode> results;
    std::string loweredQuery = query;
    std::transform(loweredQuery.begin(), loweredQuery.end(), loweredQuery.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    std::vector<std::string> keywords = splitTokens(loweredQuery);
    if(keywords.empty() && !loweredQuery.empty()) keywords.push_back(loweredQuery);
    for(const auto& kv : nodes_){
      const MemoryNode& node = kv.second;
      if(node.kind != "file" && node.kind != "dir") continue;
      if(scope == "personal" && node.bucket != "personal") continue;
      if(scope == "knowledge" && node.bucket != "knowledge") continue;
      if(node.relPath.empty()) continue;
      std::string titleLower = node.title;
      std::string summaryLower = node.summary;
      std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
      std::transform(summaryLower.begin(), summaryLower.end(), summaryLower.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
      int titleHits = 0, summaryHits = 0, contentHits = 0;
      for(const auto& kw : keywords){
        if(!kw.empty() && titleLower.find(kw) != std::string::npos) ++titleHits;
        if(inSummary && !kw.empty() && summaryLower.find(kw) != std::string::npos) ++summaryHits;
      }
      if(inContent && !keywords.empty() && node.kind == "file"){
        bool dummy = false;
        std::string content = read_content(node.relPath, 8192, dummy);
        std::string contentLower = content;
        std::transform(contentLower.begin(), contentLower.end(), contentLower.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        for(const auto& kw : keywords){
          if(!kw.empty() && contentLower.find(kw) != std::string::npos) ++contentHits;
        }
      }
      int score = 3 * summaryHits + 2 * titleHits + contentHits;
      if(score <= 0) continue;
      MemoryNode copy = node;
      copy.tokenEst = score; // reuse field to store score for now
      results.push_back(std::move(copy));
    }
    std::sort(results.begin(), results.end(), [](const MemoryNode& a, const MemoryNode& b){
      if(a.tokenEst != b.tokenEst) return a.tokenEst > b.tokenEst;
      return a.relPath < b.relPath;
    });
    if(results.size() > limit) results.resize(limit);
    return results;
  }

  MemoryStats stats() const {
    MemoryStats st;
    st.nodeCount = nodes_.size();
    for(const auto& kv : nodes_){
      const MemoryNode& node = kv.second;
      st.maxDepth = std::max(st.maxDepth, node.depth);
      if(node.kind == "dir") ++st.dirCount; else ++st.fileCount;
      if(node.bucket == "personal") ++st.personalCount;
      else if(node.bucket == "knowledge") ++st.knowledgeCount;
      if(node.tokenEst > 0) st.totalTokens += node.tokenEst;
    }
    return st;
  }

  std::vector<MemoryNode> eager_nodes() const {
    std::vector<MemoryNode> out;
    for(const auto& kv : nodes_){
      if(kv.second.eagerExpose) out.push_back(kv.second);
    }
    std::sort(out.begin(), out.end(), [](const MemoryNode& a, const MemoryNode& b){
      if(a.depth != b.depth) return a.depth < b.depth;
      return a.relPath < b.relPath;
    });
    return out;
  }

  const std::string& root() const { return root_; }

private:
  std::string root_;
  std::map<std::string, MemoryNode> nodes_;
};

inline std::string memory_now_iso(){
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return std::string(buf);
}

inline std::string memory_default_root_summary(){
  return "Memory root overview for personal preferences and knowledge notes.";
}

inline std::filesystem::path memory_event_log_path(const MemoryConfig& cfg){
  return std::filesystem::path(cfg.root) / "memory_events.jsonl";
}

inline void memory_append_event(const MemoryConfig& cfg, const std::string& kind, const std::string& detail){
  std::error_code ec;
  std::filesystem::create_directories(cfg.root, ec);
  std::ofstream out(memory_event_log_path(cfg), std::ios::app);
  if(!out.good()) return;
  sj::Object obj;
  obj["ts"] = sj::Value(memory_now_iso());
  obj["kind"] = sj::Value(kind);
  obj["detail"] = sj::Value(detail);
  out << sj::dump(sj::Value(obj)) << "\n";
}

