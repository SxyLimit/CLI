#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#endif
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <clocale>
#include <codecvt>
#include <cwchar>
#include <fstream>
#include <locale>
#include <stdexcept>
#include <cctype>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <optional>
#include <unordered_map>
#include <filesystem>
#include <numeric>
#include <type_traits>
#include <cmath>
#include <limits>

#include "globals.hpp"
#include "tools.hpp"
#include "settings.hpp"

namespace platform {
#ifdef _WIN32

class TermRaw {
  HANDLE hIn = INVALID_HANDLE_VALUE;
  DWORD origMode = 0;
  bool active = false;
public:
  void enable(){
    hIn = GetStdHandle(STD_INPUT_HANDLE);
    if(hIn == INVALID_HANDLE_VALUE) std::exit(1);
    if(!GetConsoleMode(hIn, &origMode)) std::exit(1);
    DWORD mode = origMode;
    mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    if(!SetConsoleMode(hIn, mode)) std::exit(1);
    active = true;
  }
  void disable(){
    if(active){
      SetConsoleMode(hIn, origMode);
      active = false;
    }
  }
  ~TermRaw(){ disable(); }
};

inline void ensure_virtual_terminal_output(){
  static bool initialized = false;
  if(initialized) return;
  initialized = true;

  // Ensure the console understands UTF-8 sequences.
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
  _setmode(_fileno(stdin), _O_BINARY);

  auto enable_virtual_terminal = [](DWORD stdHandle){
    HANDLE handle = GetStdHandle(stdHandle);
    if(handle == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if(!GetConsoleMode(handle, &mode)) return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(handle, mode);
  };

  enable_virtual_terminal(STD_OUTPUT_HANDLE);
  enable_virtual_terminal(STD_ERROR_HANDLE);
}

inline int wait_for_input(int timeout_ms){
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  DWORD rc = WaitForSingleObject(hIn, timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms));
  if(rc == WAIT_TIMEOUT) return 0;
  if(rc == WAIT_OBJECT_0) return 1;
  return -1;
}

inline bool read_char(char &ch){
  DWORD read = 0;
  if(!ReadFile(GetStdHandle(STD_INPUT_HANDLE), &ch, 1, &read, nullptr)) return false;
  return read == 1;
}

inline void write_stdout(const char* data, size_t len){
  DWORD written = 0;
  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), data, static_cast<DWORD>(len), &written, nullptr);
}

inline void flush_stdout(){
  _commit(_fileno(stdout));
}

inline bool env_var_exists(const std::string& key){
  DWORD length = GetEnvironmentVariableA(key.c_str(), nullptr, 0);
  if(length == 0){
    DWORD err = GetLastError();
    return err != ERROR_ENVVAR_NOT_FOUND;
  }
  return true;
}

inline void set_env(const std::string& key, const std::string& value, bool overwrite){
  if(!overwrite && env_var_exists(key)) return;
  _putenv_s(key.c_str(), value.c_str());
}

#else

class TermRaw {
  termios orig{};
  bool active = false;
public:
  void enable(){
    if(tcgetattr(STDIN_FILENO, &orig) == -1) std::exit(1);
    termios raw = orig;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) std::exit(1);
    active = true;
  }
  void disable(){
    if(active){
      tcsetattr(STDIN_FILENO, TCSANOW, &orig);
      active = false;
    }
  }
  ~TermRaw(){ disable(); }
};

inline void ensure_virtual_terminal_output(){}

inline int wait_for_input(int timeout_ms){
  struct pollfd pfd{STDIN_FILENO, POLLIN, 0};
  int rc = ::poll(&pfd, 1, timeout_ms);
  if(rc <= 0) return rc;
  if(!(pfd.revents & POLLIN)) return 0;
  return 1;
}

inline bool read_char(char &ch){
  ssize_t n = ::read(STDIN_FILENO, &ch, 1);
  return n == 1;
}

inline void write_stdout(const char* data, size_t len){
  ::write(STDOUT_FILENO, data, len);
}

inline void flush_stdout(){
  ::fsync(STDOUT_FILENO);
}

inline bool env_var_exists(const std::string& key){
  return ::getenv(key.c_str()) != nullptr;
}

inline void set_env(const std::string& key, const std::string& value, bool overwrite){
  ::setenv(key.c_str(), value.c_str(), overwrite ? 1 : 0);
}

#endif
} // namespace platform

// ===== Global state definitions =====
ToolRegistry REG;
CwdMode      g_cwd_mode = CwdMode::Full;
bool         g_should_exit = false;
std::string  g_parse_error_cmd;

static std::string g_config_home;
static bool g_config_home_initialized = false;

static std::string trim_copy(const std::string& s){
  size_t a = 0, b = s.size();
  while(a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while(b > a && std::isspace(static_cast<unsigned char>(s[b-1]))) --b;
  return s.substr(a, b - a);
}

static void load_env_overrides(){
  static bool loaded = false;
  if(loaded) return;
  loaded = true;
  std::ifstream in(".env");
  if(!in.good()) return;
  std::string line;
  while(std::getline(in, line)){
    std::string stripped = trim_copy(line);
    if(stripped.empty() || stripped[0] == '#') continue;
    auto eq = stripped.find('=');
    if(eq == std::string::npos) continue;
    std::string key = trim_copy(stripped.substr(0, eq));
    std::string value = trim_copy(stripped.substr(eq + 1));
    if(key.empty()) continue;
    if(!platform::env_var_exists(key)){
      platform::set_env(key, value, false);
    }
  }
}

static void ensure_config_home_initialized(){
  if(g_config_home_initialized) return;
  g_config_home_initialized = true;
  load_env_overrides();
  std::string homeOverride;
  if(const char* env = ::getenv("HOME_PATH"); env && *env){
    homeOverride = env;
  }
  if(homeOverride.empty()){
    homeOverride = "./settings";
  }
  std::filesystem::path p(homeOverride);
  if(p.is_relative()){
    p = std::filesystem::absolute(p);
  }
  p = p.lexically_normal();
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  g_config_home = p.string();
}

const std::string& config_home(){
  ensure_config_home_initialized();
  return g_config_home;
}

static std::string config_file_path(const std::string& name){
  std::filesystem::path full = std::filesystem::path(config_home()) / name;
  return std::filesystem::absolute(full).string();
}

const std::string& settings_file_path(){
  static std::string path;
  path = config_file_path("mycli_settings.conf");
  return path;
}

static std::unordered_map<std::string, std::map<std::string, std::string>> g_i18n = {
  {"show_usage",         {{"en", "usage: show [LICENSE|MyCLI]"}, {"zh", "用法：show [LICENSE|MyCLI]"}}},
  {"show_license_error", {{"en", "Failed to read LICENSE file."}, {"zh", "读取 LICENSE 文件失败。"}}},
  {"show_mycli_version", {{"en", "CLI Demo v0.0.1"}, {"zh", "CLI 演示 v0.0.1"}}},
  {"setting_get_usage",   {{"en", "usage: setting get [path...]"}, {"zh", "用法：setting get [路径...]"}}},
  {"setting_unknown_key", {{"en", "unknown setting key: {key}"}, {"zh", "未知设置项：{key}"}}},
  {"setting_get_value",   {{"en", "setting {key} = {value}"}, {"zh", "设置 {key} = {value}"}}},
  {"setting_set_usage",   {{"en", "usage: setting set <key> <value>"}, {"zh", "用法：setting set <key> <value>"}}},
  {"setting_invalid_value", {{"en", "invalid value for {key}: {value}"}, {"zh", "设置 {key} 的值无效：{value}"}}},
  {"setting_set_success", {{"en", "updated {key} -> {value}"}, {"zh", "已更新 {key} -> {value}"}}},
  {"setting_list_header", {{"en", "Available setting keys:"}, {"zh", "可用设置项："}}},
  {"setting_usage",       {{"en", "usage: setting <get|set>"}, {"zh", "用法：setting <get|set>"}}},
  {"cd_mode_updated",    {{"en", "prompt cwd mode set to {mode}"}, {"zh", "提示符目录模式已设为 {mode}"}}},
  {"cd_mode_error",      {{"en", "failed to update prompt mode"}, {"zh", "更新提示符模式失败"}}},
  {"cd_usage",           {{"en", "usage: cd <path> | cd -o [-a|-c]"}, {"zh", "用法：cd <path> | cd -o [-a|-c]"}}},
  {"mode.full",          {{"en", "full"}, {"zh", "完整"}}},
  {"mode.omit",          {{"en", "omit"}, {"zh", "仅名称"}}},
  {"mode.hidden",        {{"en", "hidden"}, {"zh", "隐藏"}}},
  {"help_available_commands", {{"en", "Available commands:"}, {"zh", "可用命令："}}},
  {"help_command_summary",   {{"en", "  help  - Show command help"}, {"zh", "  help  - 显示命令帮助"}}},
  {"help_use_command",       {{"en", "Use: help <command> to see details."}, {"zh", "使用：help <command> 查看详情。"}}},
  {"help_no_such_command",   {{"en", "No such command: {name}"}, {"zh", "没有名为 {name} 的命令"}}},
  {"help_subcommands",       {{"en", "  subcommands:"}, {"zh", "  子命令："}}},
  {"help_options",           {{"en", "  options:"}, {"zh", "  选项："}}},
  {"help_positional",        {{"en", "  positional: {value}"}, {"zh", "  位置参数：{value}"}}},
  {"help_required_tag",      {{"en", " (required)"}, {"zh", "（必填）"}}},
  {"help_path_tag",          {{"en", " (path)"}, {"zh", "（路径）"}}},
  {"help_usage_line",        {{"en", "usage: {value}"}, {"zh", "用法：{value}"}}},
  {"unknown_command",        {{"en", "unknown command: {name}"}, {"zh", "未知命令：{name}"}}},
  {"path_error_missing",     {{"en", "missing"}, {"zh", "不存在"}}},
  {"path_error_need_dir",    {{"en", "needs directory"}, {"zh", "需要目录"}}},
  {"path_error_need_file",   {{"en", "needs file"}, {"zh", "需要文件"}}},
  {"path_error_need_extension", {{"en", "needs extension: {ext}"}, {"zh", "需要后缀：{ext}"}}}
};

static std::vector<std::string> positionalPlaceholders(const std::vector<PositionalArgSpec>& specs){
  std::vector<std::string> out;
  out.reserve(specs.size());
  for(const auto& spec : specs){
    out.push_back(spec.placeholder);
  }
  return out;
}

static std::string joinPositionalPlaceholders(const std::vector<PositionalArgSpec>& specs){
  return join(positionalPlaceholders(specs));
}

static std::vector<std::string> normalizeExtensions(const std::vector<std::string>& exts){
  std::vector<std::string> normalized;
  normalized.reserve(exts.size());
  for(const auto& ext : exts){
    if(ext.empty()) continue;
    std::string norm = ext;
    if(norm.front() != '.') norm.insert(norm.begin(), '.');
    std::transform(norm.begin(), norm.end(), norm.begin(), [](unsigned char c){
      return static_cast<char>(std::tolower(c));
    });
    if(!norm.empty()) normalized.push_back(norm);
  }
  std::sort(normalized.begin(), normalized.end());
  normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
  return normalized;
}

struct MessageWatcherState {
  std::string folder;
  std::map<std::string, std::time_t> known;
  std::map<std::string, std::time_t> seen;
};

static MessageWatcherState g_message_watcher;

struct PromptIndicatorEntry {
  PromptIndicatorDescriptor desc;
  PromptIndicatorState state;
};

static std::vector<std::string> g_prompt_indicator_order;
static std::map<std::string, PromptIndicatorEntry> g_prompt_indicators;

struct LlmWatcherState {
  std::string path;
  std::time_t knownMtime = 0;
  std::time_t seenMtime = 0;
  off_t knownSize = 0;
  off_t seenSize = 0;
  bool initialized = false;
};

static LlmWatcherState g_llm_watcher;
static bool g_llm_pending = false;

static std::string joinPath(const std::string& dir, const std::string& name){
  if(dir.empty()) return name;
  if(dir.back()=='/') return dir + name;
  return dir + "/" + name;
}

static bool isMarkdownFile(const dirent* entry){
  if(!entry) return false;
  if(entry->d_name[0]=='.' && (!entry->d_name[1] || (entry->d_name[1]=='.' && !entry->d_name[2]))) return false;
  std::string name = entry->d_name;
  if(name.size()<3) return false;
  auto pos = name.find_last_of('.');
  if(pos==std::string::npos) return false;
  std::string ext = name.substr(pos+1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
  return ext=="md";
}

static std::vector<std::pair<std::string, std::time_t>> collectMarkdownFiles(const std::string& folder){
  std::vector<std::pair<std::string, std::time_t>> files;
  if(folder.empty()) return files;
  DIR* d = ::opendir(folder.c_str());
  if(!d) return files;
  while(dirent* e = ::readdir(d)){
    if(!isMarkdownFile(e)) continue;
    std::string full = joinPath(folder, e->d_name);
    struct stat st{};
    if(::stat(full.c_str(), &st)==0 && S_ISREG(st.st_mode)){
      files.emplace_back(full, st.st_mtime);
    }
  }
  ::closedir(d);
  std::sort(files.begin(), files.end(), [](const auto& a, const auto& b){
    if(a.second==b.second) return a.first < b.first;
    return a.second < b.second;
  });
  return files;
}

void message_set_watch_folder(const std::string& path){
  g_message_watcher.folder = path;
  g_message_watcher.known.clear();
  g_message_watcher.seen.clear();
  if(path.empty()) return;
  auto files = collectMarkdownFiles(path);
  for(const auto& item : files){
    g_message_watcher.known[item.first] = item.second;
    g_message_watcher.seen[item.first] = item.second;
  }
}

const std::string& message_watch_folder(){
  return g_message_watcher.folder;
}

void message_poll(){
  if(g_message_watcher.folder.empty()) return;
  auto files = collectMarkdownFiles(g_message_watcher.folder);
  std::map<std::string, std::time_t> current;
  for(const auto& item : files){
    current[item.first] = item.second;
  }
  for(auto it = g_message_watcher.seen.begin(); it != g_message_watcher.seen.end(); ){
    if(current.find(it->first)==current.end()) it = g_message_watcher.seen.erase(it);
    else ++it;
  }
  g_message_watcher.known = std::move(current);
  bool unread = message_has_unread();
  PromptIndicatorState state = prompt_indicator_current("message");
  state.visible = unread;
  state.textColor = unread ? ansi::RED : ansi::WHITE;
  update_prompt_indicator("message", state);
}

bool message_has_unread(){
  for(const auto& kv : g_message_watcher.known){
    auto it = g_message_watcher.seen.find(kv.first);
    std::time_t seen = (it==g_message_watcher.seen.end())? 0 : it->second;
    if(seen != kv.second) return true;
  }
  return false;
}

std::vector<std::string> message_peek_unread(){
  std::vector<std::string> out;
  auto pending = message_pending_files();
  for(const auto& info : pending){
    out.push_back(info.path);
  }
  return out;
}

std::vector<std::string> message_consume_unread(){
  std::vector<std::string> out;
  auto pending = message_pending_files();
  for(const auto& info : pending){
    out.push_back(info.path);
    message_mark_read(info.path);
  }
  return out;
}

std::vector<MessageFileInfo> message_all_files(){
  std::vector<MessageFileInfo> files;
  for(const auto& kv : g_message_watcher.known){
    MessageFileInfo info;
    info.path = kv.first;
    info.modifiedAt = kv.second;
    auto it = g_message_watcher.seen.find(kv.first);
    if(it==g_message_watcher.seen.end()){
      info.isUnread = true;
      info.isNew = true;
    }else{
      info.isUnread = (it->second != kv.second);
      info.isNew = false;
    }
    files.push_back(std::move(info));
  }
  std::sort(files.begin(), files.end(), [](const MessageFileInfo& a, const MessageFileInfo& b){
    if(a.modifiedAt == b.modifiedAt) return a.path < b.path;
    return a.modifiedAt > b.modifiedAt;
  });
  return files;
}

std::vector<MessageFileInfo> message_pending_files(){
  std::vector<MessageFileInfo> pending;
  auto all = message_all_files();
  for(auto& info : all){
    if(info.isUnread) pending.push_back(info);
  }
  return pending;
}

bool message_mark_read(const std::string& path){
  auto it = g_message_watcher.known.find(path);
  if(it==g_message_watcher.known.end()) return false;
  g_message_watcher.seen[path] = it->second;
  bool unread = message_has_unread();
  PromptIndicatorState state = prompt_indicator_current("message");
  state.visible = unread;
  state.textColor = unread ? ansi::RED : ansi::WHITE;
  update_prompt_indicator("message", state);
  return true;
}

std::optional<std::string> message_resolve_label(const std::string& label){
  if(label.empty()) return std::nullopt;
  auto all = message_all_files();
  for(const auto& info : all){
    if(info.path == label) return info.path;
  }
  std::vector<std::string> matches;
  matches.reserve(all.size());
  for(const auto& info : all){
    if(basenameOf(info.path) == label) matches.push_back(info.path);
  }
  if(matches.size()==1) return matches.front();
  if(matches.empty()){
    const std::string& folder = message_watch_folder();
    if(!folder.empty()){
      std::string candidate = joinPath(folder, label);
      for(const auto& info : all){
        if(info.path == candidate) return info.path;
      }
    }
  }
  return std::nullopt;
}

std::vector<std::string> message_all_file_labels(){
  std::vector<std::string> labels;
  std::set<std::string> seen;
  for(const auto& info : message_all_files()){
    std::string name = basenameOf(info.path);
    if(seen.insert(name).second){
      labels.push_back(std::move(name));
    }
  }
  return labels;
}

void register_prompt_indicator(const PromptIndicatorDescriptor& desc){
  if(desc.id.empty()) return;
  PromptIndicatorEntry entry;
  entry.desc = desc;
  entry.state.text = desc.text;
  entry.state.bracketColor = desc.bracketColor;
  auto inserted = g_prompt_indicators.emplace(desc.id, entry);
  if(inserted.second){
    g_prompt_indicator_order.push_back(desc.id);
  }else{
    inserted.first->second.desc = desc;
    inserted.first->second.state.text = desc.text;
    inserted.first->second.state.bracketColor = desc.bracketColor;
  }
}

void update_prompt_indicator(const std::string& id, const PromptIndicatorState& state){
  auto it = g_prompt_indicators.find(id);
  if(it == g_prompt_indicators.end()) return;
  PromptIndicatorState next = state;
  if(next.text.empty()) next.text = it->second.desc.text;
  if(next.bracketColor.empty()) next.bracketColor = it->second.desc.bracketColor;
  it->second.state = next;
}

PromptIndicatorState prompt_indicator_current(const std::string& id){
  auto it = g_prompt_indicators.find(id);
  if(it == g_prompt_indicators.end()) return PromptIndicatorState{};
  PromptIndicatorState state = it->second.state;
  if(state.text.empty()) state.text = it->second.desc.text;
  if(state.bracketColor.empty()) state.bracketColor = it->second.desc.bracketColor;
  return state;
}

static std::string resolve_llm_history_path(){
  if(!g_llm_watcher.path.empty()) return g_llm_watcher.path;
  return config_file_path("mycli_llm_history.json");
}

void llm_initialize(){
  if(g_llm_watcher.initialized) return;
  g_llm_watcher.path = resolve_llm_history_path();
  g_llm_watcher.initialized = true;
  struct stat st{};
  if(!g_llm_watcher.path.empty() && ::stat(g_llm_watcher.path.c_str(), &st)==0){
    g_llm_watcher.knownMtime = g_llm_watcher.seenMtime = st.st_mtime;
    g_llm_watcher.knownSize = g_llm_watcher.seenSize = st.st_size;
  }else{
    g_llm_watcher.knownMtime = g_llm_watcher.seenMtime = 0;
    g_llm_watcher.knownSize = g_llm_watcher.seenSize = 0;
  }
}

void llm_poll(){
  if(!g_llm_watcher.initialized) llm_initialize();
  if(g_llm_watcher.path.empty()) return;
  struct stat st{};
  if(::stat(g_llm_watcher.path.c_str(), &st)==0){
    g_llm_watcher.knownMtime = st.st_mtime;
    g_llm_watcher.knownSize = st.st_size;
  }else{
    g_llm_watcher.knownMtime = 0;
    g_llm_watcher.knownSize = 0;
    g_llm_watcher.seenMtime = 0;
    g_llm_watcher.seenSize = 0;
  }
  if(llm_has_unread()){
    g_llm_pending = false;
  }
  bool unread = llm_has_unread();
  PromptIndicatorState state = prompt_indicator_current("llm");
  state.visible = unread || g_llm_pending;
  if(unread){
    state.textColor = ansi::RED;
  }else if(g_llm_pending){
    state.textColor = ansi::YELLOW;
  }else{
    state.textColor = ansi::WHITE;
  }
  update_prompt_indicator("llm", state);
}

bool llm_has_unread(){
  if(!g_llm_watcher.initialized) llm_initialize();
  return g_llm_watcher.knownMtime != g_llm_watcher.seenMtime ||
         g_llm_watcher.knownSize != g_llm_watcher.seenSize;
}

void llm_mark_seen(){
  if(!g_llm_watcher.initialized) llm_initialize();
  g_llm_watcher.seenMtime = g_llm_watcher.knownMtime;
  g_llm_watcher.seenSize = g_llm_watcher.knownSize;
}

void llm_set_pending(bool pending){
  g_llm_pending = pending;
  bool unread = llm_has_unread();
  PromptIndicatorState state = prompt_indicator_current("llm");
  state.visible = unread || g_llm_pending;
  if(unread){
    state.textColor = ansi::RED;
  }else if(g_llm_pending){
    state.textColor = ansi::YELLOW;
  }else{
    state.textColor = ansi::WHITE;
  }
  update_prompt_indicator("llm", state);
}

static void persist_home_path_to_env(const std::string& path){
  std::ifstream in(".env");
  std::vector<std::string> lines;
  bool found = false;
  if(in.good()){
    std::string line;
    while(std::getline(in, line)){
      std::string stripped = trim_copy(line);
      auto eq = stripped.find('=');
      if(stripped.empty() || stripped[0]=='#' || eq==std::string::npos){
        lines.push_back(line);
        continue;
      }
      std::string key = trim_copy(stripped.substr(0, eq));
      if(key == "HOME_PATH"){
        lines.push_back(std::string("HOME_PATH=") + path);
        found = true;
      }else{
        lines.push_back(line);
      }
    }
  }
  if(!found){
    lines.push_back(std::string("HOME_PATH=") + path);
  }
  std::ofstream out(".env", std::ios::trunc);
  if(!out.good()) return;
  for(size_t i=0;i<lines.size();++i){
    out << lines[i] << "\n";
  }
}

bool set_config_home(const std::string& path, std::string& error){
  std::string trimmed = trim_copy(path);
  if(trimmed.empty()){
    error = "invalid_value";
    return false;
  }
  std::filesystem::path p(trimmed);
  if(p.is_relative()) p = std::filesystem::absolute(p);
  p = p.lexically_normal();
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  if(ec){
    error = "fs_error";
    return false;
  }
  ensure_config_home_initialized();
  std::filesystem::path oldPath(config_home());
  std::filesystem::path newPath = p;
  std::string newPathStr = newPath.string();
  if(oldPath == newPath){
    platform::set_env("HOME_PATH", newPathStr, true);
    persist_home_path_to_env(newPathStr);
    return true;
  }
  auto move_file = [&](const std::string& name){
    std::filesystem::path from = oldPath / name;
    std::filesystem::path to = newPath / name;
    if(!std::filesystem::exists(from)) return true;
    if(std::filesystem::exists(to)) return true;
    std::error_code moveEc;
    std::filesystem::create_directories(to.parent_path(), moveEc);
    moveEc.clear();
    std::filesystem::rename(from, to, moveEc);
    if(moveEc){
      std::ifstream src(from, std::ios::binary);
      std::ofstream dst(to, std::ios::binary);
      if(!src.good() || !dst.good()){
        return false;
      }
      dst << src.rdbuf();
      src.close();
      dst.close();
      std::filesystem::remove(from, moveEc);
    }
    return true;
  };
  if(!move_file("mycli_settings.conf") ||
     !move_file("mycli_tools.conf") ||
     !move_file("mycli_llm_history.json")){
    error = "fs_error";
    return false;
  }
  g_config_home = newPathStr;
  platform::set_env("HOME_PATH", newPathStr, true);
  persist_home_path_to_env(newPathStr);
  g_llm_watcher.initialized = false;
  g_llm_watcher.path.clear();
  llm_initialize();
  return true;
}

static size_t utf8CharLength(unsigned char lead){
  if(lead < 0x80) return 1;
  if((lead & 0xE0) == 0xC0) return 2;
  if((lead & 0xF0) == 0xE0) return 3;
  if((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

static void utf8PopBack(std::string& text){
  if(text.empty()) return;
  size_t i = text.size();
  while(i > 0){
    --i;
    unsigned char byte = static_cast<unsigned char>(text[i]);
    if((byte & 0xC0) != 0x80){
      text.erase(i);
      return;
    }
  }
  text.clear();
}

static std::string promptNamePlain(){
  if(g_settings.promptName.empty()) return std::string("mycli");
  return g_settings.promptName;
}

static std::string plainPromptText(){
  return promptNamePlain() + "> ";
}

struct PromptIndicatorRender {
  std::string plain;
  std::string colored;
};

static PromptIndicatorRender promptIndicatorsRender(){
  PromptIndicatorRender render;
  bool any = false;
  std::string bracketColor = ansi::WHITE;
  for(const auto& id : g_prompt_indicator_order){
    auto it = g_prompt_indicators.find(id);
    if(it == g_prompt_indicators.end()) continue;
    PromptIndicatorState state = prompt_indicator_current(id);
    if(!state.visible) continue;
    std::string text = state.text;
    if(text.empty()) continue;
    if(!any){
      bracketColor = state.bracketColor.empty()? ansi::WHITE : state.bracketColor;
      render.plain.push_back('[');
      render.colored += bracketColor;
      render.colored.push_back('[');
      render.colored += ansi::RESET;
      any = true;
    }
    std::string color = state.textColor.empty()? ansi::WHITE : state.textColor;
    render.plain += text;
    render.colored += color;
    render.colored += text;
    render.colored += ansi::RESET;
  }
  if(any){
    render.plain.push_back(']');
    render.colored += bracketColor;
    render.colored.push_back(']');
    render.colored += ansi::RESET;
  }
  return render;
}

void set_tool_summary_locale(ToolSpec& spec, const std::string& lang, const std::string& value){
  spec.summaryLocales[lang] = value;
  if(lang=="en" && spec.summary.empty()) spec.summary = value;
}

void set_tool_help_locale(ToolSpec& spec, const std::string& lang, const std::string& value){
  spec.helpLocales[lang] = value;
  if(lang=="en" && spec.help.empty()) spec.help = value;
}

std::string localized_tool_summary(const ToolSpec& spec){
  auto it = spec.summaryLocales.find(g_settings.language);
  if(it!=spec.summaryLocales.end() && !it->second.empty()) return it->second;
  auto en = spec.summaryLocales.find("en");
  if(en!=spec.summaryLocales.end() && !en->second.empty()) return en->second;
  return spec.summary;
}

std::string localized_tool_help(const ToolSpec& spec){
  auto it = spec.helpLocales.find(g_settings.language);
  if(it!=spec.helpLocales.end() && !it->second.empty()) return it->second;
  auto en = spec.helpLocales.find("en");
  if(en!=spec.helpLocales.end() && !en->second.empty()) return en->second;
  return spec.help;
}

std::string tr(const std::string& key){
  auto it = g_i18n.find(key);
  if(it!=g_i18n.end()){
    auto jt = it->second.find(g_settings.language);
    if(jt!=it->second.end()) return jt->second;
    jt = it->second.find("en");
    if(jt!=it->second.end()) return jt->second;
  }
  return key;
}

std::string trFmt(const std::string& key, const std::map<std::string, std::string>& values){
  std::string base = tr(key);
  std::string out;
  out.reserve(base.size()+32);
  for(size_t i=0;i<base.size();){
    if(base[i]=='{' ){
      size_t end = base.find('}', i+1);
      if(end!=std::string::npos){
        std::string var = base.substr(i+1, end-i-1);
        auto it = values.find(var);
        if(it!=values.end()) out += it->second;
        else out += base.substr(i, end-i+1);
        i = end+1;
        continue;
      }
    }
    out.push_back(base[i]);
    ++i;
  }
  return out;
}

struct SubsequenceWeights {
  double baseHit = 1.0;
  double boundaryBonus = 7.0;
  double headBonus = 1.0;
  double consecutiveBonusPerExtend = 4.0;
  double caseMatchBonus = 0.5;
  double gapBase = 1.0;
  double gapQuad = 0.10;
  double firstIndexPenalty = 0.10;
  double lengthPenaltyLambda = 0.15;
  double exactEqualBonus = 20.0;
  double substringBonus = 10.0;
  double prefixBonus = 5.0;
};

static const SubsequenceWeights kSubsequenceWeights{};

static inline bool subseq_is_sep(char c){
  return c=='/' || c=='.' || c=='_' || c=='-' || std::isspace(static_cast<unsigned char>(c));
}

static inline bool subseq_is_boundary(const std::string& text, int i){
  if(i <= 0) return true;
  char prev = text[static_cast<size_t>(i) - 1];
  char cur = text[static_cast<size_t>(i)];
  if(subseq_is_sep(prev)) return true;
  bool camel = std::islower(static_cast<unsigned char>(prev)) && std::isupper(static_cast<unsigned char>(cur));
  bool typeSwitch = (std::isalnum(static_cast<unsigned char>(prev)) != 0) ^ (std::isalnum(static_cast<unsigned char>(cur)) != 0);
  return camel || typeSwitch;
}

static int subseq_count_boundary_hits(const std::string& text, const std::vector<int>& pos){
  int count = 0;
  for(int idx : pos){
    if(idx >= 0 && idx < static_cast<int>(text.size()) && subseq_is_boundary(text, idx)) ++count;
  }
  return count;
}

static int subseq_longest_run(const std::vector<int>& pos){
  if(pos.empty()) return 0;
  int best = 1, cur = 1;
  for(size_t i=1;i<pos.size();++i){
    if(pos[i] == pos[i-1] + 1){
      ++cur;
    }else{
      best = std::max(best, cur);
      cur = 1;
    }
  }
  best = std::max(best, cur);
  return best;
}

static int subseq_case_mismatch(const std::string& target, const std::string& query, const std::vector<int>& pos){
  int mismatch = 0;
  for(size_t j=0;j<pos.size();++j){
    int i = pos[j];
    if(i < 0 || i >= static_cast<int>(target.size())) continue;
    char tc = target[static_cast<size_t>(i)];
    char qc = query[j];
    if(std::tolower(static_cast<unsigned char>(tc)) == std::tolower(static_cast<unsigned char>(qc)) && tc != qc){
      ++mismatch;
    }
  }
  return mismatch;
}

static std::optional<MatchResult> greedy_subsequence_alignment(const std::string& target,
                                                               const std::string& query,
                                                               bool ignoreCase){
  if(query.empty()){
    MatchResult base;
    base.matched = true;
    base.exact = target.empty();
    base.isExactEqual = base.exact;
    base.isSubstring = true;
    base.isPrefix = true;
    base.score = 0.0;
    return base;
  }

  std::vector<int> positions;
  positions.reserve(query.size());
  size_t qi = 0;
  auto norm = [&](char ch){
    return ignoreCase ? static_cast<char>(std::tolower(static_cast<unsigned char>(ch))) : ch;
  };

  for(size_t i=0;i<target.size() && qi<query.size();++i){
    if(norm(target[i]) == norm(query[qi])){
      positions.push_back(static_cast<int>(i));
      ++qi;
    }
  }

  if(qi != query.size()) return std::nullopt;

  MatchResult result;
  result.matched = true;
  result.positions = std::move(positions);
  result.score = 0.0;
  result.boundaryHits = subseq_count_boundary_hits(target, result.positions);
  result.maxRun = subseq_longest_run(result.positions);
  result.totalGaps = 0;
  for(size_t i=1;i<result.positions.size();++i){
    result.totalGaps += std::max(0, result.positions[i] - result.positions[i-1] - 1);
  }
  result.windowSpan = result.positions.empty()? 0 : (result.positions.back() - result.positions.front());
  result.firstIndex = result.positions.empty()? 0 : result.positions.front();
  result.caseMismatch = subseq_case_mismatch(target, query, result.positions);
  result.isSubstring = (result.maxRun == static_cast<int>(result.positions.size()));
  result.isPrefix = (!result.positions.empty() && result.positions.front() == 0);

  bool isExact = (target.size() == query.size());
  if(isExact){
    isExact = std::equal(target.begin(), target.end(), query.begin(),
                         [&](char a, char b){
                           return ignoreCase
                              ? std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b))
                              : a == b;
                         });
  }
  result.exact = isExact;
  result.isExactEqual = isExact;

  return result;
}

static std::optional<MatchResult> best_subsequence_alignment(const std::string& target,
                                                             const std::string& query,
                                                             bool ignoreCase){
  const int n = static_cast<int>(target.size());
  const int m = static_cast<int>(query.size());
  if(m == 0){
    MatchResult base;
    base.matched = true;
    base.exact = target.empty();
    base.isExactEqual = base.exact;
    base.isSubstring = true;
    base.isPrefix = true;
    base.score = 0.0;
    return base;
  }

  const double NEG_INF = -1e300;
  std::vector<std::vector<double>> dp(n, std::vector<double>(m, NEG_INF));
  std::vector<std::vector<int>> prev(n, std::vector<int>(m, -1));
  std::vector<int> boundary(n, 0);
  for(int i=0;i<n;++i){
    boundary[i] = subseq_is_boundary(target, i) ? 1 : 0;
  }

  auto eq = [&](char a, char b){
    if(ignoreCase){
      return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    }
    return a == b;
  };

  for(int i=0;i<n;++i){
    if(!eq(target[i], query[0])) continue;
    double score = kSubsequenceWeights.baseHit;
    if(boundary[i]) score += kSubsequenceWeights.boundaryBonus;
    if(i == 0) score += kSubsequenceWeights.headBonus;
    if(target[i] == query[0]) score += kSubsequenceWeights.caseMatchBonus;
    score -= kSubsequenceWeights.firstIndexPenalty * i;
    dp[i][0] = score;
    prev[i][0] = -1;
  }

  for(int j=1;j<m;++j){
    for(int i=j;i<n;++i){
      if(!eq(target[i], query[j])) continue;
      double bestScore = NEG_INF;
      int bestPrev = -1;
      for(int k=j-1;k<i;++k){
        double prevScore = dp[k][j-1];
        if(prevScore <= NEG_INF/2) continue;
        double trans = prevScore;
        int gap = i - k - 1;
        if(gap > 0){
          double penalty = kSubsequenceWeights.gapBase * gap;
          if(gap > 1){
            penalty += kSubsequenceWeights.gapQuad * (gap - 1) * (gap - 1);
          }
          trans -= penalty;
        }
        trans += kSubsequenceWeights.baseHit;
        if(boundary[i]) trans += kSubsequenceWeights.boundaryBonus;
        if(target[i] == query[j]) trans += kSubsequenceWeights.caseMatchBonus;
        if(i == k + 1) trans += kSubsequenceWeights.consecutiveBonusPerExtend;
        if(trans > bestScore){
          bestScore = trans;
          bestPrev = k;
        }
      }
      if(bestPrev != -1){
        dp[i][j] = bestScore;
        prev[i][j] = bestPrev;
      }
    }
  }

  double best = NEG_INF;
  int endIndex = -1;
  for(int i=m-1;i<n;++i){
    if(dp[i][m-1] > best){
      best = dp[i][m-1];
      endIndex = i;
    }
  }
  if(endIndex == -1) return std::nullopt;

  std::vector<int> pos(static_cast<size_t>(m));
  int ci = endIndex;
  int cj = m - 1;
  while(cj >= 0){
    pos[static_cast<size_t>(cj)] = ci;
    ci = prev[ci][cj];
    --cj;
  }

  int firstIndex = pos.front();
  int windowSpan = pos.back() - pos.front();
  int gaps = 0;
  for(size_t t=1;t<pos.size();++t){
    gaps += std::max(0, pos[t] - pos[t-1] - 1);
  }

  int maxRun = subseq_longest_run(pos);
  bool isSubstring = (maxRun == m);
  bool isPrefix = (firstIndex == 0);
  bool isExact = ((int)target.size() == m) && isSubstring &&
                 std::equal(target.begin(), target.end(), query.begin(),
                            [&](char a, char b){ return std::tolower(static_cast<unsigned char>(a)) ==
                                                      std::tolower(static_cast<unsigned char>(b)); });

  if(isExact) best += kSubsequenceWeights.exactEqualBonus;
  if(isSubstring) best += kSubsequenceWeights.substringBonus;
  if(isPrefix) best += kSubsequenceWeights.prefixBonus;
  best -= kSubsequenceWeights.lengthPenaltyLambda * std::log1p(static_cast<double>(target.size()));

  MatchResult result;
  result.matched = true;
  result.positions = std::move(pos);
  result.score = best;
  result.boundaryHits = subseq_count_boundary_hits(target, result.positions);
  result.maxRun = maxRun;
  result.totalGaps = gaps;
  result.windowSpan = windowSpan;
  result.firstIndex = firstIndex;
  result.caseMismatch = subseq_case_mismatch(target, query, result.positions);
  result.isSubstring = isSubstring;
  result.isPrefix = isPrefix;
  result.isExactEqual = isExact;
  result.exact = isExact;
  return result;
}

MatchResult compute_match(const std::string& candidate, const std::string& pattern){
  MatchResult res;
  bool ignoreCase = g_settings.completionIgnoreCase;
  bool subseq = g_settings.completionSubsequence;

  if(pattern.empty()){
    res.matched = true;
    res.exact = candidate.empty();
    res.isExactEqual = res.exact;
    res.isSubstring = true;
    res.isPrefix = true;
    res.score = 0.0;
    return res;
  }

  if(subseq){
    if(g_settings.completionSubsequenceStrategy == SubsequenceStrategy::Ranked){
      if(auto best = best_subsequence_alignment(candidate, pattern, ignoreCase)){
        return *best;
      }
    }else{
      if(auto greedy = greedy_subsequence_alignment(candidate, pattern, ignoreCase)){
        return *greedy;
      }
    }
  }

  auto normalize = [&](char ch){
    return ignoreCase ? static_cast<char>(std::tolower(static_cast<unsigned char>(ch))) : ch;
  };

  if(pattern.size() > candidate.size()) return res;

  bool prefix = true;
  res.positions.clear();
  res.positions.reserve(pattern.size());
  for(size_t i=0;i<pattern.size();++i){
    char c = normalize(candidate[i]);
    char pc = normalize(pattern[i]);
    if(c != pc){
      prefix = false;
      break;
    }
    res.positions.push_back(static_cast<int>(i));
  }
  if(!prefix){
    res.positions.clear();
    return res;
  }

  res.matched = true;
  res.exact = (candidate.size() == pattern.size());
  res.isExactEqual = res.exact;
  res.isPrefix = true;
  res.isSubstring = !res.positions.empty();
  res.score = 0.0;
  res.boundaryHits = subseq_count_boundary_hits(candidate, res.positions);
  res.maxRun = static_cast<int>(res.positions.size());
  res.totalGaps = 0;
  res.windowSpan = res.positions.empty() ? 0 : (res.positions.back() - res.positions.front());
  res.firstIndex = res.positions.empty() ? 0 : res.positions.front();
  res.caseMismatch = subseq_case_mismatch(candidate, pattern, res.positions);
  return res;
}

void sortCandidatesByMatch(const std::string& query, Candidates& cand){
  if(!g_settings.completionSubsequence) return;
  if(g_settings.completionSubsequenceStrategy != SubsequenceStrategy::Ranked) return;
  if(query.empty()) return;
  size_t n = cand.labels.size();
  if(n <= 1) return;
  if(cand.matchDetails.size() != n) return;

  std::vector<size_t> order(n);
  std::iota(order.begin(), order.end(), size_t{0});
  auto cmp = [&](size_t lhs, size_t rhs){
    const MatchResult& a = cand.matchDetails[lhs];
    const MatchResult& b = cand.matchDetails[rhs];
    if(a.score != b.score) return a.score > b.score;
    if(a.isExactEqual != b.isExactEqual) return a.isExactEqual && !b.isExactEqual;
    if(a.isSubstring != b.isSubstring) return a.isSubstring && !b.isSubstring;
    if(a.isPrefix != b.isPrefix) return a.isPrefix && !b.isPrefix;
    if(a.boundaryHits != b.boundaryHits) return a.boundaryHits > b.boundaryHits;
    if(a.maxRun != b.maxRun) return a.maxRun > b.maxRun;
    if(a.totalGaps != b.totalGaps) return a.totalGaps < b.totalGaps;
    if(a.windowSpan != b.windowSpan) return a.windowSpan < b.windowSpan;
    if(a.firstIndex != b.firstIndex) return a.firstIndex < b.firstIndex;
    if(a.caseMismatch != b.caseMismatch) return a.caseMismatch < b.caseMismatch;
    if(cand.labels[lhs].size() != cand.labels[rhs].size()) return cand.labels[lhs].size() < cand.labels[rhs].size();
    return cand.labels[lhs] < cand.labels[rhs];
  };

  std::stable_sort(order.begin(), order.end(), cmp);

  bool changed = false;
  for(size_t i=0;i<n;++i){
    if(order[i] != i){ changed = true; break; }
  }
  if(!changed) return;

  auto reorderVec = [&](auto& vec){
    using VecType = std::decay_t<decltype(vec)>;
    VecType tmp;
    tmp.reserve(vec.size());
    for(size_t idx : order){
      tmp.push_back(vec[idx]);
    }
    vec = std::move(tmp);
  };
  reorderVec(cand.items);
  reorderVec(cand.labels);
  reorderVec(cand.matchPositions);
  reorderVec(cand.annotations);
  reorderVec(cand.exactMatches);
  reorderVec(cand.matchDetails);
}

// ===== Prompt params =====
static const int extraLines = 3;

struct CodepointRange {
  char32_t first;
  char32_t last;
};

static bool isInRange(char32_t cp, const CodepointRange& range){
  return cp >= range.first && cp <= range.last;
}

static bool isCombining(char32_t cp){
  // Combining mark ranges adapted from Markus Kuhn's wcwidth implementation
  // (https://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c) updated for modern Unicode.
  static constexpr CodepointRange ranges[] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x05BF, 0x05BF},
    {0x05C1, 0x05C2}, {0x05C4, 0x05C5}, {0x05C7, 0x05C7}, {0x0600, 0x0605},
    {0x0610, 0x061A}, {0x061C, 0x061C}, {0x064B, 0x065F}, {0x0670, 0x0670},
    {0x06D6, 0x06DD}, {0x06DF, 0x06E4}, {0x06E7, 0x06E8}, {0x06EA, 0x06ED},
    {0x070F, 0x070F}, {0x0711, 0x0711}, {0x0730, 0x074A}, {0x07A6, 0x07B0},
    {0x07EB, 0x07F3}, {0x07FD, 0x07FD}, {0x0816, 0x0819}, {0x081B, 0x0823},
    {0x0825, 0x0827}, {0x0829, 0x082D}, {0x0859, 0x085B}, {0x08D3, 0x08E1},
    {0x08E3, 0x0902}, {0x093A, 0x093A}, {0x093C, 0x093C}, {0x0941, 0x0948},
    {0x094D, 0x094D}, {0x0951, 0x0957}, {0x0962, 0x0963}, {0x0981, 0x0981},
    {0x09BC, 0x09BC}, {0x09C1, 0x09C4}, {0x09CD, 0x09CD}, {0x09E2, 0x09E3},
    {0x09FE, 0x09FE}, {0x0A01, 0x0A02}, {0x0A3C, 0x0A3C}, {0x0A41, 0x0A42},
    {0x0A47, 0x0A48}, {0x0A4B, 0x0A4D}, {0x0A51, 0x0A51}, {0x0A70, 0x0A71},
    {0x0A75, 0x0A75}, {0x0A81, 0x0A82}, {0x0ABC, 0x0ABC}, {0x0AC1, 0x0AC5},
    {0x0AC7, 0x0AC8}, {0x0ACD, 0x0ACD}, {0x0AE2, 0x0AE3}, {0x0AFA, 0x0AFF},
    {0x0B01, 0x0B01}, {0x0B3C, 0x0B3C}, {0x0B3F, 0x0B3F}, {0x0B41, 0x0B44},
    {0x0B4D, 0x0B4D}, {0x0B56, 0x0B56}, {0x0B62, 0x0B63}, {0x0B82, 0x0B82},
    {0x0BC0, 0x0BC0}, {0x0BCD, 0x0BCD}, {0x0C00, 0x0C00}, {0x0C04, 0x0C04},
    {0x0C3E, 0x0C40}, {0x0C46, 0x0C48}, {0x0C4A, 0x0C4D}, {0x0C55, 0x0C56},
    {0x0C62, 0x0C63}, {0x0C81, 0x0C81}, {0x0CBC, 0x0CBC}, {0x0CBF, 0x0CBF},
    {0x0CC6, 0x0CC6}, {0x0CCC, 0x0CCD}, {0x0CE2, 0x0CE3}, {0x0D00, 0x0D01},
    {0x0D3B, 0x0D3C}, {0x0D41, 0x0D44}, {0x0D4D, 0x0D4D}, {0x0D62, 0x0D63},
    {0x0D81, 0x0D81}, {0x0DCA, 0x0DCA}, {0x0DD2, 0x0DD4}, {0x0DD6, 0x0DD6},
    {0x0E31, 0x0E31}, {0x0E34, 0x0E3A}, {0x0E47, 0x0E4E}, {0x0EB1, 0x0EB1},
    {0x0EB4, 0x0EBC}, {0x0EC8, 0x0ECD}, {0x0F18, 0x0F19}, {0x0F35, 0x0F35},
    {0x0F37, 0x0F37}, {0x0F39, 0x0F39}, {0x0F71, 0x0F7E}, {0x0F80, 0x0F84},
    {0x0F86, 0x0F87}, {0x0F8D, 0x0F97}, {0x0F99, 0x0FBC}, {0x0FC6, 0x0FC6},
    {0x102D, 0x1030}, {0x1032, 0x1037}, {0x1039, 0x103A}, {0x103D, 0x103E},
    {0x1058, 0x1059}, {0x105E, 0x1060}, {0x1071, 0x1074}, {0x1082, 0x1082},
    {0x1085, 0x1086}, {0x108D, 0x108D}, {0x109D, 0x109D}, {0x135D, 0x135F},
    {0x1712, 0x1714}, {0x1732, 0x1734}, {0x1752, 0x1753}, {0x1772, 0x1773},
    {0x17B4, 0x17B5}, {0x17B7, 0x17BD}, {0x17C6, 0x17C6}, {0x17C9, 0x17D3},
    {0x17DD, 0x17DD}, {0x180B, 0x180D}, {0x180F, 0x180F}, {0x1885, 0x1886},
    {0x18A9, 0x18A9}, {0x1920, 0x1922}, {0x1927, 0x1928}, {0x1932, 0x1932},
    {0x1939, 0x193B}, {0x1A17, 0x1A18}, {0x1A1B, 0x1A1B}, {0x1A56, 0x1A56},
    {0x1A58, 0x1A5E}, {0x1A60, 0x1A60}, {0x1A62, 0x1A62}, {0x1A65, 0x1A6C},
    {0x1A73, 0x1A7C}, {0x1A7F, 0x1A7F}, {0x1AB0, 0x1ABE}, {0x1ABF, 0x1ACE},
    {0x1B00, 0x1B03}, {0x1B34, 0x1B34}, {0x1B36, 0x1B3A}, {0x1B3C, 0x1B3C},
    {0x1B42, 0x1B42}, {0x1B6B, 0x1B73}, {0x1B80, 0x1B81}, {0x1BA2, 0x1BA5},
    {0x1BA8, 0x1BA9}, {0x1BAB, 0x1BAD}, {0x1BE6, 0x1BE6}, {0x1BE8, 0x1BE9},
    {0x1BED, 0x1BED}, {0x1BEF, 0x1BF1}, {0x1C2C, 0x1C33}, {0x1C36, 0x1C37},
    {0x1CD0, 0x1CD2}, {0x1CD4, 0x1CE0}, {0x1CE2, 0x1CE8}, {0x1CED, 0x1CED},
    {0x1CF4, 0x1CF4}, {0x1CF8, 0x1CF9}, {0x1DC0, 0x1DF9}, {0x1DFB, 0x1DFF},
    {0x200B, 0x200F}, {0x202A, 0x202E}, {0x2060, 0x2064}, {0x2066, 0x206F},
    {0x20D0, 0x20DC}, {0x20DD, 0x20E0}, {0x20E1, 0x20E1}, {0x20E2, 0x20E4},
    {0x20E5, 0x20F0}, {0x2CEF, 0x2CF1}, {0x2D7F, 0x2D7F}, {0x2DE0, 0x2DFF},
    {0x302A, 0x302D}, {0x302E, 0x302F}, {0x3099, 0x309A}, {0xA66F, 0xA66F},
    {0xA674, 0xA67D}, {0xA69E, 0xA69F}, {0xA6F0, 0xA6F1}, {0xA802, 0xA802},
    {0xA806, 0xA806}, {0xA80B, 0xA80B}, {0xA825, 0xA826}, {0xA82C, 0xA82C},
    {0xA8C4, 0xA8C5}, {0xA8E0, 0xA8F1}, {0xA8FF, 0xA8FF}, {0xA926, 0xA92D},
    {0xA947, 0xA951}, {0xA980, 0xA982}, {0xA9B3, 0xA9B3}, {0xA9B6, 0xA9B9},
    {0xA9BC, 0xA9BD}, {0xA9E5, 0xA9E5}, {0xAA29, 0xAA2E}, {0xAA31, 0xAA32},
    {0xAA35, 0xAA36}, {0xAA43, 0xAA43}, {0xAA4C, 0xAA4C}, {0xAA7C, 0xAA7C},
    {0xAAB0, 0xAAB0}, {0xAAB2, 0xAAB4}, {0xAAB7, 0xAAB8}, {0xAABE, 0xAABF},
    {0xAAC1, 0xAAC1}, {0xAAEC, 0xAAED}, {0xAAF6, 0xAAF6}, {0xABE5, 0xABE5},
    {0xABE8, 0xABE8}, {0xABED, 0xABED}, {0xFB1E, 0xFB1E}, {0xFE00, 0xFE0F},
    {0xFE20, 0xFE2F}, {0x101FD, 0x101FD}, {0x102E0, 0x102E0},
    {0x10376, 0x1037A}, {0x10A01, 0x10A03}, {0x10A05, 0x10A06},
    {0x10A0C, 0x10A0F}, {0x10A38, 0x10A3A}, {0x10A3F, 0x10A3F},
    {0x10AE5, 0x10AE6}, {0x10D24, 0x10D27}, {0x10EAB, 0x10EAC},
    {0x10EFD, 0x10EFF}, {0x10F46, 0x10F50}, {0x10F82, 0x10F85},
    {0x11001, 0x11001}, {0x11038, 0x11046}, {0x1107F, 0x11081},
    {0x110B3, 0x110B6}, {0x110B9, 0x110BA}, {0x11100, 0x11102},
    {0x11127, 0x1112B}, {0x1112D, 0x11134}, {0x11173, 0x11173},
    {0x11180, 0x11181}, {0x111B6, 0x111BE}, {0x111C9, 0x111CC},
    {0x111CF, 0x111CF}, {0x1122F, 0x11231}, {0x11234, 0x11234},
    {0x11236, 0x11237}, {0x1123E, 0x1123E}, {0x112DF, 0x112DF},
    {0x112E3, 0x112EA}, {0x11300, 0x11301}, {0x1133B, 0x1133C},
    {0x11340, 0x11340}, {0x11366, 0x1136C}, {0x11370, 0x11374},
    {0x11438, 0x1143F}, {0x11442, 0x11444}, {0x11446, 0x11446},
    {0x1145E, 0x1145E}, {0x114B3, 0x114B8}, {0x114BA, 0x114BA},
    {0x114BF, 0x114C0}, {0x114C2, 0x114C3}, {0x115B2, 0x115B5},
    {0x115BC, 0x115BD}, {0x115BF, 0x115C0}, {0x115DC, 0x115DD},
    {0x11633, 0x1163A}, {0x1163D, 0x1163D}, {0x1163F, 0x11640},
    {0x116AB, 0x116AB}, {0x116AD, 0x116AD}, {0x116B0, 0x116B5},
    {0x116B7, 0x116B7}, {0x1171D, 0x1171F}, {0x11722, 0x11725},
    {0x11727, 0x1172B}, {0x1182F, 0x11837}, {0x11839, 0x1183A},
    {0x1193B, 0x1193C}, {0x1193E, 0x1193E}, {0x11943, 0x11943},
    {0x119D4, 0x119D7}, {0x119DA, 0x119DB}, {0x119E0, 0x119E0},
    {0x11A01, 0x11A0A}, {0x11A33, 0x11A38}, {0x11A3B, 0x11A3E},
    {0x11A47, 0x11A47}, {0x11A51, 0x11A56}, {0x11A59, 0x11A5B},
    {0x11A8A, 0x11A96}, {0x11A98, 0x11A99}, {0x11C30, 0x11C36},
    {0x11C38, 0x11C3D}, {0x11C3F, 0x11C3F}, {0x11C92, 0x11CA7},
    {0x11CAA, 0x11CB0}, {0x11CB2, 0x11CB3}, {0x11CB5, 0x11CB6},
    {0x11D31, 0x11D36}, {0x11D3A, 0x11D3A}, {0x11D3C, 0x11D3D},
    {0x11D3F, 0x11D45}, {0x11D47, 0x11D47}, {0x11D90, 0x11D91},
    {0x11D95, 0x11D95}, {0x11D97, 0x11D97}, {0x11EF3, 0x11EF4},
    {0x13430, 0x13438}, {0x16AF0, 0x16AF4}, {0x16B30, 0x16B36},
    {0x16F4F, 0x16F4F}, {0x16F8F, 0x16F92}, {0x16FE4, 0x16FE4},
    {0x16FF0, 0x16FF1}, {0x1BC9D, 0x1BC9E}, {0x1BCA0, 0x1BCA3},
    {0x1D167, 0x1D169}, {0x1D173, 0x1D182}, {0x1D185, 0x1D18B},
    {0x1D1AA, 0x1D1AD}, {0x1D242, 0x1D244}, {0x1DA00, 0x1DA36},
    {0x1DA3B, 0x1DA6C}, {0x1DA75, 0x1DA75}, {0x1DA84, 0x1DA84},
    {0x1DA9B, 0x1DA9F}, {0x1DAA1, 0x1DAAF}, {0x1E000, 0x1E006},
    {0x1E008, 0x1E018}, {0x1E01B, 0x1E021}, {0x1E023, 0x1E024},
    {0x1E026, 0x1E02A}, {0x1E08F, 0x1E08F}, {0x1E130, 0x1E136},
    {0x1E2AE, 0x1E2AE}, {0x1E2EC, 0x1E2EF}, {0x1E4EC, 0x1E4EF},
    {0x1E8D0, 0x1E8D6}, {0x1E944, 0x1E94A}, {0xE0100, 0xE01EF}
  };
  for(const auto& range : ranges){
    if(cp < range.first) break;
    if(isInRange(cp, range)) return true;
  }
  return false;
}

static int codepointWidth(char32_t cp){
  if(cp == 0) return 0;
  if(cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0;
  if(isCombining(cp)) return 0;
  if(cp >= 0x1100 &&
     (cp <= 0x115F ||
      cp == 0x2329 || cp == 0x232A ||
      (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
      (cp >= 0xAC00 && cp <= 0xD7A3) ||
      (cp >= 0xF900 && cp <= 0xFAFF) ||
      (cp >= 0xFE10 && cp <= 0xFE19) ||
      (cp >= 0xFE30 && cp <= 0xFE6F) ||
      (cp >= 0xFF00 && cp <= 0xFF60) ||
      (cp >= 0xFFE0 && cp <= 0xFFE6) ||
      (cp >= 0x1F300 && cp <= 0x1F64F) ||
      (cp >= 0x1F680 && cp <= 0x1F6FF) ||
      (cp >= 0x1F900 && cp <= 0x1FAFF) ||
      (cp >= 0x20000 && cp <= 0x2FFFD) ||
      (cp >= 0x30000 && cp <= 0x3FFFD)))
    return 2;
  return 1;
}

static int displayWidth(const std::string& text){
  static std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
  std::u32string codepoints;
  try {
    codepoints = conv.from_bytes(text);
  } catch (const std::range_error&) {
    return static_cast<int>(text.size());
  }
  int width = 0;
  for(char32_t cp : codepoints){
    width += codepointWidth(cp);
  }
  return width;
}

struct Utf8Glyph {
  std::string bytes;
  int width = 1;
};

static std::vector<Utf8Glyph> utf8Glyphs(const std::string& text){
  std::vector<Utf8Glyph> glyphs;
  glyphs.reserve(text.size());
  size_t i = 0;
  while(i < text.size()){
    unsigned char lead = static_cast<unsigned char>(text[i]);
    size_t len = utf8CharLength(lead);
    if(i + len > text.size()) len = 1;
    std::string bytes = text.substr(i, len);
    int width = displayWidth(bytes);
    if(width <= 0) width = 1;
    glyphs.push_back(Utf8Glyph{bytes, width});
    i += len;
  }
  return glyphs;
}

static std::string renderHighlightedLabel(const std::string& label, const std::vector<int>& positions){
  std::string out;
  out.reserve(label.size()*4);
  size_t idx = 0;
  int state = 0; // 0 none, 1 white, 2 gray
  auto flush = [&](int next){
    if(state==next) return;
    if(state!=0) out += ansi::RESET;
    if(next==1) out += ansi::WHITE;
    else if(next==2) out += ansi::GRAY;
    state = next;
  };
  for(size_t i=0;i<label.size();++i){
    bool matched = (idx < positions.size() && positions[idx]==static_cast<int>(i));
    flush(matched?1:2);
    out.push_back(label[i]);
    if(matched) ++idx;
  }
  if(state!=0) out += ansi::RESET;
  return out;
}

static int highlightCursorOffset(const std::string& label, const std::vector<int>& positions){
  if(positions.empty()) return 0;
  int last = positions.back();
  if(last < 0) return 0;
  std::string prefix = label.substr(0, static_cast<size_t>(last)+1);
  return displayWidth(prefix);
}

static int promptDisplayWidth(){
  auto indicators = promptIndicatorsRender();
  return displayWidth(indicators.plain + plainPromptText());
}

// helper: 是否“路径型占位符”
static bool isPathLikePlaceholder(const std::string& ph){
  std::string t = ph; std::transform(t.begin(), t.end(), t.begin(), ::tolower);
  // 支持 <path> / <file> / <dir> 及其变体 [<path>]、<path...> 等
  return t.find("<path")!=std::string::npos || t.find("<file")!=std::string::npos || t.find("<dir")!=std::string::npos;
}

static PathKind placeholderPathKind(const std::string& ph){
  std::string t = ph; std::transform(t.begin(), t.end(), t.begin(), ::tolower);
  if(t.find("<file") != std::string::npos) return PathKind::File;
  if(t.find("<dir")  != std::string::npos) return PathKind::Dir;
  return PathKind::Any;
}

static bool positionalSpecIsPath(const PositionalArgSpec& spec){
  if(spec.isPath) return true;
  if(spec.inferFromPlaceholder) return isPathLikePlaceholder(spec.placeholder);
  return false;
}

static PathKind positionalSpecKind(const PositionalArgSpec& spec){
  if(spec.pathKind != PathKind::Any) return spec.pathKind;
  if(spec.inferFromPlaceholder) return placeholderPathKind(spec.placeholder);
  return PathKind::Any;
}

struct PathCompletionContext {
  bool active = false;
  bool appliesToCurrentWord = false;
  PathKind kind = PathKind::Any;
  std::vector<std::string> extensions;
  bool allowDirectory = true;
};

static PathCompletionContext analyzePositionalPathContext(const std::vector<PositionalArgSpec>& posDefs,
                                                         size_t startIdx,
                                                         const std::vector<OptionSpec>& opts,
                                                         const std::vector<std::string>& toks,
                                                         const SplitWord& sw,
                                                         const std::string& buf){
  PathCompletionContext ctx;
  if(posDefs.empty()) return ctx;

  bool trailingSpace = (!buf.empty() && std::isspace(static_cast<unsigned char>(buf.back())));
  if(!trailingSpace && startIdx >= toks.size()) return ctx;

  size_t i = startIdx;
  size_t posFilled = 0;
  bool currentWordIsPositional = false;

  while(i < toks.size()){
    const std::string& tk = toks[i];
    if(!tk.empty() && tk[0]=='-'){
      bool takes=false;
      size_t advance = 1;
      for(auto &o : opts){
        if(o.name == tk){
          takes = o.takesValue;
          if(takes && i + 1 < toks.size()){
            bool valueIsCurrent = (!trailingSpace && i + 1 == toks.size() - 1 && toks.back() == sw.word);
            if(valueIsCurrent){
              currentWordIsPositional = false;
            }
            advance = 2;
          }
          break;
        }
      }
      i += advance;
      continue;
    }

    bool isCurrentWord = (!trailingSpace && i == toks.size() - 1 && toks[i] == sw.word);
    if(isCurrentWord){
      currentWordIsPositional = true;
      break;
    }
    posFilled += 1;
    i += 1;
  }

  if(!(trailingSpace || currentWordIsPositional)) return ctx;

  if(posFilled < posDefs.size() && positionalSpecIsPath(posDefs[posFilled])){
    ctx.active = true;
    ctx.appliesToCurrentWord = currentWordIsPositional;
    ctx.kind = positionalSpecKind(posDefs[posFilled]);
    ctx.extensions = posDefs[posFilled].allowedExtensions;
    ctx.allowDirectory = posDefs[posFilled].allowDirectory;
    if(!ctx.extensions.empty() && ctx.kind == PathKind::Any){
      ctx.kind = PathKind::File;
    }
  }
  return ctx;
}

static bool inSubcommandSlot(const ToolSpec& spec, const std::vector<std::string>& toks){
  if (spec.subs.empty()) return false;
  if (toks.size()==1) return true;
  if (toks.size()>=2){
    for (auto &s : spec.subs) if (s.name==toks[1]) return false;
    return true;
  }
  return false;
}

static Candidates finalizeCandidates(const std::string& query, Candidates&& cand){
  sortCandidatesByMatch(query, cand);
  return std::move(cand);
}

static Candidates firstWordCandidates(const std::string& buf){
  Candidates out; auto sw=splitLastWord(buf);
  if(!sw.before.empty()) return out;

  auto names = REG.listNames();
  names.push_back("help");
  std::sort(names.begin(),names.end());
  names.erase(std::unique(names.begin(),names.end()), names.end());

  for(auto&s:names){
    MatchResult match = compute_match(s, sw.word);
    if(!match.matched) continue;
    if(match.exact && s == sw.word) continue;
    out.items.push_back(sw.before + s);
    out.labels.push_back(s);
    out.matchPositions.push_back(match.positions);
    out.annotations.push_back("");
    out.exactMatches.push_back(match.exact);
    out.matchDetails.push_back(match);
  }
  return finalizeCandidates(sw.word, std::move(out));
}

static Candidates candidatesForTool(const ToolSpec& spec, const std::string& buf){
  Candidates out; auto sw=splitLastWord(buf); auto toks=splitTokens(buf);
  if(toks.empty() || toks[0]!=spec.name) return out;

  // 子命令名补全
  if (inSubcommandSlot(spec, toks)){
    for(auto &sub: spec.subs){
      MatchResult match = compute_match(sub.name, sw.word);
      if(!match.matched) continue;
      out.items.push_back(sw.before + sub.name);
      out.labels.push_back(sub.name);
      out.matchPositions.push_back(match.positions);
      out.annotations.push_back("");
      out.exactMatches.push_back(match.exact);
      out.matchDetails.push_back(match);
    }
    if(!out.items.empty()) return finalizeCandidates(sw.word, std::move(out));
  }

  // 是否子命令上下文
  auto findSub=[&]()->const SubcommandSpec*{
    if(toks.size()>=2){ for(auto &s: spec.subs) if(s.name==toks[1]) return &s; }
    return nullptr;
  };
  const SubcommandSpec* sub=findSub();

  if(spec.name == "setting" && sub){
    auto positionalIndex = [&]()->std::optional<size_t>{
      bool trailingSpace = (!buf.empty() && std::isspace(static_cast<unsigned char>(buf.back())));
      size_t start = 2; // command + subcommand
      size_t count = 0;
      for(size_t i=start; i<toks.size(); ++i){
        bool isCurrent = (!trailingSpace && i + 1 == toks.size() && toks[i] == sw.word);
        if(isCurrent) return count;
        count++;
      }
      if(trailingSpace) return count;
      return std::nullopt;
    }();

    if(positionalIndex){
      size_t idx = *positionalIndex;
      if((sub->name=="get" || sub->name=="set") && idx==0){
        auto keys = settings_list_keys();
        for(const auto& key : keys){
          MatchResult match = compute_match(key, sw.word);
          if(!match.matched) continue;
          out.items.push_back(sw.before + key);
          out.labels.push_back(key);
          out.matchPositions.push_back(match.positions);
          out.annotations.push_back("");
          out.exactMatches.push_back(match.exact);
          out.matchDetails.push_back(match);
        }
        if(!out.items.empty()) return finalizeCandidates(sw.word, std::move(out));
      }
      if(sub->name=="set" && idx==1){
        std::string keyName = (toks.size()>=3? toks[2] : "");
        auto values = settings_value_suggestions_for(keyName);
        for(const auto& val : values){
          MatchResult match = compute_match(val, sw.word);
          if(!match.matched) continue;
          out.items.push_back(sw.before + val);
          out.labels.push_back(val);
          out.matchPositions.push_back(match.positions);
          out.annotations.push_back("");
          out.exactMatches.push_back(match.exact);
          out.matchDetails.push_back(match);
        }
        if(!out.items.empty()) return finalizeCandidates(sw.word, std::move(out));
      }
    }
  }

  if(spec.name == "message" && sub && sub->name=="detail"){
    bool trailingSpace = (!buf.empty() && std::isspace(static_cast<unsigned char>(buf.back())));
    bool expectingArgument = false;
    if(trailingSpace && toks.size()==2){
      expectingArgument = true;
    }else if(!trailingSpace && toks.size()>=3 && toks[2]==sw.word){
      expectingArgument = true;
    }
    if(expectingArgument){
      std::set<std::string> seen;
      for(const auto& info : message_all_files()){
        std::string label = basenameOf(info.path);
        if(!seen.insert(label).second) continue;
        MatchResult match = compute_match(label, sw.word);
        if(!match.matched) continue;
        out.items.push_back(sw.before + label);
        out.labels.push_back(label);
        out.matchPositions.push_back(match.positions);
        std::string annotation;
        if(info.isUnread){
          annotation = info.isNew? "[NEW]" : "[UPDATED]";
        }
        out.annotations.push_back(annotation);
        out.exactMatches.push_back(match.exact);
        out.matchDetails.push_back(match);
      }
      if(!out.items.empty()) return finalizeCandidates(sw.word, std::move(out));
    }
  }

  // 值补全（包含路径型选项）
  if(toks.size()>=2){
    std::string prev = (toks.back()==sw.word && toks.size()>=2) ? toks[toks.size()-2] : (!toks.empty()? toks.back():"");
    auto tryValue = [&](const OptionSpec& o)->bool{
      if(o.name==prev && o.takesValue){
        if(o.isPath) {
          PathKind kind = (o.pathKind != PathKind::Any) ? o.pathKind : placeholderPathKind(o.placeholder);
          const std::vector<std::string>* extPtr = o.allowedExtensions.empty() ? nullptr : &o.allowedExtensions;
          out = pathCandidatesForWord(buf, sw.word, kind, extPtr, o.allowDirectory);
          return true;
        }
        std::vector<std::string> vals = o.valueSuggestions;
        if(o.dynamicValues){ auto more=o.dynamicValues(toks); vals.insert(vals.end(), more.begin(), more.end()); }
        for(auto &v: vals){
          MatchResult match = compute_match(v, sw.word);
          if(!match.matched) continue;
          out.items.push_back(sw.before+v);
          out.labels.push_back(v);
          out.matchPositions.push_back(match.positions);
          out.annotations.push_back("");
          out.exactMatches.push_back(match.exact);
          out.matchDetails.push_back(match);
        }
        return true;
      } return false;
    };
    if(sub){ for(auto &o: sub->options) if(tryValue(o)) return finalizeCandidates(sw.word, std::move(out)); }
    for(auto &o: spec.options) if(tryValue(o)) return finalizeCandidates(sw.word, std::move(out));
  }

  // 如果“下一个位置参数占位符”为路径型 → 直接进入路径补全（无需 ./ 或 /）
  auto positionalContext = [&](const std::vector<PositionalArgSpec>& posDefs, size_t startIdx, const std::vector<OptionSpec>& opts){
    return analyzePositionalPathContext(posDefs, startIdx, opts, toks, sw, buf);
  };

  if(sub){
    std::vector<OptionSpec> combinedOpts = spec.options;
    combinedOpts.insert(combinedOpts.end(), sub->options.begin(), sub->options.end());
    auto ctx = positionalContext(sub->positional, /*startIdx*/2, combinedOpts);
    if(ctx.active){
      const std::vector<std::string>* extPtr = ctx.extensions.empty() ? nullptr : &ctx.extensions;
      return pathCandidatesForWord(buf, sw.word, ctx.kind, extPtr, ctx.allowDirectory);
    }
  }else{
    auto ctx = positionalContext(spec.positional, /*startIdx*/1, spec.options);
    if(ctx.active){
      const std::vector<std::string>* extPtr = ctx.extensions.empty() ? nullptr : &ctx.extensions;
      return pathCandidatesForWord(buf, sw.word, ctx.kind, extPtr, ctx.allowDirectory);
    }
  }

  // 选项名补全
  if(startsWith(sw.word,"-")){
    std::set<std::string> used;
    for(size_t i=1;i<toks.size();++i) if(startsWith(toks[i],"--")||startsWith(toks[i],"-")) used.insert(toks[i]);
    auto addOpts = [&](const std::vector<OptionSpec>& opts){
      for(auto &o: opts){
        if(!used.count(o.name)){
          MatchResult match = compute_match(o.name, sw.word);
          if(!match.matched) continue;
          out.items.push_back(sw.before + o.name);
          out.labels.push_back(o.name);
          out.matchPositions.push_back(match.positions);
          out.annotations.push_back("");
          out.exactMatches.push_back(match.exact);
          out.matchDetails.push_back(match);
        }
      }
    };
    if(sub) addOpts(sub->options);
    addOpts(spec.options);
    if(!out.items.empty()) return finalizeCandidates(sw.word, std::move(out));
  }

  // 路径模式（兜底）
  if(startsWith(sw.word,"/")||startsWith(sw.word,"./")||startsWith(sw.word,"../")){
    return pathCandidatesForWord(buf, sw.word, PathKind::Any, nullptr, true);
  }

  return out;
}

static void prioritizeExactMatches(Candidates& cand){
  if(cand.labels.empty()) return;
  if(cand.exactMatches.size() < cand.labels.size()){
    cand.exactMatches.resize(cand.labels.size(), false);
  }
  bool hasExact = std::any_of(cand.exactMatches.begin(), cand.exactMatches.end(), [](bool v){ return v; });
  if(!hasExact) return;
  std::vector<size_t> order(cand.labels.size());
  std::iota(order.begin(), order.end(), size_t{0});
  std::stable_partition(order.begin(), order.end(), [&](size_t idx){
    return cand.exactMatches[idx];
  });
  bool changed = false;
  for(size_t i=0;i<order.size();++i){
    if(order[i] != i){ changed = true; break; }
  }
  if(!changed) return;
  auto reorderVec = [&](auto& vec){
    using VecType = std::decay_t<decltype(vec)>;
    VecType reordered;
    reordered.reserve(vec.size());
    for(size_t idx : order){
      reordered.push_back(vec[idx]);
    }
    vec = std::move(reordered);
  };
  reorderVec(cand.items);
  reorderVec(cand.labels);
  reorderVec(cand.matchPositions);
  reorderVec(cand.annotations);
  reorderVec(cand.exactMatches);
  reorderVec(cand.matchDetails);
}

static Candidates computeCandidates(const std::string& buf){
  auto toks=splitTokens(buf);
  auto sw  = splitLastWord(buf);

  // help 二参补全
  if (!toks.empty() && toks[0] == "help") {
    Candidates out;
    if (toks.size()==1 || (toks.size()==2 && toks.back()==sw.word)) {
      auto names = REG.listNames();
      std::sort(names.begin(), names.end());
      names.erase(std::unique(names.begin(),names.end()), names.end());
      for (auto &n : names) {
        MatchResult match = compute_match(n, sw.word);
        if(!match.matched) continue;
        out.items.push_back(sw.before + n);
        out.labels.push_back(n);
        out.matchPositions.push_back(match.positions);
        out.annotations.push_back("");
        out.exactMatches.push_back(match.exact);
        out.matchDetails.push_back(match);
      }
    }
    return finalizeCandidates(sw.word, std::move(out));
  }

  if(toks.empty()) return firstWordCandidates(buf);
  if(const ToolDefinition* def = REG.find(toks[0])){
    if(def->completion){
      return def->completion(buf, toks);
    }
    return candidatesForTool(def->ui, buf);
  }
  return firstWordCandidates(buf);
}

static std::optional<std::string> detectPathErrorMessage(const std::string& buf, const Candidates& cand){
  auto toks = splitTokens(buf);
  auto sw   = splitLastWord(buf);
  if(toks.empty() || sw.word.empty()) return std::nullopt;
  if(!buf.empty() && std::isspace(static_cast<unsigned char>(buf.back()))) return std::nullopt;
  if(!g_settings.showPathErrorHint) return std::nullopt;

  if(toks[0] == "help") return std::nullopt;
  const ToolDefinition* def = REG.find(toks[0]);
  if(!def) return std::nullopt;
  const ToolSpec& spec = def->ui;

  const SubcommandSpec* sub = nullptr;
  if(!spec.subs.empty() && toks.size()>=2){
    for(const auto &s : spec.subs){
      if(s.name == toks[1]){ sub = &s; break; }
    }
  }

  PathKind expected = PathKind::Any;
  bool hasExpectation = false;
  std::vector<std::string> requiredExtensions;
  bool allowDirectory = true;

  auto findPathOpt = [&](const std::vector<OptionSpec>& opts)->const OptionSpec*{
    if(toks.size()<2 || toks.back()!=sw.word) return nullptr;
    std::string prev = toks[toks.size()-2];
    for(auto &o : opts){
      if(o.name==prev && o.takesValue && o.isPath) return &o;
    }
    return nullptr;
  };

  const OptionSpec* opt = nullptr;
  if(sub){ opt = findPathOpt(sub->options); }
  if(!opt) opt = findPathOpt(spec.options);
  if(opt){
    expected = (opt->pathKind != PathKind::Any) ? opt->pathKind : placeholderPathKind(opt->placeholder);
    if(!opt->allowedExtensions.empty() && expected == PathKind::Any){
      expected = PathKind::File;
    }
    requiredExtensions = opt->allowedExtensions;
    allowDirectory = opt->allowDirectory;
    hasExpectation = true;
  } else {
    if(sub){
      std::vector<OptionSpec> combinedOpts = spec.options;
      combinedOpts.insert(combinedOpts.end(), sub->options.begin(), sub->options.end());
      auto ctx = analyzePositionalPathContext(sub->positional, /*startIdx*/2, combinedOpts, toks, sw, buf);
      if(ctx.appliesToCurrentWord){
        expected = ctx.kind;
        requiredExtensions = ctx.extensions;
        allowDirectory = ctx.allowDirectory;
        hasExpectation = true;
      }
    } else {
      auto ctx = analyzePositionalPathContext(spec.positional, /*startIdx*/1, spec.options, toks, sw, buf);
      if(ctx.appliesToCurrentWord){
        expected = ctx.kind;
        requiredExtensions = ctx.extensions;
        allowDirectory = ctx.allowDirectory;
        hasExpectation = true;
      }
    }
  }

  if(!hasExpectation) return std::nullopt;

  auto normalizedExts = normalizeExtensions(requiredExtensions);

  struct stat st{};
  if(::stat(sw.word.c_str(), &st)!=0){
    bool hasCand = std::any_of(cand.labels.begin(), cand.labels.end(), [&](const std::string& lab){
      MatchResult match = compute_match(lab, sw.word);
      return match.matched;
    });
    if(!hasCand) return tr("path_error_missing");
    return std::nullopt;
  }
  bool isDir = S_ISDIR(st.st_mode);
  bool isFile = S_ISREG(st.st_mode);

  auto hasMatchingCandidateOfType = [&](PathKind kind){
    for(const auto& label : cand.labels){
      MatchResult match = compute_match(label, sw.word);
      if(!match.matched) continue;
      bool candIsDir = (!label.empty() && label.back()=='/');
      if(kind == PathKind::Dir && candIsDir) return true;
      if(kind == PathKind::File && !candIsDir) return true;
      if(kind == PathKind::Any) return true;
    }
    return false;
  };

  if(!allowDirectory && isDir && expected != PathKind::Dir){
    return tr("path_error_need_file");
  }

  if(expected == PathKind::Dir && !isDir){
    if(hasMatchingCandidateOfType(PathKind::Dir)) return std::nullopt;
    return tr("path_error_need_dir");
  }
  if(expected == PathKind::File && !isFile){
    if(hasMatchingCandidateOfType(PathKind::File)) return std::nullopt;
    return tr("path_error_need_file");
  }

  if(!normalizedExts.empty() && isFile){
    auto pos = sw.word.find_last_of('.');
    std::string ext = (pos == std::string::npos) ? std::string() : sw.word.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){
      return static_cast<char>(std::tolower(c));
    });
    if(std::find(normalizedExts.begin(), normalizedExts.end(), ext) == normalizedExts.end()){
      return trFmt("path_error_need_extension", {{"ext", join(normalizedExts, "|")}});
    }
  }
  return std::nullopt;
}

static std::string contextGhostFor(const std::string& buf){
  auto toks=splitTokens(buf); auto sw=splitLastWord(buf);
  if(toks.empty()) return "";
  if (toks[0] == "help"){
    if(toks.size()==1) return " <command>";
    return "";
  }
  const ToolDefinition* def = REG.find(toks[0]); if(!def) return "";
  if(inSubcommandSlot(def->ui, toks)) return " <subcommand>";
  if(!def->ui.subs.empty() && toks.size()>=2){
    for(auto &sub: def->ui.subs){
      if(sub.name==toks[1]){
        std::set<std::string> used;
        for(size_t i=2;i<toks.size();++i)
          if(startsWith(toks[i],"--")||startsWith(toks[i],"-")) used.insert(toks[i]);
        return renderSubGhost(def->ui, sub, toks, 1, used);
      }
    }
  }
  return renderCommandGhost(def->ui, toks);
}

// ===== Rendering =====
static void renderPromptLabel(){
  auto indicator = promptIndicatorsRender();
  if(!indicator.plain.empty()){
    std::cout << indicator.colored;
  }
  const std::string name = promptNamePlain();
  const std::string theme = g_settings.promptTheme;
  if(auto gradient = theme_gradient_colors(theme); gradient.has_value()){
    if(!name.empty()){
      std::cout << ansi::BOLD;
      const auto& colors = *gradient;
      const int startR = colors[0], startG = colors[1], startB = colors[2];
      const int endR = colors[3], endG = colors[4], endB = colors[5];
      auto glyphs = utf8Glyphs(name);
      size_t glyphCount = glyphs.size();
      int totalWidth = 0;
      for(const auto& g : glyphs){ totalWidth += std::max(1, g.width); }
      int progress = 0;
      for(size_t idx=0; idx<glyphCount; ++idx){
        int glyphWidth = std::max(1, glyphs[idx].width);
        int anchor = progress;
        if(idx + 1 == glyphCount) anchor = totalWidth > 0 ? totalWidth - 1 : 0;
        double t = (totalWidth<=1)? 0.0 : static_cast<double>(anchor) / static_cast<double>(totalWidth-1);
        int r = static_cast<int>(startR + (endR - startR) * t + 0.5);
        int g = static_cast<int>(startG + (endG - startG) * t + 0.5);
        int b = static_cast<int>(startB + (endB - startB) * t + 0.5);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm", r, g, b);
        std::cout << buf << glyphs[idx].bytes;
        progress += glyphWidth;
      }
      std::cout << ansi::RESET;
    }
    std::cout << ansi::CYAN << ansi::BOLD << "> " << ansi::RESET;
  }else{
    std::cout << ansi::CYAN << ansi::BOLD << name << "> " << ansi::RESET;
  }
}

static void renderInputWithGhost(const std::string& status, int status_len,
                                 const std::string& buf, const std::string& ghost){
  (void)status_len;
  std::cout << ansi::CLR
            << ansi::WHITE << status << ansi::RESET;
  renderPromptLabel();
  std::cout << ansi::WHITE << buf << ansi::RESET;
  if(!ghost.empty()) std::cout << ansi::GRAY << ghost << ansi::RESET;
  std::cout.flush();
}
static void renderBelowThree(const std::string& status, int status_len,
                             int cursorCol,
                             const std::string& buf,
                             const Candidates& cand,
                             int sel, int &lastShown){
  (void)status;
  int total=(int)cand.labels.size();
  int toShow = std::min(3, std::max(0, total-1));
  auto sw = splitLastWord(buf);
  int indent = status_len + promptDisplayWidth() + displayWidth(sw.before);
  for(int i=1;i<=toShow;++i){
    size_t idx = (sel+i)%total;
    const std::string& label = cand.labels[idx];
    const std::vector<int>& matches = cand.matchPositions[idx];
    std::string line = renderHighlightedLabel(label, matches);
    std::string annotation = (idx < cand.annotations.size()) ? cand.annotations[idx] : "";
    if(!annotation.empty()){
      line += " ";
      line += ansi::GREEN;
      line += annotation;
      line += ansi::RESET;
    }
    std::cout << "\n" << "\x1b[2K";
    for(int s=0;s<indent;++s) std::cout << ' ';
    std::cout << line;
  }
  for(int pad=toShow; pad<lastShown; ++pad){ std::cout << "\n" << "\x1b[2K"; }
  int up = toShow + ((lastShown>toShow)? (lastShown-toShow):0);
  if(up>0) std::cout << ansi::CUU << up << "A";
  std::cout << ansi::CHA << cursorCol << "G" << std::flush;
  lastShown = toShow;
}

// ===== Exec & help =====
static void execToolLine(const std::string& line){
  auto toks = splitTokens(line); if(toks.empty()) return;
  const ToolDefinition* def = REG.find(toks[0]); if(!def){ std::cout<<trFmt("unknown_command", {{"name", toks[0]}})<<"\n"; return; }
  if(!def->executor){ std::cout<<"no handler\n"; return; }
  ToolExecutionRequest req;
  req.tokens = toks;
  req.silent = false;
  req.forLLM = false;
  ToolExecutionResult result = def->executor(req);
  std::string out = result.viewForCli();
  if(!out.empty()) std::cout<<out;
}

ToolExecutionResult invoke_registered_tool(const std::string& line, bool silent){
  ToolExecutionRequest req;
  req.tokens = splitTokens(line);
  req.silent = silent;
  req.forLLM = true;
  if(req.tokens.empty()) return ToolExecutionResult{};
  const ToolDefinition* def = REG.find(req.tokens[0]);
  if(!def || !def->executor){
    ToolExecutionResult res;
    res.exitCode = 1;
    res.output = trFmt("unknown_command", {{"name", req.tokens[0]}}) + "\n";
    res.display = res.output;
    return res;
  }
  return def->executor(req);
}
static void printHelpAll(){
  auto names = REG.listNames();
  std::cout<<tr("help_available_commands")<<"\n";
  std::cout<<tr("help_command_summary")<<"\n";
  for(auto &n:names){
    const ToolDefinition* t=REG.find(n);
    std::cout<<"  "<<n;
    if(t){
      std::string summary = localized_tool_summary(t->ui);
      if(!summary.empty()) std::cout<<"  - "<<summary;
    }
    std::cout<<"\n";
  }
  std::cout<<tr("help_use_command")<<"\n";
}
static void printHelpOne(const std::string& name){
  const ToolDefinition* def = REG.find(name);
  if(!def){ std::cout<<trFmt("help_no_such_command", {{"name", name}})<<"\n"; return; }
  const ToolSpec& spec = def->ui;
  std::string summary = localized_tool_summary(spec);
  if(summary.empty()) std::cout<<name<<"\n";
  else std::cout<<name<<" - "<<summary<<"\n";
  std::string helpText = localized_tool_help(spec);
  if(!helpText.empty()) std::cout<<helpText<<"\n";
  if(!spec.subs.empty()){
    std::cout<<tr("help_subcommands")<<"\n";
    for(auto &s:spec.subs){
      std::cout<<"    "<<s.name;
      if(!s.positional.empty()) std::cout<<" "<<joinPositionalPlaceholders(s.positional);
      if(!s.options.empty())    std::cout<<"  [options]";
      std::cout<<"\n";
      if(!s.options.empty()){
        for(auto &o:s.options){
          std::cout<<"      "<<o.name;
          if(o.takesValue) std::cout<<" "<<(o.placeholder.empty()? "<val>":o.placeholder);
          if(o.required)   std::cout<<tr("help_required_tag");
          if(o.isPath)     std::cout<<tr("help_path_tag");
          if(!o.allowedExtensions.empty()){
            auto exts = normalizeExtensions(o.allowedExtensions);
            if(!exts.empty()) std::cout<<" ["<<join(exts, "|")<<"]";
          }
          if(!o.valueSuggestions.empty()){
            std::cout<<"  {"; for(size_t i=0;i<o.valueSuggestions.size();++i){ if(i) std::cout<<","; std::cout<<o.valueSuggestions[i]; } std::cout<<"}";
          }
          std::cout<<"\n";
        }
      }
    }
  }
  if(!spec.options.empty()){
    std::cout<<tr("help_options")<<"\n";
    for(auto &o:spec.options){
      std::cout<<"    "<<o.name;
      if(o.takesValue) std::cout<<" "<<(o.placeholder.empty()? "<val>":o.placeholder);
      if(o.required)   std::cout<<tr("help_required_tag");
      if(o.isPath)     std::cout<<tr("help_path_tag");
      if(!o.allowedExtensions.empty()){
        auto exts = normalizeExtensions(o.allowedExtensions);
        if(!exts.empty()) std::cout<<" ["<<join(exts, "|")<<"]";
      }
      if(!o.valueSuggestions.empty()){
        std::cout<<"  {"; for(size_t i=0;i<o.valueSuggestions.size();++i){ if(i) std::cout<<","; std::cout<<o.valueSuggestions[i]; } std::cout<<"}";
      }
      std::cout<<"\n";
    }
  }
  if(!spec.positional.empty()){
    std::cout<<trFmt("help_positional", {{"value", joinPositionalPlaceholders(spec.positional)}})<<"\n";
  }
}

// ===== Main =====
int main(){
  std::setlocale(LC_CTYPE, "");
  load_settings(settings_file_path());
  apply_settings_to_runtime();
  message_set_watch_folder(g_settings.messageWatchFolder);
  llm_initialize();

  register_prompt_indicator(PromptIndicatorDescriptor{"message", "M"});
  register_prompt_indicator(PromptIndicatorDescriptor{"llm", "L"});

  // 1) 注册内置工具与状态
  register_all_tools();
  register_status_providers();

  // 2) 从当前目录加载动态工具（如 git/pytool）
  const std::string conf = config_file_path("mycli_tools.conf");
  register_tools_from_config(conf);

  // 3) 退出时回车复位
  std::atexit([](){ platform::write_stdout("\r\n", 2); platform::flush_stdout(); });
#ifdef SIGINT
  std::signal(SIGINT,  [](int){ platform::write_stdout("\r\n", 2); std::_Exit(128); });
#endif
#ifdef SIGTERM
  std::signal(SIGTERM, [](int){ platform::write_stdout("\r\n", 2); std::_Exit(128); });
#endif
#ifdef SIGHUP
  std::signal(SIGHUP,  [](int){ platform::write_stdout("\r\n", 2); std::_Exit(128); });
#endif
#ifdef SIGQUIT
  std::signal(SIGQUIT, [](int){ platform::write_stdout("\r\n", 2); std::_Exit(128); });
#endif

  // 4) 原始模式（最小化）
  platform::ensure_virtual_terminal_output();
  platform::TermRaw term; term.enable();

  std::string buf;
  int sel = 0;
  int lastShown = 0;

  message_poll();
  llm_poll();
  bool lastMessageUnread = message_has_unread();
  bool lastLlmUnread = llm_has_unread();

  Candidates cand;
  SplitWord sw;
  int total = 0;
  bool haveCand = false;
  std::string contextGhost;

  bool needRender = true;

  auto renderFrame = [&](){
    std::string status = REG.renderStatusPrefix();
    int status_len = displayWidth(status);

    cand = computeCandidates(buf);
    prioritizeExactMatches(cand);
    sw = splitLastWord(buf);
    total = static_cast<int>(cand.labels.size());
    haveCand = total > 0;
    if(haveCand && sel >= total) sel = 0;
    bool showInlineSuggestion = haveCand;
    if(showInlineSuggestion){
      if(sel >= static_cast<int>(cand.items.size()) || cand.items[sel].rfind(buf, 0) != 0){
        showInlineSuggestion = false;
      }
    }
    contextGhost = (haveCand && showInlineSuggestion) ? std::string() : contextGhostFor(buf);
    auto pathError = detectPathErrorMessage(buf, cand);

    std::cout << ansi::CLR
              << ansi::WHITE << status << ansi::RESET;
    renderPromptLabel();
    std::cout << ansi::WHITE << sw.before << ansi::RESET;
    if(pathError){
      std::cout << ansi::RED << sw.word << ansi::RESET
                << "  " << ansi::YELLOW << "+" << *pathError << ansi::RESET;
    }else if(showInlineSuggestion){
      const std::string& label = cand.labels[sel];
      const std::vector<int>& matches = cand.matchPositions[sel];
      std::string rendered = renderHighlightedLabel(label, matches);
      std::string annotation = (sel < cand.annotations.size()) ? cand.annotations[sel] : "";
      if(!annotation.empty()){
        rendered += " ";
        rendered += ansi::GREEN;
        rendered += annotation;
        rendered += ansi::RESET;
      }
      std::cout << rendered;
    }else{
      std::cout << ansi::WHITE << sw.word << ansi::RESET;
    }
    if(!contextGhost.empty()) std::cout << ansi::GRAY << contextGhost << ansi::RESET;
    std::cout.flush();

    int baseIndent = status_len + promptDisplayWidth() + displayWidth(sw.before);
    int cursorCol = baseIndent;
    if(pathError){
      cursorCol += displayWidth(sw.word);
    }else if(showInlineSuggestion){
      int offset = highlightCursorOffset(cand.labels[sel], cand.matchPositions[sel]);
      if(sel < cand.annotations.size() && !cand.annotations[sel].empty() && sw.word.empty()){
        offset = 0;
      }
      cursorCol += offset;
    }else{
      cursorCol += displayWidth(sw.word);
    }
    cursorCol += 1;

    if(haveCand){
      renderBelowThree(status, status_len, cursorCol, buf, cand, sel, lastShown);
    }else{
      for(int i=0;i<lastShown;i++){ std::cout<<"\n"<<"\x1b[2K"; }
      if(lastShown>0){ std::cout<<ansi::CUU<<lastShown<<"A"<<ansi::CHA<<1<<"G"; }
      std::cout<<ansi::CHA<<cursorCol<<"G"<<std::flush;
      lastShown=0; sel=0;
    }

    needRender = false;
    lastMessageUnread = message_has_unread();
    lastLlmUnread = llm_has_unread();
  };

  while(true){
    if(needRender){
      renderFrame();
    }

    int rc = platform::wait_for_input(200);
    if(rc == 0){
      bool beforeMsg = lastMessageUnread;
      bool beforeLlm = lastLlmUnread;
      message_poll();
      llm_poll();
      bool afterMsg = message_has_unread();
      bool afterLlm = llm_has_unread();
      if(afterMsg != beforeMsg || afterLlm != beforeLlm){
        lastMessageUnread = afterMsg;
        lastLlmUnread = afterLlm;
        needRender = true;
      }
      continue;
    }
    if(rc < 0){
#ifndef _WIN32
      if(errno == EINTR) continue;
#endif
      break;
    }
    char ch;
    if(!platform::read_char(ch)) break;

    if(ch=='\n' || ch=='\r'){
      std::cout << "\n";
      if(!buf.empty()){
        auto tks = splitTokens(buf);
        if(tks[0]=="help"){
          if(tks.size()==1) printHelpAll();
          else printHelpOne(tks[1]);
        }else{
          execToolLine(buf);
          if(!g_parse_error_cmd.empty()){ printHelpOne(g_parse_error_cmd); g_parse_error_cmd.clear(); }
          if(g_should_exit){ std::cout<<ansi::DIM<<"bye"<<ansi::RESET<<"\n"; break; }
        }
      }
      message_poll();
      llm_poll();
      lastMessageUnread = message_has_unread();
      lastLlmUnread = llm_has_unread();
      buf.clear(); sel=0; lastShown=0;
      needRender = true;
      continue;
    }
    if(ch==0x7f){
      if(!buf.empty()){ utf8PopBack(buf); sel=0; needRender = true; }
      continue;
    }
    if(ch=='\t'){
      if(haveCand && total>0){
        auto head=splitLastWord(buf).before;
        buf=head+cand.labels[sel];
        sel=0;
        needRender = true;
      }
      continue;
    }
    if(ch=='\x1b'){
      char seq[2];
      if(!platform::read_char(seq[0])) continue;
      if(!platform::read_char(seq[1])) continue;
      if(seq[0]=='['){
        if(seq[1]=='A'){
          if(haveCand && total>0){
            sel=(sel-1+total)%total;
            needRender = true;
          }
        }else if(seq[1]=='B'){
          if(haveCand && total>0){
            sel=(sel+1)%total;
            needRender = true;
          }
        }
      }
      continue;
    }
    if(static_cast<unsigned char>(ch) >= 0x20){
      buf.push_back(ch);
      sel=0;
      needRender = true;
      continue;
    }
  }

  platform::write_stdout("\r\n", 2); platform::flush_stdout();
  return 0;
}
