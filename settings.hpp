#pragma once

#include "globals.hpp"

#include <sstream>

inline AppSettings g_settings{};

enum class SettingValueKind {
  Boolean,
  Enum,
  String,
};

struct SettingKeyInfo {
  SettingValueKind kind = SettingValueKind::String;
  std::vector<std::string> allowedValues;
};

namespace {

std::vector<std::string>& languageStorage(){
  static std::vector<std::string> langs = {"en", "zh"};
  return langs;
}

const std::map<std::string, SettingKeyInfo>& keyInfoMap(){
  static const std::map<std::string, SettingKeyInfo> infos = {
    {"prompt.cwd", {SettingValueKind::Enum, {"full", "omit", "hidden"}}},
    {"completion.ignore_case", {SettingValueKind::Boolean, {"false", "true"}}},
    {"completion.subsequence", {SettingValueKind::Boolean, {"false", "true"}}},
    {"language", {SettingValueKind::String, {}}},
    {"ui.path_error_hint", {SettingValueKind::Boolean, {"false", "true"}}},
    {"message.folder", {SettingValueKind::String, {}}},
    {"prompt.name", {SettingValueKind::String, {}}},
    {"prompt.theme", {SettingValueKind::Enum, {"blue", "blue-purple"}}},
  };
  return infos;
}

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

}

inline void settings_register_language(const std::string& lang){
  if(lang.empty()) return;
  auto& langs = languageStorage();
  if(std::find(langs.begin(), langs.end(), lang)==langs.end()){
    langs.push_back(lang);
  }
}

inline const std::vector<std::string>& settings_known_languages(){
  return languageStorage();
}

inline const SettingKeyInfo* settings_key_info(const std::string& key){
  const auto& map = keyInfoMap();
  auto it = map.find(key);
  if(it==map.end()) return nullptr;
  return &it->second;
}

inline std::vector<std::string> settings_value_suggestions_for(const std::string& key){
  std::vector<std::string> out;
  const SettingKeyInfo* info = settings_key_info(key);
  if(!info) return out;
  switch(info->kind){
    case SettingValueKind::Boolean:
    case SettingValueKind::Enum:
      out = info->allowedValues;
      break;
    case SettingValueKind::String:
      if(key=="language"){
        const auto& langs = settings_known_languages();
        out.assign(langs.begin(), langs.end());
      }else if(key=="prompt.name"){
        // no predefined suggestions
      }
      break;
  }
  return out;
}

inline void load_settings(const std::string& path){
  AppSettings defaults;
  g_settings = defaults;

  settings_register_language("en");
  settings_register_language("zh");

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
      if(parseCwdMode(val, mode)) g_settings.cwdMode = mode;
    }else if(key=="completion.ignore_case"){
      bool b; if(parseBool(val,b)) g_settings.completionIgnoreCase = b;
    }else if(key=="completion.subsequence"){
      bool b; if(parseBool(val,b)) g_settings.completionSubsequence = b;
    }else if(key=="language"){
      if(!val.empty()){ g_settings.language = val; settings_register_language(val); }
    }else if(key=="ui.path_error_hint"){
      bool b; if(parseBool(val,b)) g_settings.showPathErrorHint = b;
    }else if(key=="message.folder"){
      g_settings.messageWatchFolder = val;
    }else if(key=="prompt.name"){
      g_settings.promptName = val.empty()? "mycli" : val;
    }else if(key=="prompt.theme"){
      std::string t = val;
      std::transform(t.begin(), t.end(), t.begin(), ::tolower);
      if(t=="blue" || t=="blue-purple"){
        g_settings.promptTheme = t;
      }
    }
  }
}

inline void save_settings(const std::string& path){
  std::ofstream out(path);
  if(!out.good()) return;
  out << "prompt.cwd=" << cwdModeToString(g_settings.cwdMode) << "\n";
  out << "completion.ignore_case=" << (g_settings.completionIgnoreCase? "true" : "false") << "\n";
  out << "completion.subsequence=" << (g_settings.completionSubsequence? "true" : "false") << "\n";
  out << "language=" << g_settings.language << "\n";
  out << "ui.path_error_hint=" << (g_settings.showPathErrorHint? "true" : "false") << "\n";
  out << "message.folder=" << g_settings.messageWatchFolder << "\n";
  out << "prompt.name=" << g_settings.promptName << "\n";
  out << "prompt.theme=" << g_settings.promptTheme << "\n";
}

inline void apply_settings_to_runtime(){
  g_cwd_mode = g_settings.cwdMode;
}

inline bool settings_get_value(const std::string& key, std::string& value){
  if(key=="prompt.cwd"){
    value = cwdModeToString(g_settings.cwdMode); return true;
  }
  if(key=="completion.ignore_case"){
    value = g_settings.completionIgnoreCase? "true" : "false"; return true;
  }
  if(key=="completion.subsequence"){
    value = g_settings.completionSubsequence? "true" : "false"; return true;
  }
  if(key=="language"){
    value = g_settings.language; return true;
  }
  if(key=="ui.path_error_hint"){
    value = g_settings.showPathErrorHint? "true" : "false"; return true;
  }
  if(key=="message.folder"){
    value = g_settings.messageWatchFolder; return true;
  }
  if(key=="prompt.name"){
    value = g_settings.promptName; return true;
  }
  if(key=="prompt.theme"){
    value = g_settings.promptTheme; return true;
  }
  return false;
}

inline bool settings_set_value(const std::string& key, const std::string& value, std::string& error){
  if(key=="prompt.cwd"){
    CwdMode mode;
    if(!parseCwdMode(value, mode)){
      error = "invalid_value";
      return false;
    }
    g_settings.cwdMode = mode;
    apply_settings_to_runtime();
    return true;
  }
  if(key=="completion.ignore_case"){
    bool b;
    if(!parseBool(value, b)){
      error = "invalid_value";
      return false;
    }
    g_settings.completionIgnoreCase = b;
    return true;
  }
  if(key=="completion.subsequence"){
    bool b;
    if(!parseBool(value, b)){
      error = "invalid_value";
      return false;
    }
    g_settings.completionSubsequence = b;
    return true;
  }
  if(key=="language"){
    if(value.empty()){
      error = "invalid_value";
      return false;
    }
    g_settings.language = value;
    settings_register_language(value);
    return true;
  }
  if(key=="ui.path_error_hint"){
    bool b;
    if(!parseBool(value, b)){
      error = "invalid_value";
      return false;
    }
    g_settings.showPathErrorHint = b;
    return true;
  }
  if(key=="message.folder"){
    g_settings.messageWatchFolder = value;
    message_set_watch_folder(value);
    return true;
  }
  if(key=="prompt.name"){
    g_settings.promptName = value.empty()? "mycli" : value;
    return true;
  }
  if(key=="prompt.theme"){
    std::string t = value;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    if(!(t=="blue" || t=="blue-purple")){
      error = "invalid_value";
      return false;
    }
    g_settings.promptTheme = t;
    return true;
  }
  error = "unknown_key";
  return false;
}

inline std::vector<std::string> settings_list_keys(){
  std::vector<std::string> keys;
  for(const auto& kv : keyInfoMap()) keys.push_back(kv.first);
  std::sort(keys.begin(), keys.end());
  return keys;
}
