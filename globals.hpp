#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <algorithm>
#include <unordered_map>
#include <optional>
#include <array>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <filesystem>

namespace platform {
class TermRaw;
void register_raw_terminal(TermRaw* term);
void unregister_raw_terminal(TermRaw* term);
void suspend_raw_mode();
void resume_raw_mode();

class RawModeScope {
public:
  RawModeScope(){ suspend_raw_mode(); }
  ~RawModeScope(){ resume_raw_mode(); }
  RawModeScope(const RawModeScope&) = delete;
  RawModeScope& operator=(const RawModeScope&) = delete;
};
}

// ===== ANSI =====
namespace ansi {
  inline constexpr const char* CLR   = "\x1b[2K\r";
  inline constexpr const char* RESET = "\x1b[0m";
  inline constexpr const char* WHITE = "\x1b[37m";
  inline constexpr const char* GRAY  = "\x1b[2m";
  inline constexpr const char* GREEN = "\x1b[32m";
  inline constexpr const char* YELLOW= "\x1b[33m";
  inline constexpr const char* RED   = "\x1b[31m";
  inline constexpr const char* BLINK = "\x1b[5m";
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
  auto isSep = [](char ch){ return ch=='/' || ch=='\\'; };
  std::string s = p;
  while(s.size()>1 && isSep(s.back())) s.pop_back();
  size_t pos = s.find_last_of("/\\");
  if(pos==std::string::npos) return s;
  if(pos==s.size()-1) return s;
  return s.substr(pos+1);
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
  bool allowDirectory = true;
  std::vector<std::string> allowedExtensions;
};

struct PositionalArgSpec {
  std::string placeholder;
  bool isPath = false;
  PathKind pathKind = PathKind::Any;
  std::vector<std::string> allowedExtensions;
  bool allowDirectory = true;
  bool inferFromPlaceholder = true;
};

struct SubcommandSpec {
  std::string name;
  std::vector<OptionSpec> options;
  std::vector<PositionalArgSpec> positional;           // e.g. {"<path>", "[<file>]", "<dir...>"}
  std::map<std::string, std::vector<std::string>> mutexGroups;
  std::function<void(const std::vector<std::string>&)> handler;
};

struct ToolSpec {
  std::string name;
  std::string summary;
  std::map<std::string, std::string> summaryLocales;
  std::string help;
  std::map<std::string, std::string> helpLocales;
  std::vector<OptionSpec> options;                     // global/options
  std::vector<PositionalArgSpec> positional;           // command-level positional
  std::vector<SubcommandSpec> subs;                    // subcommands
  std::function<void(const std::vector<std::string>&)> handler;
  bool hidden = false;
  bool requiresExplicitExpose = false;
};

struct ToolExecutionRequest {
  std::vector<std::string> tokens;
  bool silent = false;
  bool forLLM = false;
};

struct ToolExecutionResult {
  int exitCode = 0;
  std::string output;
  std::optional<std::string> display;
  std::optional<std::string> metaJson;
  std::optional<std::string> stderrOutput;

  bool succeeded() const { return exitCode == 0; }
  std::string viewForCli() const { return display.has_value() ? *display : output; }
};

struct MatchResult {
  bool matched = false;
  bool exact = false;
  std::vector<int> positions;
  double score = -1e300;
  int boundaryHits = 0;
  int maxRun = 0;
  int totalGaps = 0;
  int windowSpan = 0;
  int firstIndex = 0;
  int caseMismatch = 0;
  bool isExactEqual = false;
  bool isSubstring = false;
  bool isPrefix = false;
};

struct Candidates {
  std::vector<std::string> items;
  std::vector<std::string> labels;
  std::vector<std::vector<int>> matchPositions;
  std::vector<std::string> annotations;
  std::vector<bool> exactMatches;
  std::vector<MatchResult> matchDetails;
};

using ToolExecutor = std::function<ToolExecutionResult(const ToolExecutionRequest&)>;
using ToolCompletionProvider = std::function<Candidates(const std::string& buffer,
                                                        const std::vector<std::string>& tokens)>;

struct ToolDefinition {
  ToolSpec ui;
  ToolExecutor executor;
  ToolCompletionProvider completion;
};

MatchResult compute_match(const std::string& candidate, const std::string& pattern);
void sortCandidatesByMatch(const std::string& query, Candidates& cand);

struct StatusProvider {
  std::string name;
  std::function<std::string()> render; // 纯文本（样式由 main 添加）
};

bool tool_visible_in_ui(const ToolSpec& spec);

struct ToolRegistry {
  std::map<std::string, ToolDefinition> tools;
  std::vector<StatusProvider> statusProviders;

  void registerTool(const ToolDefinition& def){ tools[def.ui.name] = def; }
  ToolDefinition* find(const std::string& n){
    auto it = tools.find(n); return it==tools.end()? nullptr : &it->second;
  }
  const ToolDefinition* find(const std::string& n) const {
    auto it = tools.find(n); return it==tools.end()? nullptr : &it->second;
  }
  std::vector<std::string> listNames() const {
    std::vector<std::string> r; r.reserve(tools.size());
    for (auto &kv : tools){
      if(!tool_visible_in_ui(kv.second.ui)) continue;
      r.push_back(kv.first);
    }
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
Candidates pathCandidatesForWord(const std::string& fullBuf,
                                 const std::string& word,
                                 PathKind kind = PathKind::Any,
                                 const std::vector<std::string>* extensions = nullptr,
                                 bool allowDirectories = true);
std::string renderCommandGhost(const ToolSpec& spec, const std::vector<std::string>& toks);
std::string renderSubGhost(const ToolSpec& parent, const SubcommandSpec& sub,
                           const std::vector<std::string>& toks, size_t subIdx,
                           const std::set<std::string>& used);

void register_all_tools();
void register_status_providers();
void register_tools_from_config(const std::string& path);
bool tool_accessible_to_user(const ToolSpec& spec, bool forLLM);
bool agent_tools_exposed();
void agent_indicator_set_running();
void agent_indicator_set_finished();
void agent_indicator_mark_acknowledged();
void agent_indicator_clear();

// ===== Settings support =====
enum class MatchMode { Prefix, Subsequence };

enum class SubsequenceStrategy {
  Ranked,
  Greedy,
};

struct MemoryConfig {
  bool enabled = true;
  std::string root;
  std::string indexFile;
  std::string personalSubdir = "personal";
  std::string summaryLang;
  int summaryMinLen = 50;
  int summaryMaxLen = 100;
  int maxBootstrapDepth = 1;
};

struct AppSettings {
  CwdMode cwdMode = CwdMode::Full;
  bool completionIgnoreCase = false;
  bool completionSubsequence = false;
  SubsequenceStrategy completionSubsequenceStrategy = SubsequenceStrategy::Ranked;
  std::string language = "en";
  bool showPathErrorHint = true;
  std::string messageWatchFolder = "./message";
  std::string promptName = "mycli";
  std::string promptTheme = "blue";
  std::map<std::string, std::string> promptThemeArtPaths;
  bool promptInputEllipsisEnabled = false;
  int  promptInputEllipsisLeftWidth = 30;
  bool promptInputEllipsisRightWidthAuto = true;
  int  promptInputEllipsisRightWidth = 0;
  int  historyRecentLimit = 10;
  std::string configHome;
  bool agentExposeFsTools = false;
  MemoryConfig memory;
};

extern AppSettings g_settings;

inline std::optional<std::array<int, 6>> theme_gradient_colors(const std::string& theme){
  if(theme == "blue-purple"){
    return std::array<int, 6>{0, 153, 255, 128, 0, 255};
  }
  if(theme == "red-yellow"){
    return std::array<int, 6>{255, 102, 102, 255, 221, 51};
  }
  if(theme == "purple-orange"){
    return std::array<int, 6>{162, 70, 255, 255, 140, 66};
  }
  return std::nullopt;
}

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
std::string localized_tool_help(const ToolSpec& spec);
void set_tool_summary_locale(ToolSpec& spec, const std::string& lang, const std::string& value);
void set_tool_help_locale(ToolSpec& spec, const std::string& lang, const std::string& value);
const std::string& settings_file_path();
const std::string& config_home();
bool set_config_home(const std::string& path, std::string& error);
std::filesystem::path cli_root_directory();

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
struct PromptIndicatorDescriptor {
  std::string id;
  std::string text;
  std::string bracketColor = ansi::WHITE;
};

struct PromptIndicatorState {
  bool visible = false;
  std::string text;
  std::string textColor = ansi::WHITE;
  std::string bracketColor = ansi::WHITE;
};

void register_prompt_indicator(const PromptIndicatorDescriptor& desc);
void update_prompt_indicator(const std::string& id, const PromptIndicatorState& state);
PromptIndicatorState prompt_indicator_current(const std::string& id);

void llm_set_pending(bool pending);

void memory_import_indicator_begin();
void memory_import_indicator_complete();
void memory_import_indicator_mark_seen();

void agent_indicator_guard_alert_inc();
void agent_indicator_guard_alert_dec();
void agent_monitor_set_active(bool active);

ToolExecutionResult invoke_registered_tool(const std::string& line, bool silent = true);

// ===== LLM watcher =====
void llm_initialize();
void llm_poll();
bool llm_has_unread();
void llm_mark_seen();

// ===== Command history =====
void history_record_command(const std::string& command);
const std::vector<std::string>& history_recent_commands();
void history_apply_limit();
