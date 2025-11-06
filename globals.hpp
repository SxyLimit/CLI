#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <algorithm>
#include <unordered_map>
#include <optional>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>

// ===== ANSI =====
namespace ansi {
  inline constexpr const char* CLR   = "\x1b[2K\r";
  inline constexpr const char* RESET = "\x1b[0m";
  inline constexpr const char* WHITE = "\x1b[37m";
  inline constexpr const char* GRAY  = "\x1b[2m";
  inline constexpr const char* GREEN = "\x1b[32m";
  inline constexpr const char* YELLOW= "\x1b[33m";
  inline constexpr const char* RED   = "\x1b[31m";
  inline constexpr const char* CYAN  = "\x1b[36m";
  inline constexpr const char* BOLD  = "\x1b[1m";
  inline constexpr const char* DIM   = "\x1b[2m";
  inline constexpr const char* CUU   = "\x1b[";
  inline constexpr const char* CHA   = "\x1b[";
}

// ===== Common helpers =====
inline bool startsWith(const std::string& s, const std::string& pre){ return s.rfind(pre,0)==0; }
inline std::vector<std::string> splitTokens(const std::string& s){
  std::vector<std::string> v; std::string cur;
  for(char c: s){ if(std::isspace((unsigned char)c)){ if(!cur.empty()){ v.push_back(cur); cur.clear(); } } else cur.push_back(c); }
  if(!cur.empty()) v.push_back(cur); return v;
}
inline std::string join(const std::vector<std::string>& v, const std::string& sep=" "){
  std::string o; for(size_t i=0;i<v.size();++i){ if(i) o+=sep; o+=v[i]; } return o;
}
inline bool isDirFS(const std::string& path){ struct stat st{}; return ::stat(path.c_str(),&st)==0 && S_ISDIR(st.st_mode); }
inline std::string basenameOf(const std::string& p){
  if(p.empty()) return p;
  std::string s=p; while(s.size()>1 && s.back()=='/') s.pop_back();
  size_t pos=s.find_last_of('/'); if(pos==std::string::npos) return s;
  if(pos==s.size()-1) return s; return s.substr(pos+1);
}

// last word split
struct SplitWord { std::string before; std::string word; };
inline SplitWord splitLastWord(const std::string& buf){
  size_t p = buf.find_last_of(" \t");
  if(p==std::string::npos) return {"", buf};
  return {buf.substr(0,p+1), buf.substr(p+1)};
}

// ===== Specs & Registry =====
enum class PathKind { Any, File, Dir };

struct OptionSpec {
  std::string name;
  bool takesValue = false;
  std::vector<std::string> valueSuggestions;
  std::function<std::vector<std::string>(const std::vector<std::string>&)> dynamicValues;
  bool required = false;
  std::string placeholder;
  bool isPath = false;
  PathKind pathKind = PathKind::Any;
};

struct SubcommandSpec {
  std::string name;
  std::vector<OptionSpec> options;
  std::vector<std::string> positional;                 // e.g. {"<path>", "[<file>]", "<dir...>"}
  std::map<std::string, std::vector<std::string>> mutexGroups;
  std::function<void(const std::vector<std::string>&)> handler;
};

struct ToolSpec {
  std::string name;
  std::string summary;
  std::map<std::string, std::string> summaryLocales;
  std::vector<OptionSpec> options;                     // global/options
  std::vector<std::string> positional;                 // command-level positional
  std::vector<SubcommandSpec> subs;                    // subcommands
  std::function<void(const std::vector<std::string>&)> handler;
};

struct Candidates {
  std::vector<std::string> items;
  std::vector<std::string> labels;
  std::vector<std::vector<int>> matchPositions;
  std::vector<std::string> annotations;
};

struct MatchResult {
  bool matched = false;
  std::vector<int> positions;
};

MatchResult compute_match(const std::string& candidate, const std::string& pattern);

struct StatusProvider {
  std::string name;
  std::function<std::string()> render; // 纯文本（样式由 main 添加）
};

struct ToolRegistry {
  std::map<std::string, ToolSpec> tools;
  std::vector<StatusProvider> statusProviders;

  void registerTool(const ToolSpec& t){ tools[t.name] = t; }
  const ToolSpec* find(const std::string& n) const {
    auto it = tools.find(n); return it==tools.end()? nullptr : &it->second;
  }
  std::vector<std::string> listNames() const {
    std::vector<std::string> r; r.reserve(tools.size());
    for (auto &kv : tools) r.push_back(kv.first);
    std::sort(r.begin(), r.end()); return r;
  }
  void registerStatusProvider(const StatusProvider& sp){ statusProviders.push_back(sp); }
  std::string renderStatusPrefix() const {
    std::string out; for (auto &sp : statusProviders) { try{ if(sp.render) out += sp.render(); }catch(...){} } return out;
  }
};

// ===== Global state (defined in main.cpp) =====
enum class CwdMode { Full, Omit, Hidden };
extern ToolRegistry REG;
extern CwdMode      g_cwd_mode;
extern bool         g_should_exit;
extern std::string  g_parse_error_cmd;

// ===== Shared decls (inline impl in tools.hpp) =====
Candidates pathCandidatesForWord(const std::string& fullBuf, const std::string& word, PathKind kind = PathKind::Any);
std::string renderCommandGhost(const ToolSpec& spec, const std::vector<std::string>& toks);
std::string renderSubGhost(const ToolSpec& parent, const SubcommandSpec& sub,
                           const std::vector<std::string>& toks, size_t subIdx,
                           const std::set<std::string>& used);

void register_all_tools();
void register_status_providers();
void register_tools_from_config(const std::string& path);

// ===== Settings support =====
enum class MatchMode { Prefix, Subsequence };

struct AppSettings {
  CwdMode cwdMode = CwdMode::Full;
  bool completionIgnoreCase = false;
  bool completionSubsequence = false;
  std::string language = "en";
  bool showPathErrorHint = true;
  std::string messageWatchFolder = "./message";
  std::string promptName = "mycli";
  std::string promptTheme = "blue";
  std::string configHome;
};

extern AppSettings g_settings;

void load_settings(const std::string& path);
void save_settings(const std::string& path);
void apply_settings_to_runtime();

bool settings_get_value(const std::string& key, std::string& value);
bool settings_set_value(const std::string& key, const std::string& value, std::string& error);
std::vector<std::string> settings_list_keys();

// ===== Localization =====
std::string tr(const std::string& key);
std::string trFmt(const std::string& key, const std::map<std::string, std::string>& values);
std::string localized_tool_summary(const ToolSpec& spec);
void set_tool_summary_locale(ToolSpec& spec, const std::string& lang, const std::string& value);
const std::string& settings_file_path();
const std::string& config_home();
bool set_config_home(const std::string& path, std::string& error);

// ===== Message watcher =====
struct MessageFileInfo {
  std::string path;
  std::time_t modifiedAt = 0;
  bool isNew = false;
  bool isUnread = false;
};

void message_set_watch_folder(const std::string& path);
const std::string& message_watch_folder();
void message_poll();
bool message_has_unread();
std::vector<std::string> message_peek_unread();
std::vector<std::string> message_consume_unread();
std::vector<MessageFileInfo> message_all_files();
std::vector<MessageFileInfo> message_pending_files();
bool message_mark_read(const std::string& path);
std::optional<std::string> message_resolve_label(const std::string& label);
std::vector<std::string> message_all_file_labels();

// ===== Prompt badges =====
struct PromptBadge {
  std::string id;
  char letter = 0;
  std::function<bool()> active;
};

void register_prompt_badge(const PromptBadge& badge);

// ===== LLM watcher =====
void llm_initialize();
void llm_poll();
bool llm_has_unread();
void llm_mark_seen();
