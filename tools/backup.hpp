#pragma once

#include "tool_common.hpp"
#include "../utils/json.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>

namespace tool {

struct BackupEntry {
  std::string id;
  std::string label;
  std::string backupPath;
  std::string sourcePath;
  std::string timestamp;
};

struct Backup {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "backup";
    spec.summary = "Create and manage quick backups";
    set_tool_summary_locale(spec, "en", "Create and manage quick backups");
    set_tool_summary_locale(spec, "zh", "快速创建和管理备份");
    spec.help = "backup save [<path>] [-m <mark>] | backup recall [label] | backup delete <label> [-f] | backup clear [-f]";
    set_tool_help_locale(spec, "en", spec.help);
    set_tool_help_locale(spec, "zh", "backup save [<路径>] [-m <备注>] | backup recall [label] | backup delete <label> [-f] | backup clear [-f]");
    spec.subs = {
      SubcommandSpec{"save", {OptionSpec{"-m", true, {}, nullptr, false, "<mark>"}}, {positional("[<path>]", true, PathKind::Any, {}, true)}, {}, nullptr},
      SubcommandSpec{"recall", {}, {positional("[<label>]")}, {}, nullptr},
      SubcommandSpec{"delete", {OptionSpec{"-f", false}}, {positional("<label>")}, {}, nullptr},
      SubcommandSpec{"clear", {OptionSpec{"-f", false}}, {}, {}, nullptr}
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& tokens = request.tokens;
    if(tokens.size() >= 2){
      if(tokens[1] == "save") return handleSave(request, 2);
      if(tokens[1] == "recall") return handleRecall(request);
      if(tokens[1] == "delete") return handleDelete(request);
      if(tokens[1] == "clear") return handleClear(request);
    }
    return handleSave(request, 1);
  }

  static Candidates complete(const std::string& buffer, const std::vector<std::string>& tokens){
    Candidates cand;
    if(tokens.empty() || tokens[0] != "backup") return cand;
    bool trailingSpace = (!buffer.empty() && std::isspace(static_cast<unsigned char>(buffer.back())));
    auto sw = splitLastWord(buffer);
    auto entries = loadEntries();
    auto addEntries = [&](const std::string& word){
      for(const auto& e : entries){
        MatchResult m = compute_match(e.label, word);
        if(!m.matched) continue;
        cand.items.push_back(sw.before + e.label);
        cand.labels.push_back(e.label);
        cand.matchPositions.push_back(m.positions);
        cand.annotations.push_back(e.backupPath);
        cand.exactMatches.push_back(m.exact);
        cand.matchDetails.push_back(m);
      }
    };

    if(tokens.size() == 1){
      std::vector<std::string> subs = {"save", "recall", "delete", "clear"};
      for(const auto& s : subs){
        MatchResult m = compute_match(s, sw.word);
        if(!m.matched) continue;
        cand.items.push_back(sw.before + s);
        cand.labels.push_back(s);
        cand.matchPositions.push_back(m.positions);
        cand.annotations.push_back("");
        cand.exactMatches.push_back(m.exact);
        cand.matchDetails.push_back(m);
      }
      return cand;
    }

    if(tokens.size() == 2 && !trailingSpace){
      std::vector<std::string> subs = {"save", "recall", "delete", "clear"};
      for(const auto& s : subs){
        MatchResult m = compute_match(s, sw.word);
        if(!m.matched) continue;
        cand.items.push_back(sw.before + s);
        cand.labels.push_back(s);
        cand.matchPositions.push_back(m.positions);
        cand.annotations.push_back("");
        cand.exactMatches.push_back(m.exact);
        cand.matchDetails.push_back(m);
      }
      return cand;
    }

    auto savePathCompletion = [&](size_t startIndex) -> std::optional<Candidates> {
      bool enteringMarkValue = false;
      bool havePath = false;
      bool editingPath = false;
      for(size_t i = startIndex; i < tokens.size(); ++i){
        const std::string& tok = tokens[i];
        if(tok == "-m"){
          if(i + 1 >= tokens.size()){
            enteringMarkValue = true;
            break;
          }
          if(i + 1 == tokens.size() - 1 && tokens.back() == sw.word && !trailingSpace){
            enteringMarkValue = true;
            break;
          }
          ++i; // skip mark value
          continue;
        }
        if(!havePath){
          havePath = true;
          if(i == tokens.size() - 1 && (!trailingSpace || tok == sw.word)){
            editingPath = true;
          }
        }
      }
      if(!havePath && !enteringMarkValue && trailingSpace){
        editingPath = true;
      }
      if(editingPath){
        return pathCandidatesForWord(buffer, sw.word, PathKind::Any, nullptr, true);
      }
      return std::nullopt;
    };

    if(tokens.size() >= 2 && tokens[1] == "save"){
      if(auto paths = savePathCompletion(2)) return *paths;
    }

    if(tokens.size() >= 2){
      const std::string& first = tokens[1];
      if(first != "recall" && first != "delete" && first != "clear"){ // implicit save
        if(auto paths = savePathCompletion(1)) return *paths;
      }
    }

    if(tokens.size() >= 2 && (tokens[1] == "recall" || tokens[1] == "delete")){
      bool expectingLabel = false;
      if(tokens.size() == 2){
        expectingLabel = trailingSpace;
      }else{
        expectingLabel = !trailingSpace;
      }
      if(!expectingLabel){
        return cand;
      }
      addEntries(sw.word);
      return cand;
    }

    if(tokens.size() == 2 && tokens[1] == "clear" && trailingSpace){
      return cand;
    }

    return cand;
  }

private:
  static std::filesystem::path backupRoot(){
    return std::filesystem::path(config_home()) / ".backup";
  }

  static ToolExecutionResult textWithTrailingNewline(const std::string& text, int exitCode = 0){
    std::string out = text;
    if(out.empty() || out.back() != '\n') out.push_back('\n');
    return detail::text_result(out, exitCode);
  }

  static std::filesystem::path indexPath(){
    return backupRoot() / "backups.json";
  }

  static std::string timestampNow(){
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    tm = *std::localtime(&tt);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &tm);
    return std::string(buf);
  }

  static std::string sanitize(const std::string& text, const std::string& fallback){
    std::string out;
    out.reserve(text.size());
    for(char ch : text){
      if(std::isalnum(static_cast<unsigned char>(ch)) || ch=='-' || ch=='_' || ch=='.'){
        out.push_back(ch);
      }else if(std::isspace(static_cast<unsigned char>(ch))){
        out.push_back('_');
      }
    }
    if(out.empty()) return fallback;
    return out;
  }

  static std::vector<BackupEntry> loadEntries(){
    std::vector<BackupEntry> entries;
    std::ifstream in(indexPath());
    if(!in) return entries;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if(content.empty()) return entries;
    try{
      sj::Parser parser(content);
      sj::Value root = parser.parse();
      const sj::Value* arrayVal = nullptr;
      if(root.isObject()){
        arrayVal = root.find("entries");
      }else if(root.isArray()){
        arrayVal = &root;
      }
      if(!arrayVal || !arrayVal->isArray()) return entries;
      for(const auto& v : arrayVal->asArray()){
        if(!v.isObject()) continue;
        const auto& obj = v.asObject();
        BackupEntry e;
        if(auto idIt = obj.find("id"); idIt != obj.end() && idIt->second.isString()) e.id = idIt->second.asString();
        if(auto labelIt = obj.find("label"); labelIt != obj.end() && labelIt->second.isString()) e.label = labelIt->second.asString();
        if(auto pathIt = obj.find("backupPath"); pathIt != obj.end() && pathIt->second.isString()) e.backupPath = pathIt->second.asString();
        if(auto srcIt = obj.find("sourcePath"); srcIt != obj.end() && srcIt->second.isString()) e.sourcePath = srcIt->second.asString();
        if(auto tsIt = obj.find("timestamp"); tsIt != obj.end() && tsIt->second.isString()) e.timestamp = tsIt->second.asString();
        if(!e.label.empty() && !e.backupPath.empty()) entries.push_back(std::move(e));
      }
    }catch(...){
      entries.clear();
    }
    return entries;
  }

  static bool saveEntries(const std::vector<BackupEntry>& entries, std::string& error){
    std::error_code ec;
    std::filesystem::create_directories(backupRoot(), ec);
    sj::Array arr;
    for(const auto& e : entries){
      sj::Object obj;
      obj.emplace("id", sj::Value(e.id));
      obj.emplace("label", sj::Value(e.label));
      obj.emplace("backupPath", sj::Value(e.backupPath));
      obj.emplace("sourcePath", sj::Value(e.sourcePath));
      obj.emplace("timestamp", sj::Value(e.timestamp));
      arr.push_back(sj::Value(std::move(obj)));
    }
    sj::Object root;
    root.emplace("entries", sj::Value(std::move(arr)));
    std::ofstream out(indexPath());
    if(!out){
      error = "failed to open backup index";
      return false;
    }
    out << sj::dump(sj::Value(std::move(root)), 2);
    if(!out){
      error = "failed to write backup index";
      return false;
    }
    return true;
  }

  static BackupEntry* findEntry(std::vector<BackupEntry>& entries, const std::string& key){
    for(auto& e : entries){
      if(e.id == key || e.label == key) return &e;
    }
    return nullptr;
  }

  static std::string nextId(const std::vector<BackupEntry>& entries){
    long long maxId = 0;
    for(const auto& e : entries){
      try{
        long long val = std::stoll(e.id);
        if(val > maxId) maxId = val;
      }catch(...){
        continue;
      }
    }
    return std::to_string(maxId + 1);
  }

  static bool confirmDangerous(const std::string& prompt, bool force, bool allowPrompt){
    if(force) return true;
    if(!allowPrompt) return false;
    static const std::vector<std::string> yesNoSuggestions = {"y", "yes", "n", "no"};
    detail::InteractiveLineOptions options;
    options.prompt = prompt + " [y/N]: ";
    options.suggestions = &yesNoSuggestions;
    options.maxLength = 8;
    options.maxLengthSuffix = startsWith(g_settings.language, "zh") ? " 长度已达上限" : " length limit";
    options.suggestionRows = 3;
    std::string line;
    auto status = detail::read_interactive_line(options, line);
    if(status != detail::InteractiveLineStatus::Ok) return false;
    for(auto& ch : line) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return line == "y" || line == "yes";
  }

  static ToolExecutionResult handleSave(const ToolExecutionRequest& request, size_t startIndex){
    const auto& tokens = request.tokens;
    std::string mark;
    std::optional<std::string> targetArg;
    for(size_t i = startIndex; i < tokens.size(); ++i){
      if(tokens[i] == "-m"){ if(i + 1 >= tokens.size()) { g_parse_error_cmd = "backup"; return detail::text_result("backup: -m requires a value\n", 1);} mark = tokens[++i]; }
      else if(!tokens[i].empty() && tokens[i][0] == '-'){
        g_parse_error_cmd = "backup"; return detail::text_result("unknown option: " + tokens[i] + "\n", 1);
      }else{
        if(!targetArg) targetArg = tokens[i];
        else { g_parse_error_cmd = "backup"; return detail::text_result("usage: backup save [<path>] [-m <mark>]\n", 1); }
      }
    }

    std::filesystem::path source = targetArg ? std::filesystem::path(*targetArg) : std::filesystem::path(".");
    std::error_code ec;
    source = std::filesystem::absolute(source, ec);
    if(ec){
      return detail::text_result("backup: failed to resolve path\n", 1);
    }
    if(!std::filesystem::exists(source)){
      return detail::text_result("backup: source does not exist\n", 1);
    }

    std::string base = source.filename().string();
    if(base.empty()) base = "backup";
    std::string cleanBase = sanitize(base, "backup");
    std::string cleanMark = sanitize(mark, "");
    std::string ts = timestampNow();
    std::vector<BackupEntry> entries = loadEntries();
    std::string id = nextId(entries);

    std::string label = cleanBase;
    if(!cleanMark.empty()) label += "-" + cleanMark;
    label += "-" + ts;

    std::filesystem::path dest = backupRoot() / (cleanBase + "-" + ts);
    std::filesystem::create_directories(dest.parent_path(), ec);
    if(ec){
      return detail::text_result("backup: failed to create backup folder\n", 1);
    }

    if(std::filesystem::is_directory(source)){
      std::filesystem::create_directories(dest, ec);
      if(ec){
        return detail::text_result("backup: failed to prepare directory\n", 1);
      }
      std::filesystem::copy(source, dest, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    }else{
      std::filesystem::copy_file(source, dest, std::filesystem::copy_options::overwrite_existing, ec);
    }
    if(ec){
      return detail::text_result("backup: failed to copy source\n", 1);
    }

    BackupEntry entry;
    entry.id = id;
    entry.label = label;
    entry.backupPath = std::filesystem::absolute(dest).string();
    entry.sourcePath = source.string();
    entry.timestamp = ts;
    entries.push_back(entry);
    std::string error;
    if(!saveEntries(entries, error)){
      return detail::text_result("backup: " + error + "\n", 1);
    }

    std::ostringstream oss;
    oss << "Backup saved: " << entry.backupPath << "\n";
    oss << "Label: " << entry.label << "\n";
    return detail::text_result(oss.str());
  }

  static ToolExecutionResult handleRecall(const ToolExecutionRequest& request){
    const auto& tokens = request.tokens;
    std::vector<BackupEntry> entries = loadEntries();
    if(tokens.size() < 3){
      if(entries.empty()) return textWithTrailingNewline("no backups found", 1);
      std::ostringstream oss;
      oss << "Saved backups:" << "\n";
      for(const auto& e : entries){
        oss << "- " << e.label << " -> " << e.backupPath << "\n";
      }
      return textWithTrailingNewline(oss.str());
    }
    if(tokens.size() > 3){
      g_parse_error_cmd = "backup";
      return textWithTrailingNewline("usage: backup recall [label]", 1);
    }

    std::string key = tokens[2];
    BackupEntry* entry = findEntry(entries, key);
    if(!entry){
      return textWithTrailingNewline("backup not found", 1);
    }
    std::ostringstream oss;
    oss << entry->backupPath << "\n";
    return textWithTrailingNewline(oss.str());
  }

  static ToolExecutionResult handleDelete(const ToolExecutionRequest& request){
    const auto& tokens = request.tokens;
    bool force = false;
    std::optional<std::string> keyOpt;
    for(size_t i = 2; i < tokens.size(); ++i){
      if(tokens[i] == "-f") force = true;
      else if(!tokens[i].empty() && tokens[i][0] == '-'){
        g_parse_error_cmd = "backup"; return detail::text_result("unknown option: " + tokens[i] + "\n", 1);
      }else if(!keyOpt) keyOpt = tokens[i];
      else { g_parse_error_cmd = "backup"; return detail::text_result("usage: backup delete <label> [-f]\n", 1); }
    }
    if(!keyOpt){
      g_parse_error_cmd = "backup";
      return detail::text_result("usage: backup delete <label> [-f]\n", 1);
    }
    std::vector<BackupEntry> entries = loadEntries();
    BackupEntry* entry = findEntry(entries, *keyOpt);
    if(!entry){
      return detail::text_result("backup not found\n", 1);
    }

    bool allowPrompt = !(request.silent || request.forLLM);
    if(!confirmDangerous("Delete backup " + entry->label + "?", force, allowPrompt)){
      return detail::text_result("delete cancelled\n", 1);
    }

    std::filesystem::path removePath = entry->backupPath;
    if(!removePath.empty()){
      std::filesystem::path parent = removePath.parent_path();
      if(!parent.empty()) removePath = parent;
    }
    std::error_code ec;
    if(!removePath.empty()){
      std::filesystem::remove_all(removePath, ec);
    }
    std::string removeId = entry->id;
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const BackupEntry& e){ return e.id == removeId; }), entries.end());
    std::string error;
    if(!saveEntries(entries, error)){
      return detail::text_result("backup: " + error + "\n", 1);
    }
    return detail::text_result("backup deleted\n");
  }

  static ToolExecutionResult handleClear(const ToolExecutionRequest& request){
    const auto& tokens = request.tokens;
    bool force = false;
    for(size_t i = 2; i < tokens.size(); ++i){
      if(tokens[i] == "-f") force = true;
      else { g_parse_error_cmd = "backup"; return detail::text_result("usage: backup clear [-f]\n", 1); }
    }
    bool allowPrompt = !(request.silent || request.forLLM);
    if(!confirmDangerous("Clear all backups?", force, allowPrompt)){
      return detail::text_result("clear cancelled\n", 1);
    }
    std::error_code ec;
    std::filesystem::remove_all(backupRoot(), ec);
    if(ec){
      return detail::text_result("backup: failed to clear backups\n", 1);
    }
    return detail::text_result("all backups cleared\n");
  }
};

inline ToolDefinition make_backup_tool(){
  ToolDefinition def;
  def.ui = Backup::ui();
  def.executor = Backup::run;
  def.completion = Backup::complete;
  return def;
}

} // namespace tool
