#pragma once

#include "globals.hpp"

#include <sstream>

inline AppConfig g_config{};

namespace {

std::string normalizeBool(const std::string& v){
  std::string t = v; std::transform(t.begin(), t.end(), t.begin(), ::tolower);
  return t;
}

bool parseBool(const std::string& v, bool& out){
  std::string t = normalizeBool(v);
  if(t=="true" || t=="1" || t=="yes" || t=="on"){ out = true; return true; }
  if(t=="false" || t=="0" || t=="no" || t=="off"){ out = false; return true; }
  return false;
}

std::string cwdModeToString(CwdMode mode){
  switch(mode){
    case CwdMode::Full:   return "full";
    case CwdMode::Omit:   return "omit";
    case CwdMode::Hidden: return "hidden";
  }
  return "full";
}

bool parseCwdMode(const std::string& v, CwdMode& out){
  std::string t = normalizeBool(v);
  if(t=="full"){ out = CwdMode::Full; return true; }
  if(t=="omit"){ out = CwdMode::Omit; return true; }
  if(t=="hidden"){ out = CwdMode::Hidden; return true; }
  return false;
}

const std::vector<std::string>& knownKeys(){
  static const std::vector<std::string> keys = {
    "prompt.cwd",
    "completion.ignore_case",
    "completion.subsequence",
    "language"
  };
  return keys;
}

}

inline void load_config(const std::string& path){
  AppConfig defaults;
  g_config = defaults;

  std::ifstream in(path);
  if(!in.good()) return;

  std::string line;
  while(std::getline(in, line)){
    if(line.empty() || line[0]=='#' || line[0]==';') continue;
    auto eq = line.find('=');
    if(eq==std::string::npos) continue;
    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq+1);
    auto trim = [](std::string s){
      size_t a=0,b=s.size();
      while(a<b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
      while(b>a && std::isspace(static_cast<unsigned char>(s[b-1]))) --b;
      return s.substr(a, b-a);
    };
    key = trim(key);
    val = trim(val);
    if(key=="prompt.cwd"){
      CwdMode mode;
      if(parseCwdMode(val, mode)) g_config.cwdMode = mode;
    }else if(key=="completion.ignore_case"){
      bool b; if(parseBool(val,b)) g_config.completionIgnoreCase = b;
    }else if(key=="completion.subsequence"){
      bool b; if(parseBool(val,b)) g_config.completionSubsequence = b;
    }else if(key=="language"){
      if(!val.empty()) g_config.language = val;
    }
  }
}

inline void save_config(const std::string& path){
  std::ofstream out(path);
  if(!out.good()) return;
  out << "prompt.cwd=" << cwdModeToString(g_config.cwdMode) << "\n";
  out << "completion.ignore_case=" << (g_config.completionIgnoreCase? "true" : "false") << "\n";
  out << "completion.subsequence=" << (g_config.completionSubsequence? "true" : "false") << "\n";
  out << "language=" << g_config.language << "\n";
}

inline void apply_config_to_runtime(){
  g_cwd_mode = g_config.cwdMode;
}

inline bool config_get_value(const std::string& key, std::string& value){
  if(key=="prompt.cwd"){
    value = cwdModeToString(g_config.cwdMode); return true;
  }
  if(key=="completion.ignore_case"){
    value = g_config.completionIgnoreCase? "true" : "false"; return true;
  }
  if(key=="completion.subsequence"){
    value = g_config.completionSubsequence? "true" : "false"; return true;
  }
  if(key=="language"){
    value = g_config.language; return true;
  }
  return false;
}

inline bool config_set_value(const std::string& key, const std::string& value, std::string& error){
  if(key=="prompt.cwd"){
    CwdMode mode;
    if(!parseCwdMode(value, mode)){
      error = "invalid_value";
      return false;
    }
    g_config.cwdMode = mode;
    apply_config_to_runtime();
    return true;
  }
  if(key=="completion.ignore_case"){
    bool b;
    if(!parseBool(value, b)){
      error = "invalid_value";
      return false;
    }
    g_config.completionIgnoreCase = b;
    return true;
  }
  if(key=="completion.subsequence"){
    bool b;
    if(!parseBool(value, b)){
      error = "invalid_value";
      return false;
    }
    g_config.completionSubsequence = b;
    return true;
  }
  if(key=="language"){
    if(value.empty()){
      error = "invalid_value";
      return false;
    }
    g_config.language = value;
    return true;
  }
  error = "unknown_key";
  return false;
}

inline std::vector<std::string> config_list_keys(){
  return knownKeys();
}
