#pragma once

#include "tool_common.hpp"
#include "../utils/json.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <system_error>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace tool {

struct AgentFsConfig {
  std::filesystem::path sandboxRoot;
  std::vector<std::string> allowedExtensions;
  size_t maxReadBytes = 4096;
  size_t maxWriteBytes = 65536;
  size_t maxTreeEntries = 2048;
  int toolTimeoutMs = 15000;
};

inline AgentFsConfig default_agent_fs_config(){
  AgentFsConfig cfg;
  cfg.sandboxRoot = std::filesystem::current_path();
  cfg.allowedExtensions = {".py", ".md", ".txt", ".json", ".yaml", ".yml", ".toml", ".html", ".css", ".js"};
  return cfg;
}

inline std::filesystem::path agent_realpath(const std::filesystem::path& input, std::error_code& ec){
  std::filesystem::path path = input;
  if(path.is_relative()){
    path = std::filesystem::current_path() / path;
  }
  return std::filesystem::weakly_canonical(path, ec);
}

inline bool path_has_allowed_extension(const AgentFsConfig& cfg, const std::filesystem::path& path){
  if(cfg.allowedExtensions.empty()) return true;
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch){ return static_cast<char>(std::tolower(ch)); });
  for(const auto& allowed : cfg.allowedExtensions){
    std::string candidate = allowed;
    if(candidate.empty()) continue;
    if(candidate.front() != '.') candidate.insert(candidate.begin(), '.');
    std::transform(candidate.begin(), candidate.end(), candidate.begin(), [](unsigned char ch){ return static_cast<char>(std::tolower(ch)); });
    if(ext == candidate) return true;
  }
  return false;
}

inline bool path_within_sandbox(const AgentFsConfig& cfg, const std::filesystem::path& resolved){
  std::error_code ec;
  auto sandbox = std::filesystem::weakly_canonical(cfg.sandboxRoot, ec);
  if(ec) return false;
  auto canonical = std::filesystem::weakly_canonical(resolved, ec);
  if(ec) return false;
  auto sandboxStr = sandbox.generic_u8string();
  auto targetStr = canonical.generic_u8string();
  if(sandboxStr.empty()) return false;
  if(targetStr.size() < sandboxStr.size()) return false;
  if(targetStr.compare(0, sandboxStr.size(), sandboxStr) != 0) return false;
  if(targetStr.size() > sandboxStr.size()){
    char sep = targetStr[sandboxStr.size()];
    if(sep != '/') return false;
  }
  return true;
}

inline uint64_t fnv1a_64(const std::string& data){
  uint64_t hash = 1469598103934665603ull;
  for(unsigned char ch : data){
    hash ^= static_cast<uint64_t>(ch);
    hash *= 1099511628211ull;
  }
  return hash;
}

inline std::string hash_hex(uint64_t value){
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(value));
  return std::string(buf);
}

inline std::string read_file_to_string(const std::filesystem::path& path, std::error_code& ec){
  std::ifstream ifs(path, std::ios::binary);
  if(!ifs){
    ec = std::make_error_code(std::errc::no_such_file_or_directory);
    return {};
  }
  std::string content;
  ifs.seekg(0, std::ios::end);
  std::streamoff size = ifs.tellg();
  if(size < 0){
    ec = std::make_error_code(std::errc::io_error);
    return {};
  }
  content.resize(static_cast<size_t>(size));
  ifs.seekg(0, std::ios::beg);
  if(size > 0){
    ifs.read(&content[0], size);
    if(!ifs){
      ec = std::make_error_code(std::errc::io_error);
      return {};
    }
  }
  ec.clear();
  return content;
}

inline sj::Value make_range_meta(size_t offset, size_t length){
  sj::Object obj;
  obj.emplace("offset", sj::Value(static_cast<long long>(offset)));
  obj.emplace("length", sj::Value(static_cast<long long>(length)));
  return sj::Value(std::move(obj));
}

inline sj::Value meta_with_duration(uint64_t durationMs){
  sj::Object obj;
  obj.emplace("duration_ms", sj::Value(static_cast<long long>(durationMs)));
  return sj::Value(std::move(obj));
}

inline std::string random_session_id(){
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  std::mt19937_64 rng(static_cast<uint64_t>(ms));
  std::uniform_int_distribution<uint64_t> dist;
  uint64_t randomPart = dist(rng);
  char buf[33];
  std::snprintf(buf, sizeof(buf), "%016llx%016llx", static_cast<unsigned long long>(ms), static_cast<unsigned long long>(randomPart));
  return std::string(buf);
}

inline std::string error_json(const std::string& code, const std::string& message){
  sj::Object obj;
  obj.emplace("code", sj::Value(code));
  obj.emplace("message", sj::Value(message));
  return sj::dump(sj::Value(std::move(obj)));
}

inline std::string duration_meta_to_string(uint64_t durationMs){
  return sj::dump(meta_with_duration(durationMs));
}

inline bool parse_size_arg(const std::string& token, size_t& out){
  if(token.empty()) return false;
  try{
    size_t idx = 0;
    unsigned long long value = std::stoull(token, &idx, 10);
    if(idx != token.size()) return false;
    out = static_cast<size_t>(value);
    return true;
  }catch(...){
    return false;
  }
}

} // namespace tool

