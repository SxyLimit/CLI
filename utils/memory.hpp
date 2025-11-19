#pragma once

#include "../globals.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

inline bool memory_system_enabled(){
  return g_settings.memory.enabled;
}

inline std::filesystem::path memory_root_path(){
  std::filesystem::path base;
  if(g_settings.memory.root.empty()){
    base = std::filesystem::path(config_home()) / "memory";
  }else{
    base = std::filesystem::path(g_settings.memory.root);
    if(base.is_relative()){
      base = std::filesystem::path(config_home()) / base;
    }
  }
  std::error_code ec;
  auto absPath = std::filesystem::absolute(base, ec);
  if(!ec){
    base = absPath;
  }
  return base.lexically_normal();
}

inline std::filesystem::path memory_index_path(){
  std::filesystem::path custom;
  if(g_settings.memory.indexFile.empty()){
    custom = memory_root_path() / "memory_index.jsonl";
  }else{
    std::filesystem::path raw(g_settings.memory.indexFile);
    if(raw.is_relative()){
      custom = memory_root_path() / raw;
    }else{
      custom = raw;
    }
  }
  std::error_code ec;
  auto absPath = std::filesystem::absolute(custom, ec);
  if(!ec){
    custom = absPath;
  }
  return custom.lexically_normal();
}

inline std::filesystem::path memory_personal_path(){
  std::string subdir = g_settings.memory.personalSubdir.empty()? std::string("personal") : g_settings.memory.personalSubdir;
  std::filesystem::path rel(subdir);
  if(rel.is_absolute()){
    rel = rel.relative_path();
  }
  std::filesystem::path combined = memory_root_path() / rel;
  return combined.lexically_normal();
}

inline std::string memory_summary_language(){
  if(!g_settings.memory.summaryLanguage.empty()){
    return g_settings.memory.summaryLanguage;
  }
  return g_settings.language;
}

struct MemoryNode {
  std::string id;
  std::string kind;
  std::string relPath;
  std::string parent;
  int depth = 0;
  std::string title;
  std::string summary;
  std::vector<std::string> tags;
  bool isPersonal = false;
  std::string bucket = "other";
  bool eagerExpose = false;
  std::string hash;
  std::uint64_t sizeBytes = 0;
  std::uint64_t tokenEstimate = 0;
  std::string createdAt;
  std::string updatedAt;
  std::string sourceImportPath;
  std::string sourceImportMode;

  bool isDir() const { return kind == "dir"; }
  bool isFile() const { return kind == "file"; }
};

enum class MemorySearchScope {
  All,
  Personal,
  Knowledge
};

struct MemorySearchHit {
  const MemoryNode* node = nullptr;
  int summaryHits = 0;
  int titleHits = 0;
  int contentHits = 0;
  double score = 0.0;
};

class MemoryIndex {
public:
  bool load(const std::filesystem::path& indexFile, std::string& error);
  const MemoryNode* find(const std::string& relPath) const;
  std::vector<const MemoryNode*> nodesByDepth(int depth) const;
  std::vector<const MemoryNode*> childrenOf(const std::string& parent) const;
  const std::vector<MemoryNode>& nodes() const { return nodes_; }
  std::vector<MemorySearchHit> search(const std::string& query,
                                      MemorySearchScope scope,
                                      bool inSummary,
                                      bool inContent,
                                      size_t limit,
                                      const std::filesystem::path& memoryRoot) const;

private:
  static std::string lowercase(std::string text);
  static int count_hits(const std::string& haystack, const std::vector<std::string>& tokens);
  static std::vector<std::string> tokenize(const std::string& text);
  static bool scope_matches(const MemoryNode& node, MemorySearchScope scope);
  static std::string sanitize_parent(const std::string& value);
  static std::string read_string(const sj::Object& obj, const std::string& key);
  static int read_int(const sj::Object& obj, const std::string& key, int def = 0);
  static std::uint64_t read_uint64(const sj::Object& obj, const std::string& key);
  static bool read_bool(const sj::Object& obj, const std::string& key, bool def = false);

  std::vector<MemoryNode> nodes_;
  std::unordered_map<std::string, size_t> indexByPath_;
  std::unordered_map<std::string, std::vector<size_t>> children_;
  std::unordered_map<int, std::vector<size_t>> depthBuckets_;
};

inline std::string MemoryIndex::lowercase(std::string text){
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch){
    return static_cast<char>(std::tolower(ch));
  });
  return text;
}

inline int MemoryIndex::count_hits(const std::string& haystack, const std::vector<std::string>& tokens){
  if(haystack.empty()) return 0;
  int hits = 0;
  for(const auto& token : tokens){
    if(token.empty()) continue;
    size_t pos = 0;
    while((pos = haystack.find(token, pos)) != std::string::npos){
      ++hits;
      pos += token.size();
    }
  }
  return hits;
}

inline std::vector<std::string> MemoryIndex::tokenize(const std::string& text){
  std::vector<std::string> tokens;
  std::string current;
  for(char ch : text){
    if(std::isspace(static_cast<unsigned char>(ch))){
      if(!current.empty()){
        tokens.push_back(lowercase(current));
        current.clear();
      }
    }else{
      current.push_back(ch);
    }
  }
  if(!current.empty()){
    tokens.push_back(lowercase(current));
  }
  return tokens;
}

inline bool MemoryIndex::scope_matches(const MemoryNode& node, MemorySearchScope scope){
  switch(scope){
    case MemorySearchScope::All:
      return true;
    case MemorySearchScope::Personal:
      return node.bucket == "personal" || node.isPersonal;
    case MemorySearchScope::Knowledge:
      return node.bucket == "knowledge";
  }
  return true;
}

inline std::string MemoryIndex::sanitize_parent(const std::string& value){
  if(value == "." || value == "/" || value == "null") return std::string();
  return value;
}

inline std::string MemoryIndex::read_string(const sj::Object& obj, const std::string& key){
  auto it = obj.find(key);
  if(it == obj.end()) return std::string();
  if(!it->second.isString()) return std::string();
  return it->second.asString();
}

inline int MemoryIndex::read_int(const sj::Object& obj, const std::string& key, int def){
  auto it = obj.find(key);
  if(it == obj.end()) return def;
  return static_cast<int>(it->second.asInteger(def));
}

inline std::uint64_t MemoryIndex::read_uint64(const sj::Object& obj, const std::string& key){
  auto it = obj.find(key);
  if(it == obj.end()) return 0;
  long long value = it->second.asInteger(0);
  if(value < 0) value = 0;
  return static_cast<std::uint64_t>(value);
}

inline bool MemoryIndex::read_bool(const sj::Object& obj, const std::string& key, bool def){
  auto it = obj.find(key);
  if(it == obj.end()) return def;
  if(it->second.isBool()) return it->second.asBool(def);
  return def;
}

inline bool MemoryIndex::load(const std::filesystem::path& indexFile, std::string& error){
  nodes_.clear();
  indexByPath_.clear();
  children_.clear();
  depthBuckets_.clear();

  std::ifstream in(indexFile);
  if(!in.good()){
    error = "failed to open memory index";
    return false;
  }
  std::vector<MemoryNode> rawNodes;
  std::unordered_map<std::string, size_t> rawLookup;
  std::string line;
  size_t lineNo = 0;
  while(std::getline(in, line)){
    ++lineNo;
    if(!line.empty() && line.back() == '\r') line.pop_back();
    if(line.empty()) continue;
    try{
      sj::Parser parser(line);
      sj::Value value = parser.parse();
      if(!value.isObject()) continue;
      const auto& obj = value.asObject();
      MemoryNode node;
      node.id = read_string(obj, "id");
      node.kind = read_string(obj, "kind");
      node.relPath = read_string(obj, "rel_path");
      if(node.relPath.empty()) node.relPath = node.id;
      node.parent = sanitize_parent(read_string(obj, "parent"));
      node.depth = read_int(obj, "depth", 0);
      node.title = read_string(obj, "title");
      node.summary = read_string(obj, "summary");
      node.bucket = read_string(obj, "bucket");
      if(node.bucket.empty()) node.bucket = "other";
      node.isPersonal = read_bool(obj, "is_personal", node.bucket == "personal");
      node.eagerExpose = read_bool(obj, "eager_expose", false);
      node.hash = read_string(obj, "hash");
      node.sizeBytes = read_uint64(obj, "size_bytes");
      node.tokenEstimate = read_uint64(obj, "token_est");
      node.createdAt = read_string(obj, "created_at");
      node.updatedAt = read_string(obj, "updated_at");
      if(auto tagsIt = obj.find("tags"); tagsIt != obj.end() && tagsIt->second.isArray()){
        for(const auto& tagVal : tagsIt->second.asArray()){
          if(tagVal.isString()) node.tags.push_back(tagVal.asString());
        }
      }
      if(auto srcIt = obj.find("source"); srcIt != obj.end() && srcIt->second.isObject()){
        const auto& srcObj = srcIt->second.asObject();
        node.sourceImportPath = read_string(srcObj, "import_path");
        node.sourceImportMode = read_string(srcObj, "import_mode");
      }
      std::string key = node.relPath;
      if(key.empty() && node.kind != "file") key = node.relPath;
      auto it = rawLookup.find(key);
      if(it != rawLookup.end()){
        rawNodes[it->second] = std::move(node);
      }else{
        size_t idx = rawNodes.size();
        rawNodes.push_back(std::move(node));
        rawLookup[key] = idx;
      }
    }catch(const std::exception& ex){
      error = "memory index parse error at line " + std::to_string(lineNo) + ": " + ex.what();
      return false;
    }
  }
  nodes_ = std::move(rawNodes);
  std::sort(nodes_.begin(), nodes_.end(), [](const MemoryNode& a, const MemoryNode& b){
    if(a.depth != b.depth) return a.depth < b.depth;
    return a.relPath < b.relPath;
  });
  for(size_t i = 0; i < nodes_.size(); ++i){
    indexByPath_[nodes_[i].relPath] = i;
    depthBuckets_[nodes_[i].depth].push_back(i);
    if(nodes_[i].relPath.empty()) continue;
    std::string parent = nodes_[i].parent;
    children_[parent].push_back(i);
  }
  for(auto& entry : depthBuckets_){
    auto& bucket = entry.second;
    std::sort(bucket.begin(), bucket.end(), [&](size_t lhs, size_t rhs){
      return nodes_[lhs].relPath < nodes_[rhs].relPath;
    });
  }
  for(auto& entry : children_){
    auto& bucket = entry.second;
    std::sort(bucket.begin(), bucket.end(), [&](size_t lhs, size_t rhs){
      return nodes_[lhs].relPath < nodes_[rhs].relPath;
    });
  }
  return true;
}

inline const MemoryNode* MemoryIndex::find(const std::string& relPath) const{
  auto it = indexByPath_.find(relPath);
  if(it == indexByPath_.end()) return nullptr;
  return &nodes_[it->second];
}

inline std::vector<const MemoryNode*> MemoryIndex::nodesByDepth(int depth) const{
  std::vector<const MemoryNode*> out;
  auto it = depthBuckets_.find(depth);
  if(it == depthBuckets_.end()) return out;
  for(size_t idx : it->second){
    out.push_back(&nodes_[idx]);
  }
  return out;
}

inline std::vector<const MemoryNode*> MemoryIndex::childrenOf(const std::string& parent) const{
  std::vector<const MemoryNode*> out;
  auto it = children_.find(parent);
  if(it == children_.end()) return out;
  for(size_t idx : it->second){
    out.push_back(&nodes_[idx]);
  }
  return out;
}

inline std::vector<MemorySearchHit> MemoryIndex::search(const std::string& query,
                                                        MemorySearchScope scope,
                                                        bool inSummary,
                                                        bool inContent,
                                                        size_t limit,
                                                        const std::filesystem::path& memoryRoot) const{
  std::vector<MemorySearchHit> results;
  auto tokens = tokenize(query);
  if(tokens.empty()) return results;
  std::filesystem::path base = memoryRoot;
  std::error_code ec;
  auto absBase = std::filesystem::absolute(base, ec);
  if(!ec) base = absBase;
  base = base.lexically_normal();
  constexpr size_t kMaxContentBytes = 65536;
  for(const auto& node : nodes_){
    if(!scope_matches(node, scope)) continue;
    int summaryHits = 0;
    int titleHits = 0;
    int contentHits = 0;
    if(inSummary){
      summaryHits = count_hits(lowercase(node.summary), tokens);
      titleHits = count_hits(lowercase(node.title), tokens);
    }
    if(inContent && node.isFile()){
      std::filesystem::path full = base / std::filesystem::path(node.relPath);
      std::ifstream in(full);
      if(in.good()){
        std::string buffer;
        buffer.reserve(kMaxContentBytes);
        char chunk[4096];
        size_t total = 0;
        while(in.good() && total < kMaxContentBytes){
          in.read(chunk, static_cast<std::streamsize>(std::min<size_t>(sizeof(chunk), kMaxContentBytes - total)));
          std::streamsize got = in.gcount();
          if(got <= 0) break;
          buffer.append(chunk, static_cast<size_t>(got));
          total += static_cast<size_t>(got);
        }
        if(!buffer.empty()){
          contentHits = count_hits(lowercase(buffer), tokens);
        }
      }
    }
    if(summaryHits == 0 && titleHits == 0 && contentHits == 0) continue;
    MemorySearchHit hit;
    hit.node = &node;
    hit.summaryHits = summaryHits;
    hit.titleHits = titleHits;
    hit.contentHits = contentHits;
    hit.score = static_cast<double>(summaryHits * 3 + titleHits * 2 + contentHits);
    results.push_back(hit);
  }
  std::sort(results.begin(), results.end(), [](const MemorySearchHit& a, const MemorySearchHit& b){
    if(a.score == b.score){
      if(a.node && b.node) return a.node->relPath < b.node->relPath;
      return a.node < b.node;
    }
    return a.score > b.score;
  });
  if(limit > 0 && results.size() > limit){
    results.resize(limit);
  }
  return results;
}

