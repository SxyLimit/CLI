#pragma once

#include "tool_common.hpp"
#include "../utils/json.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace tool {

struct TodoDetailEntry {
  long long ts = 0;
  std::string text;
};

struct TodoTask {
  std::string name;
  long long createdAt = 0;
  long long updatedAt = 0;
  long long startAt = 0;
  long long deadlineAt = 0;
  long long repeatSeconds = 0;
  std::string repeatExpr;
  std::vector<std::string> todoItems;
  std::vector<TodoDetailEntry> details;
};

struct TodoResolvedTiming {
  long long startAt = 0;
  long long deadlineAt = 0;
  bool hasDeadline = false;
};

struct TodoIndicatorSnapshot {
  bool hasActive = false;
  int urgencyLevel = 0; // 0: none, 1: <=5m, 2: <=1m
  int urgentCount = 0;
  long long nearestDeadline = 0;
};

struct Todo {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "todo";
    spec.summary = "Manage tasks with time-aware scheduling";
    set_tool_summary_locale(spec, "en", "Manage tasks with time-aware scheduling");
    set_tool_summary_locale(spec, "zh", "管理带时间调度的任务");
    spec.help =
      "todo create <name> [--start <time>] [--deadline <time>] [--repeat <expr>] [--no-edit] [-c]\n"
      "todo update <name> <add|start|deadline|edit> ... [-c]\n"
      "todo edit <name> [-c]\n"
      "todo delete <name> [per] [-f]\n"
      "todo query [<+time>] | todo today [deadline]\n"
      "todo detail <name> [-c] | todo last <name> [-c] | todo finished [--purge] [-f]";
    set_tool_help_locale(spec, "en", spec.help);
    set_tool_help_locale(spec, "zh",
      "todo create <名称> [--start <时间>] [--deadline <时间>] [--repeat <循环>] [--no-edit] [-c]\n"
      "todo update <名称> <add|start|deadline|edit> ... [-c]\n"
      "todo edit <名称> [-c]\n"
      "todo delete <名称> [per] [-f]\n"
      "todo query [<+时间>] | todo today [deadline]\n"
      "todo detail <名称> [-c] | todo last <名称> [-c] | todo finished [--purge] [-f]");
    spec.subs = {
      SubcommandSpec{
        "create",
        {
          OptionSpec{"--start", true, {}, nullptr, false, "<time>"},
          OptionSpec{"--deadline", true, {}, nullptr, false, "<time>"},
          OptionSpec{"--repeat", true, {"d", "w", "m", "y", "2d", "2w", "2m", "2y"}, nullptr, false, "<expr>"},
          OptionSpec{"--edit", false},
          OptionSpec{"--no-edit", false},
          OptionSpec{"-c", false}
        },
        {positional("<name>")},
        {},
        nullptr
      },
      SubcommandSpec{
        "update",
        {OptionSpec{"-c", false}},
        {positional("<name>"), positional("[add|start|deadline|edit]"), positional("[value...]")},
        {},
        nullptr
      },
      SubcommandSpec{"edit", {OptionSpec{"-c", false}}, {positional("<name>")}, {}, nullptr},
      SubcommandSpec{
        "delete",
        {OptionSpec{"-f", false}},
        {positional("<name>"), positional("[per]")},
        {},
        nullptr
      },
      SubcommandSpec{"query", {}, {positional("[+time]")}, {}, nullptr},
      SubcommandSpec{"today", {}, {positional("[deadline]")}, {}, nullptr},
      SubcommandSpec{"detail", {OptionSpec{"-c", false}}, {positional("<name>")}, {}, nullptr},
      SubcommandSpec{"last", {OptionSpec{"-c", false}}, {positional("<name>")}, {}, nullptr},
      SubcommandSpec{
        "finished",
        {OptionSpec{"--purge", false}, OptionSpec{"-f", false}},
        {},
        {},
        nullptr
      }
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    if(request.tokens.size() < 2){
      g_parse_error_cmd = "todo";
      return detail::text_result("usage: todo <create|update|edit|delete|query|today|detail|last|finished> ...\n", 1);
    }
    const std::string& sub = request.tokens[1];
    if(sub == "create") return handleCreate(request);
    if(sub == "update") return handleUpdate(request);
    if(sub == "edit") return handleEdit(request);
    if(sub == "delete") return handleDelete(request);
    if(sub == "query") return handleQuery(request);
    if(sub == "today") return handleToday(request);
    if(sub == "detail") return handleDetail(request);
    if(sub == "last") return handleLast(request);
    if(sub == "finished") return handleFinished(request);
    g_parse_error_cmd = "todo";
    return detail::text_result("unknown todo subcommand: " + sub + "\n", 1);
  }

  static Candidates complete(const std::string& buffer, const std::vector<std::string>& tokens){
    Candidates cand;
    if(tokens.empty() || tokens[0] != "todo") return cand;

    const bool trailingSpace = (!buffer.empty() && std::isspace(static_cast<unsigned char>(buffer.back())));
    const auto sw = splitLastWord(buffer);
    auto tasks = loadTasks();

    auto addCandidate = [&](const std::string& label, const std::string& annotation = ""){
      MatchResult match = compute_match(label, sw.word);
      if(!match.matched) return;
      cand.items.push_back(sw.before + label);
      cand.labels.push_back(label);
      cand.matchPositions.push_back(match.positions);
      cand.annotations.push_back(annotation);
      cand.exactMatches.push_back(match.exact);
      cand.matchDetails.push_back(match);
    };

    auto addFromList = [&](const std::vector<std::string>& values){
      for(const auto& value : values){
        addCandidate(value);
      }
    };

    auto addTaskNames = [&](){
      long long now = nowSeconds();
      std::vector<std::string> names;
      names.reserve(tasks.size());
      for(const auto& task : tasks){
        names.push_back(task.name);
      }
      std::sort(names.begin(), names.end());
      names.erase(std::unique(names.begin(), names.end()), names.end());
      for(const auto& name : names){
        const TodoTask* task = findTaskConst(tasks, name);
        std::string annotation;
        if(task){
          TodoResolvedTiming timing = resolveTiming(*task, now);
          if(timing.hasDeadline){
            annotation = formatTime(timing.deadlineAt);
          }else{
            annotation = "no deadline";
          }
        }
        addCandidate(name, annotation);
      }
    };

    const std::vector<std::string> subs = {
      "create", "update", "edit", "delete", "query", "today", "detail", "last", "finished"
    };
    const std::vector<std::string> repeatSuggestions = {
      "d", "w", "m", "y",
      "2d", "2w", "2m", "2y",
      "3d", "7d", "14d",
      "per d", "per w", "per m", "per y",
      "per 2d", "per 2w", "per 2m", "per 2y"
    };

    if(tokens.size() == 1 || (tokens.size() == 2 && !trailingSpace)){
      addFromList(subs);
      sortCandidatesByMatch(sw.word, cand);
      return cand;
    }

    const std::string sub = (tokens.size() >= 2) ? tokens[1] : "";
    const std::string prev = (!trailingSpace && tokens.size() >= 2) ? tokens[tokens.size() - 2]
                                                                     : (tokens.empty() ? std::string() : tokens.back());

    if(sub == "create"){
      if(prev == "--start" || prev == "--deadline"){
        addFromList(makeAdaptiveTimeSuggestions(sw.word, prev == "--deadline"));
        sortCandidatesByMatch(sw.word, cand);
        return cand;
      }
      if(prev == "--repeat"){
        addFromList(repeatSuggestions);
        sortCandidatesByMatch(sw.word, cand);
        return cand;
      }
      if(tokens.size() >= 5 && tokens[tokens.size() - 2] == "--repeat" && prev == "per"){
        addFromList({"d", "w", "m", "y", "2d", "2w", "2m", "2y", "3d", "7d", "14d"});
        sortCandidatesByMatch(sw.word, cand);
        return cand;
      }

      std::vector<std::string> opts = {"--start", "--deadline", "--repeat", "--edit", "--no-edit", "-c"};
      std::set<std::string> used;
      for(size_t i = 3; i < tokens.size(); ++i){
        if(startsWith(tokens[i], "-")) used.insert(tokens[i]);
      }
      bool suggestOptions = startsWith(sw.word, "-") || trailingSpace;
      if(suggestOptions){
        for(const auto& opt : opts){
          if(used.count(opt)) continue;
          addCandidate(opt);
        }
      }
      sortCandidatesByMatch(sw.word, cand);
      return cand;
    }

    if(sub == "update"){
      if((tokens.size() == 2 && trailingSpace) || (tokens.size() == 3 && !trailingSpace)){
        addTaskNames();
        sortCandidatesByMatch(sw.word, cand);
        return cand;
      }
      if((trailingSpace || startsWith(sw.word, "-"))){
        addCandidate("-c");
      }
      if((tokens.size() == 3 && trailingSpace) || (tokens.size() == 4 && !trailingSpace)){
        addFromList({"add", "start", "deadline", "edit"});
        sortCandidatesByMatch(sw.word, cand);
        return cand;
      }
      if(tokens.size() >= 4){
        const std::string action = tokens[3];
        if(action == "start" || action == "deadline"){
          bool expectingTime = false;
          if(tokens.size() == 4 && trailingSpace) expectingTime = true;
          if(tokens.size() == 5 && !trailingSpace) expectingTime = true;
          if(expectingTime){
            addFromList(makeAdaptiveTimeSuggestions(sw.word));
            sortCandidatesByMatch(sw.word, cand);
            return cand;
          }
        }
      }
      sortCandidatesByMatch(sw.word, cand);
      return cand;
    }

    if(sub == "edit" || sub == "detail" || sub == "last"){
      if((tokens.size() == 2 && trailingSpace) || (tokens.size() == 3 && !trailingSpace)){
        addTaskNames();
      }
      if((trailingSpace || startsWith(sw.word, "-"))){
        addCandidate("-c");
      }
      sortCandidatesByMatch(sw.word, cand);
      return cand;
    }

    if(sub == "delete"){
      if((tokens.size() == 2 && trailingSpace) || (tokens.size() == 3 && !trailingSpace)){
        addTaskNames();
        sortCandidatesByMatch(sw.word, cand);
        return cand;
      }
      std::optional<std::string> targetName;
      bool hasForce = false;
      bool hasPer = false;
      for(size_t i = 2; i < tokens.size(); ++i){
        if(tokens[i] == "-f") hasForce = true;
        else if(tokens[i] == "per") hasPer = true;
        else if(!startsWith(tokens[i], "-") && !targetName) targetName = tokens[i];
      }
      bool canPer = false;
      if(targetName){
        const TodoTask* task = findTaskConst(tasks, *targetName);
        canPer = (task && task->repeatSeconds > 0);
      }
      if((trailingSpace || startsWith(sw.word, "-")) && !hasForce){
        addCandidate("-f");
      }
      if((trailingSpace || !startsWith(sw.word, "-")) && canPer && !hasPer){
        addCandidate("per");
      }
      sortCandidatesByMatch(sw.word, cand);
      return cand;
    }

    if(sub == "query"){
      if((tokens.size() == 2 && trailingSpace) || (tokens.size() == 3 && !trailingSpace)){
        addFromList({"+30m", "+1h", "+4h", "+1d", "+3d", "+7d"});
      }
      sortCandidatesByMatch(sw.word, cand);
      return cand;
    }

    if(sub == "today"){
      if((tokens.size() == 2 && trailingSpace) || (tokens.size() == 3 && !trailingSpace)){
        addCandidate("deadline");
      }
      sortCandidatesByMatch(sw.word, cand);
      return cand;
    }

    if(sub == "finished"){
      if((trailingSpace || startsWith(sw.word, "-"))){
        addCandidate("--purge");
        addCandidate("-f");
      }
      sortCandidatesByMatch(sw.word, cand);
      return cand;
    }

    sortCandidatesByMatch(sw.word, cand);
    return cand;
  }

  static TodoIndicatorSnapshot indicatorSnapshot(long long now = 0){
    TodoIndicatorSnapshot snapshot;
    if(now <= 0) now = nowSeconds();
    auto tasks = loadTasks();
    for(const auto& task : tasks){
      TodoResolvedTiming timing = resolveTiming(task, now);
      bool active = false;
      if(task.repeatSeconds > 0){
        active = true;
      }else if(!timing.hasDeadline){
        active = true;
      }else{
        active = timing.deadlineAt >= now;
      }
      if(!active) continue;
      snapshot.hasActive = true;
      if(timing.hasDeadline){
        if(snapshot.nearestDeadline <= 0 || timing.deadlineAt < snapshot.nearestDeadline){
          snapshot.nearestDeadline = timing.deadlineAt;
        }
        long long delta = timing.deadlineAt - now;
        if(delta >= 0 && delta <= 5 * 60){
          snapshot.urgentCount += 1;
          if(delta <= 60){
            snapshot.urgencyLevel = std::max(snapshot.urgencyLevel, 2);
          }else{
            snapshot.urgencyLevel = std::max(snapshot.urgencyLevel, 1);
          }
        }
      }
    }
    return snapshot;
  }

private:
  struct TodoListItem {
    const TodoTask* task = nullptr;
    TodoResolvedTiming timing;
  };

  struct CreateInteractiveInput {
    std::string name;
    std::optional<std::string> startExpr;
    std::optional<std::string> deadlineExpr;
    std::optional<std::string> repeatExpr;
    bool editAfterCreate = true;
    bool cancelled = false;
  };

  struct TodoEditorPayload {
    long long startAt = 0;
    long long deadlineAt = 0;
    long long repeatSeconds = 0;
    std::string repeatExpr;
    std::vector<std::string> todoItems;
  };

  static std::filesystem::path todoRoot(){
    return std::filesystem::path(config_home()) / "todo";
  }

  static std::filesystem::path todoIndexPath(){
    return todoRoot() / "todo.json";
  }

  static std::filesystem::path todoNamePath(){
    return todoRoot() / "name.tdle";
  }

  static std::filesystem::path todoOperationPath(){
    return todoRoot() / "operation.tdle";
  }

  static std::filesystem::path todoDetailsDir(){
    return todoRoot() / "Details";
  }

  static std::filesystem::path todoDetailFilePath(const std::string& name){
    return todoDetailsDir() / (name + ".json");
  }

  static long long nowSeconds(){
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  }

  static std::string trimCopy(const std::string& value){
    size_t left = 0;
    size_t right = value.size();
    while(left < right && std::isspace(static_cast<unsigned char>(value[left]))) ++left;
    while(right > left && std::isspace(static_cast<unsigned char>(value[right - 1]))) --right;
    return value.substr(left, right - left);
  }

  static std::string toLowerCopy(std::string value){
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch){
      return static_cast<char>(std::tolower(ch));
    });
    return value;
  }

  static bool localeIsZh(){
    std::string lang = toLowerCopy(g_settings.language);
    return startsWith(lang, "zh");
  }

  static std::string localeText(const std::string& en, const std::string& zh){
    return localeIsZh() ? zh : en;
  }

  static bool isValidName(const std::string& name){
    if(name.empty()) return false;
    for(char ch : name){
      unsigned char uch = static_cast<unsigned char>(ch);
      if(!(std::isalnum(uch) || ch == '_')) return false;
    }
    return true;
  }

  static std::string compactTimestamp(long long ts){
    std::time_t tt = static_cast<std::time_t>(ts);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    tm = *std::localtime(&tt);
#endif
    char buf[32];
    if(std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &tm) == 0){
      return "00000000000000";
    }
    return std::string(buf);
  }

  static std::string formatDate(long long ts){
    std::time_t tt = static_cast<std::time_t>(ts);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    tm = *std::localtime(&tt);
#endif
    char buf[32];
    if(std::strftime(buf, sizeof(buf), "%Y.%m.%d", &tm) == 0){
      return "1970.01.01";
    }
    return std::string(buf);
  }

  static std::string formatClockMinute(long long ts){
    std::time_t tt = static_cast<std::time_t>(ts);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    tm = *std::localtime(&tt);
#endif
    char buf[16];
    if(std::strftime(buf, sizeof(buf), "%H:%M", &tm) == 0){
      return "00:00";
    }
    return std::string(buf);
  }

  static std::string formatDateMinute(long long ts){
    std::time_t tt = static_cast<std::time_t>(ts);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    tm = *std::localtime(&tt);
#endif
    char buf[32];
    if(std::strftime(buf, sizeof(buf), "%Y.%m.%d_%H:%M", &tm) == 0){
      return "1970.01.01_00:00";
    }
    return std::string(buf);
  }

  static std::string formatTime(long long ts){
    if(ts <= 0) return "none";
    std::time_t tt = static_cast<std::time_t>(ts);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    tm = *std::localtime(&tt);
#endif
    char buf[64];
    if(std::strftime(buf, sizeof(buf), "%Y.%m.%d %H:%M:%S", &tm) == 0){
      return "none";
    }
    return std::string(buf);
  }

  static void appendUnique(std::vector<std::string>& items, const std::string& value){
    if(value.empty()) return;
    if(std::find(items.begin(), items.end(), value) != items.end()) return;
    items.push_back(value);
  }

  static bool isDigitsOnly(const std::string& value){
    return !value.empty() &&
           std::all_of(value.begin(), value.end(),
                       [](unsigned char ch){ return std::isdigit(ch) != 0; });
  }

  static std::vector<std::string> splitKeepEmpty(const std::string& text, char sep){
    std::vector<std::string> parts;
    size_t start = 0;
    while(start <= text.size()){
      size_t pos = text.find(sep, start);
      if(pos == std::string::npos){
        parts.push_back(text.substr(start));
        break;
      }
      parts.push_back(text.substr(start, pos - start));
      start = pos + 1;
    }
    return parts;
  }

  static int daysInMonth(int year, int month){
    if(year < 1 || year > 9999 || month < 1 || month > 12) return 31;
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month;
    tm.tm_mday = 0;
    tm.tm_hour = 12;
    tm.tm_isdst = -1;
    std::time_t tt = std::mktime(&tm);
    if(tt == static_cast<std::time_t>(-1)) return 31;
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &tt);
#else
    out = *std::localtime(&tt);
#endif
    if(out.tm_mday < 28 || out.tm_mday > 31) return 31;
    return out.tm_mday;
  }

  static std::string twoDigit(int value){
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << value;
    return oss.str();
  }

  static std::string normalizeTimeQuery(const std::string& raw){
    std::string value = trimCopy(raw);
    for(char& ch : value){
      if(ch == '-') ch = '.';
      else if(ch == '/') ch = '.';
      else if(ch == 'T' || ch == ' ') ch = '_';
    }
    return value;
  }

  static void appendTimePrefixSuggestions(const std::string& rawInput,
                                          std::vector<std::string>& suggestions){
    const std::string query = normalizeTimeQuery(rawInput);
    if(query.empty()) return;
    if(!std::isdigit(static_cast<unsigned char>(query.front()))) return;
    for(char ch : query){
      if(!(std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == '_' || ch == ':')){
        return;
      }
    }

    const size_t underscorePos = query.find('_');
    if(underscorePos != std::string::npos && query.find('_', underscorePos + 1) != std::string::npos){
      return;
    }
    const std::string datePart = (underscorePos == std::string::npos) ? query : query.substr(0, underscorePos);
    const std::vector<std::string> dateParts = splitKeepEmpty(datePart, '.');
    if(dateParts.empty()) return;

    const std::string yearPart = dateParts[0];
    if(yearPart.empty() || !isDigitsOnly(yearPart)) return;

    std::time_t nowT = static_cast<std::time_t>(nowSeconds());
    std::tm nowTm{};
#ifdef _WIN32
    localtime_s(&nowTm, &nowT);
#else
    nowTm = *std::localtime(&nowT);
#endif
    const int currentYear = nowTm.tm_year + 1900;
    const int currentMonth = nowTm.tm_mon + 1;
    const int currentDay = nowTm.tm_mday;

    auto addIfPrefix = [&](const std::string& candidate){
      if(ciStartsWith(candidate, query)){
        appendUnique(suggestions, candidate);
      }
    };

    if(yearPart.size() < 4){
      for(int year : {currentYear - 1, currentYear, currentYear + 1, currentYear + 2}){
        if(year < 1 || year > 9999) continue;
        std::string y = std::to_string(year);
        if(ciStartsWith(y, yearPart)){
          addIfPrefix(y + ".01");
          addIfPrefix(y + ".06");
          addIfPrefix(y + ".12");
        }
      }
      return;
    }
    if(yearPart.size() > 4) return;

    int year = 0;
    try{
      year = std::stoi(yearPart);
    }catch(...){
      return;
    }
    if(year < 1 || year > 9999) return;

    const std::string monthPart = (dateParts.size() >= 2) ? dateParts[1] : "";
    if(dateParts.size() == 1){
      for(int month : {1, 3, 6, 9, 12}){
        addIfPrefix(yearPart + "." + twoDigit(month));
      }
      addIfPrefix(yearPart + "." + twoDigit(currentMonth));
      return;
    }
    if(!monthPart.empty() && !isDigitsOnly(monthPart)) return;
    if(monthPart.size() > 2) return;

    std::vector<int> matchingMonths;
    for(int month = 1; month <= 12; ++month){
      std::string mm = twoDigit(month);
      if(monthPart.empty() || startsWith(mm, monthPart)){
        matchingMonths.push_back(month);
      }
    }
    if(matchingMonths.empty()) return;

    for(int month : matchingMonths){
      addIfPrefix(yearPart + "." + twoDigit(month));
    }

    if(dateParts.size() > 3) return;
    const bool hasDayPart = dateParts.size() >= 3;
    const std::string dayPart = hasDayPart ? dateParts[2] : "";
    if(hasDayPart && !dayPart.empty() && !isDigitsOnly(dayPart)) return;
    if(dayPart.size() > 2) return;

    for(int month : matchingMonths){
      const int dim = daysInMonth(year, month);
      std::vector<int> days;
      auto pushDay = [&](int day){
        if(day < 1 || day > dim) return;
        if(std::find(days.begin(), days.end(), day) == days.end()){
          days.push_back(day);
        }
      };

      if(!hasDayPart || dayPart.empty()){
        for(int d : {1, 2, 3, 7, 10, 15, 20, dim}){
          pushDay(d);
        }
        if(year == currentYear && month == currentMonth){
          pushDay(currentDay);
        }
      }else if(dayPart.size() < 2){
        for(int d = 1; d <= dim; ++d){
          std::string dd = twoDigit(d);
          if(startsWith(dd, dayPart)){
            pushDay(d);
          }
        }
      }else{
        int day = 0;
        try{
          day = std::stoi(dayPart);
        }catch(...){
          day = 0;
        }
        pushDay(day);
      }

      for(int day : days){
        const std::string dateValue = yearPart + "." + twoDigit(month) + "." + twoDigit(day);
        addIfPrefix(dateValue);
        for(const char* hhmm : {"00:00", "08:00", "09:00", "12:00", "15:00", "18:00", "21:00", "23:59"}){
          addIfPrefix(dateValue + "_" + hhmm);
        }
      }
    }
  }

  static std::vector<std::string> makeTimeSuggestions(){
    long long now = nowSeconds();
    std::vector<std::string> suggestions;
    appendUnique(suggestions, "now");
    appendUnique(suggestions, "today");
    appendUnique(suggestions, "tomorrow");
    for(int minute : {15, 30, 45, 60, 90, 120, 180, 240}){
      appendUnique(suggestions, "+" + std::to_string(minute) + "m");
      appendUnique(suggestions, "+" + std::to_string(minute) + "min");
    }
    for(int day : {1, 2, 3, 7}){
      appendUnique(suggestions, "+" + std::to_string(day) + "d");
    }

    appendUnique(suggestions, formatDate(now));
    appendUnique(suggestions, formatDate(now) + "_09:00");
    appendUnique(suggestions, formatDate(now) + "_18:00");
    appendUnique(suggestions, formatDate(now) + "_23:59");

    long long minuteFloor = now - (now % 60);
    appendUnique(suggestions, formatClockMinute(minuteFloor));
    appendUnique(suggestions, formatDateMinute(minuteFloor));

    long long firstQuarter = minuteFloor;
    std::time_t firstT = static_cast<std::time_t>(firstQuarter);
    std::tm firstTm{};
#ifdef _WIN32
    localtime_s(&firstTm, &firstT);
#else
    firstTm = *std::localtime(&firstT);
#endif
    int quarterMod = firstTm.tm_min % 15;
    if(quarterMod != 0 || firstTm.tm_sec != 0){
      int advance = (15 - quarterMod) % 15;
      if(advance == 0 && firstTm.tm_sec != 0) advance = 15;
      firstQuarter += static_cast<long long>(advance) * 60;
      if(firstTm.tm_sec != 0){
        firstQuarter += (60 - firstTm.tm_sec);
      }
    }
    for(int i = 0; i < 16; ++i){
      long long slot = firstQuarter + static_cast<long long>(i) * 15 * 60;
      appendUnique(suggestions, formatClockMinute(slot));
      appendUnique(suggestions, formatDateMinute(slot));
    }
    return suggestions;
  }

  static bool tryParseDateOnlyInput(const std::string& raw, long long& dayStartOut){
    std::string value = trimCopy(raw);
    if(value.empty()) return false;
    for(char& ch : value){
      if(ch == '-') ch = '.';
      if(ch == '/') ch = '.';
    }
    if(value.find(' ') != std::string::npos || value.find('T') != std::string::npos ||
       value.find('_') != std::string::npos || value.find(':') != std::string::npos){
      return false;
    }
    auto allDigits = [](const std::string& s){
      return !s.empty() &&
             std::all_of(s.begin(), s.end(), [](unsigned char ch){ return std::isdigit(ch) != 0; });
    };
    if(value.size() == 10 && value[4] == '.' && value[7] == '.'){
      if(!allDigits(value.substr(0, 4)) || !allDigits(value.substr(5, 2)) || !allDigits(value.substr(8, 2))){
        return false;
      }
      return parseAbsoluteTime(value, dayStartOut);
    }
    if(value.size() == 8 && allDigits(value)){
      return parseCompactDateTime(value, dayStartOut);
    }
    return false;
  }

  static std::vector<std::string> makeAdaptiveTimeSuggestions(const std::string& rawInput,
                                                              bool includeNone = false){
    std::vector<std::string> suggestions = makeTimeSuggestions();
    const std::string query = trimCopy(rawInput);
    const long long now = nowSeconds();

    if(!query.empty()){
      long long parsed = 0;
      std::string normalized;
      if(parseTimeExpr(query, now, parsed, &normalized)){
        long long minuteFloor = parsed - (parsed % 60);
        appendUnique(suggestions, formatDate(parsed));
        appendUnique(suggestions, formatDateMinute(minuteFloor));
        for(int delta : {15, 30, 45, 60, 90, 120, 180, 240}){
          long long slot = minuteFloor + static_cast<long long>(delta) * 60;
          appendUnique(suggestions, formatClockMinute(slot));
          appendUnique(suggestions, formatDateMinute(slot));
        }
      }

      long long dayStart = 0;
      if(tryParseDateOnlyInput(query, dayStart)){
        const std::string day = formatDate(dayStart);
        appendUnique(suggestions, day);
        for(const char* hhmm : {"00:00", "08:00", "09:00", "12:00", "15:00", "18:00", "21:00", "23:59"}){
          appendUnique(suggestions, day + "_" + hhmm);
        }

        long long anchor = dayStart + 9 * 3600;
        if(formatDate(now) == day){
          anchor = std::max(dayStart, now - (now % 60));
        }
        const long long dayEnd = dayStart + 86399;
        for(int i = 0; i < 16; ++i){
          long long slot = anchor + static_cast<long long>(i) * 15 * 60;
          if(slot > dayEnd) break;
          appendUnique(suggestions, formatDateMinute(slot));
        }

        const long long nextDay = dayStart + 86400;
        appendUnique(suggestions, formatDate(nextDay));
        appendUnique(suggestions, formatDate(nextDay) + "_09:00");
        appendUnique(suggestions, formatDate(nextDay) + "_18:00");
      }

      appendTimePrefixSuggestions(query, suggestions);

      const std::string loweredQuery = toLowerCopy(query);
      suggestions.erase(std::remove_if(suggestions.begin(), suggestions.end(),
                                       [&](const std::string& item){
                                         return toLowerCopy(item) == loweredQuery;
                                       }),
                        suggestions.end());
    }

    if(includeNone){
      appendUnique(suggestions, "none");
    }
    return suggestions;
  }

  static bool parseTmWithFormat(const std::string& value, const char* fmt, std::tm& out){
    out = {};
    std::istringstream iss(value);
    iss >> std::get_time(&out, fmt);
    if(iss.fail()) return false;
    iss >> std::ws;
    return iss.eof();
  }

  static bool parseAbsoluteTime(const std::string& expr, long long& ts,
                                std::string* normalizedOut = nullptr){
    std::string normalized = trimCopy(expr);
    if(normalized.empty()) return false;
    for(char& ch : normalized){
      if(ch == 'T' || ch == '_') ch = ' ';
      if(ch == '-') ch = '.';
      if(ch == '/') ch = '.';
    }

    std::tm tm{};
    if(parseTmWithFormat(normalized, "%Y.%m.%d %H:%M:%S", tm) ||
       parseTmWithFormat(normalized, "%Y.%m.%d %H:%M", tm) ||
       parseTmWithFormat(normalized, "%Y.%m.%d", tm)){
      tm.tm_isdst = -1;
      std::time_t out = std::mktime(&tm);
      if(out == static_cast<std::time_t>(-1)) return false;
      ts = static_cast<long long>(out);
      if(normalizedOut){
        *normalizedOut = formatTime(ts);
      }
      return true;
    }
    return false;
  }

  static bool parseClockOnly(const std::string& expr, long long base, long long& ts,
                             std::string* normalizedOut = nullptr){
    std::string value = trimCopy(expr);
    if(value.empty()) return false;
    for(char& ch : value){
      if(ch == '_') ch = ':';
    }
    if(!std::isdigit(static_cast<unsigned char>(value.front()))) return false;
    for(char ch : value){
      if(ch != ':' && !std::isdigit(static_cast<unsigned char>(ch))){
        return false;
      }
    }
    int h = -1;
    int m = -1;
    int s = 0;
    char c1 = 0, c2 = 0;
    int matched = std::sscanf(value.c_str(), "%d%c%d%c%d", &h, &c1, &m, &c2, &s);
    bool valid = false;
    if(matched == 3 && c1 == ':'){
      s = 0;
      valid = true;
    }else if(matched == 5 && c1 == ':' && c2 == ':'){
      valid = true;
    }
    if(!valid) return false;
    if(h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) return false;

    std::time_t tt = static_cast<std::time_t>(base);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    tm = *std::localtime(&tt);
#endif
    tm.tm_hour = h;
    tm.tm_min = m;
    tm.tm_sec = s;
    tm.tm_isdst = -1;
    std::time_t out = std::mktime(&tm);
    if(out == static_cast<std::time_t>(-1)) return false;
    ts = static_cast<long long>(out);
    if(normalizedOut){
      *normalizedOut = formatTime(ts);
    }
    return true;
  }

  static bool parseCompactDateTime(const std::string& expr, long long& ts, std::string* normalizedOut = nullptr){
    std::string value = trimCopy(expr);
    if(value.empty()) return false;
    if(!std::all_of(value.begin(), value.end(), [](unsigned char ch){ return std::isdigit(ch) != 0; })){
      return false;
    }
    std::string normalized;
    if(value.size() == 8){
      normalized = value.substr(0, 4) + "." + value.substr(4, 2) + "." + value.substr(6, 2);
    }else if(value.size() == 12){
      normalized = value.substr(0, 4) + "." + value.substr(4, 2) + "." + value.substr(6, 2) + " " +
                   value.substr(8, 2) + ":" + value.substr(10, 2) + ":00";
    }else if(value.size() == 14){
      normalized = value.substr(0, 4) + "." + value.substr(4, 2) + "." + value.substr(6, 2) + " " +
                   value.substr(8, 2) + ":" + value.substr(10, 2) + ":" + value.substr(12, 2);
    }else{
      return false;
    }
    if(!parseAbsoluteTime(normalized, ts)) return false;
    if(normalizedOut){
      *normalizedOut = formatTime(ts);
    }
    return true;
  }

  static bool parseRelativeSeconds(const std::string& token, long long& secondsOut){
    std::string value = trimCopy(token);
    if(value.size() < 3 || value[0] != '+') return false;
    size_t index = 1;
    while(index < value.size() && std::isdigit(static_cast<unsigned char>(value[index]))){
      ++index;
    }
    if(index <= 1 || index >= value.size()) return false;
    long long number = 0;
    try{
      number = std::stoll(value.substr(1, index - 1));
    }catch(...){
      return false;
    }
    if(number < 0) return false;
    std::string unitToken = toLowerCopy(value.substr(index));
    long long scale = 0;
    if(unitToken == "s" || unitToken == "sec" || unitToken == "secs" ||
       unitToken == "second" || unitToken == "seconds"){
      scale = 1;
    }else if(unitToken == "m" || unitToken == "min" || unitToken == "mins" ||
             unitToken == "minute" || unitToken == "minutes"){
      scale = 60;
    }else if(unitToken == "h" || unitToken == "hr" || unitToken == "hrs" ||
             unitToken == "hour" || unitToken == "hours"){
      scale = 3600;
    }else if(unitToken == "d" || unitToken == "day" || unitToken == "days"){
      scale = 86400;
    }else if(unitToken == "w" || unitToken == "week" || unitToken == "weeks"){
      scale = 7 * 86400;
    }
    else return false;
    secondsOut = number * scale;
    return true;
  }

  static bool parseTimeExpr(const std::string& expr, long long base, long long& outTs,
                            std::string* normalizedOut = nullptr){
    std::string value = trimCopy(expr);
    if(value.empty()) return false;
    std::string lowered = toLowerCopy(value);
    if(lowered == "now" || lowered == "today"){
      outTs = base;
      if(normalizedOut) *normalizedOut = formatTime(outTs);
      return true;
    }
    if(lowered == "tomorrow"){
      outTs = base + 86400;
      if(normalizedOut) *normalizedOut = formatTime(outTs);
      return true;
    }
    long long delta = 0;
    if(parseRelativeSeconds(value, delta)){
      outTs = base + delta;
      if(normalizedOut) *normalizedOut = formatTime(outTs);
      return true;
    }
    std::string normalized;
    if(parseClockOnly(value, base, outTs, &normalized)){
      if(normalizedOut) *normalizedOut = normalized;
      return true;
    }
    if(parseCompactDateTime(value, outTs, &normalized)){
      if(normalizedOut) *normalizedOut = normalized;
      return true;
    }
    if(parseAbsoluteTime(value, outTs, &normalized)){
      if(normalizedOut) *normalizedOut = normalized;
      return true;
    }
    return false;
  }

  static bool parseRepeatExpr(const std::string& expr, long long& outSeconds, std::string& normalized){
    std::string value = toLowerCopy(trimCopy(expr));
    if(startsWith(value, "per ")){
      value = trimCopy(value.substr(4));
    }
    if(value.empty()) return false;

    size_t index = 0;
    while(index < value.size() && std::isdigit(static_cast<unsigned char>(value[index]))){
      ++index;
    }

    long long number = 1;
    if(index > 0){
      try{
        number = std::stoll(value.substr(0, index));
      }catch(...){
        return false;
      }
    }
    if(number <= 0) return false;
    if(index >= value.size()) return false;
    if(index + 1 != value.size()) return false;
    char unit = value[index];

    long long scale = 0;
    if(unit == 'd') scale = 86400;
    else if(unit == 'w') scale = 7 * 86400;
    else if(unit == 'm') scale = 30 * 86400;
    else if(unit == 'y') scale = 365 * 86400;
    else return false;

    outSeconds = number * scale;
    normalized = (number == 1) ? std::string(1, unit) : (std::to_string(number) + std::string(1, unit));
    return true;
  }

  static bool looksLikeDateToken(const std::string& token){
    int separators = 0;
    for(char ch : token){
      if(ch == '.' || ch == '-') separators++;
      else if(!std::isdigit(static_cast<unsigned char>(ch))) return false;
    }
    return separators == 2;
  }

  static bool looksLikeClockToken(const std::string& token){
    int separators = 0;
    for(char ch : token){
      if(ch == ':') separators++;
      else if(!std::isdigit(static_cast<unsigned char>(ch))) return false;
    }
    return separators >= 1;
  }

  static bool consumeTimeExpr(const std::vector<std::string>& tokens, size_t& index,
                              std::string& value, std::string& error){
    if(index + 1 >= tokens.size()){
      error = "missing value";
      return false;
    }
    value = tokens[index + 1];
    if(index + 2 < tokens.size() &&
       looksLikeDateToken(tokens[index + 1]) &&
       looksLikeClockToken(tokens[index + 2])){
      value += " " + tokens[index + 2];
      index += 2;
      return true;
    }
    index += 1;
    return true;
  }

  static bool consumeRepeatExpr(const std::vector<std::string>& tokens, size_t& index,
                                std::string& value, std::string& error){
    if(index + 1 >= tokens.size()){
      error = "missing value";
      return false;
    }
    value = tokens[index + 1];
    if(toLowerCopy(value) == "per"){
      if(index + 2 >= tokens.size()){
        error = "missing repeat unit after per";
        return false;
      }
      value += " " + tokens[index + 2];
      index += 2;
      return true;
    }
    index += 1;
    return true;
  }

  static TodoResolvedTiming resolveTiming(const TodoTask& task, long long now){
    TodoResolvedTiming timing;
    timing.startAt = task.startAt > 0 ? task.startAt : task.createdAt;
    timing.deadlineAt = task.deadlineAt;
    timing.hasDeadline = task.deadlineAt > 0;

    if(task.repeatSeconds <= 0) return timing;

    const long long step = task.repeatSeconds;
    int guard = 0;
    if(timing.hasDeadline){
      while(timing.deadlineAt < now && guard < 200000){
        timing.startAt += step;
        timing.deadlineAt += step;
        ++guard;
      }
    }else{
      while((timing.startAt + step) < now && guard < 200000){
        timing.startAt += step;
        ++guard;
      }
    }
    return timing;
  }

  static bool isFinishedTask(const TodoTask& task, long long now){
    if(task.repeatSeconds > 0) return false;
    if(task.deadlineAt <= 0) return false;
    return task.deadlineAt < now;
  }

  static std::string summarizeText(const std::string& text){
    std::string out = text;
    for(char& ch : out){
      if(ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
    }
    std::string trimmed = trimCopy(out);
    if(trimmed.size() > 120){
      trimmed = trimmed.substr(0, 117) + "...";
    }
    return trimmed;
  }

  static void appendDetail(TodoTask& task, long long ts, const std::string& text){
    TodoDetailEntry entry;
    entry.ts = ts;
    entry.text = text;
    task.details.push_back(std::move(entry));
    task.updatedAt = ts;
  }

  static std::vector<std::string> todoListAddedItems(const std::vector<std::string>& before,
                                                     const std::vector<std::string>& after){
    std::vector<std::string> remaining = before;
    std::vector<std::string> added;
    for(const auto& item : after){
      auto it = std::find(remaining.begin(), remaining.end(), item);
      if(it != remaining.end()){
        remaining.erase(it);
      }else{
        added.push_back(item);
      }
    }
    return added;
  }

  static std::vector<std::string> todoListRemovedItems(const std::vector<std::string>& before,
                                                       const std::vector<std::string>& after){
    std::vector<std::string> remaining = after;
    std::vector<std::string> removed;
    for(const auto& item : before){
      auto it = std::find(remaining.begin(), remaining.end(), item);
      if(it != remaining.end()){
        remaining.erase(it);
      }else{
        removed.push_back(item);
      }
    }
    return removed;
  }

  static bool applyEditorPayloadToTask(TodoTask& task, const TodoEditorPayload& payload){
    bool changed = false;
    if(task.startAt != payload.startAt){
      task.startAt = payload.startAt;
      appendDetail(task, nowSeconds(), "Reset StartTime -> " + formatTime(task.startAt));
      changed = true;
    }
    if(task.deadlineAt != payload.deadlineAt){
      task.deadlineAt = payload.deadlineAt;
      appendDetail(task, nowSeconds(), "Reset Deadline -> " + formatTime(task.deadlineAt));
      changed = true;
    }
    if(task.repeatSeconds != payload.repeatSeconds || task.repeatExpr != payload.repeatExpr){
      task.repeatSeconds = payload.repeatSeconds;
      task.repeatExpr = payload.repeatExpr;
      appendDetail(task, nowSeconds(), "Reset Repeat -> " +
                                     (task.repeatSeconds > 0 ? task.repeatExpr : std::string("none")));
      changed = true;
    }

    std::vector<std::string> nextTodo = normalizedTodoItems(payload.todoItems);
    if(task.todoItems != nextTodo){
      std::vector<std::string> added = todoListAddedItems(task.todoItems, nextTodo);
      std::vector<std::string> removed = todoListRemovedItems(task.todoItems, nextTodo);
      task.todoItems = std::move(nextTodo);
      for(const auto& item : added){
        appendDetail(task, nowSeconds(), "Add \"" + summarizeText(item) + "\"");
      }
      if(!removed.empty()){
        appendDetail(task, nowSeconds(), "Remove " + std::to_string(removed.size()) + " todo item(s)");
      }
      changed = true;
    }
    return changed;
  }

  static bool ensureTodoFolders(std::string& error){
    std::error_code ec;
    std::filesystem::create_directories(todoRoot(), ec);
    if(ec){
      error = "failed to create todo root folder";
      return false;
    }
    std::filesystem::create_directories(todoDetailsDir(), ec);
    if(ec){
      error = "failed to create Details folder";
      return false;
    }
    return true;
  }

  static std::string repeatExprFromSeconds(long long seconds){
    if(seconds <= 0) return std::string();
    const struct UnitScale { char unit; long long scale; } units[] = {
      {'y', 365LL * 86400LL},
      {'m', 30LL * 86400LL},
      {'w', 7LL * 86400LL},
      {'d', 86400LL}
    };
    for(const auto& unit : units){
      if(seconds % unit.scale == 0){
        long long count = seconds / unit.scale;
        if(count == 1) return std::string(1, unit.unit);
        return std::to_string(count) + std::string(1, unit.unit);
      }
    }
    return std::to_string(seconds) + "s";
  }

  static std::vector<std::string> normalizedTodoItems(const std::vector<std::string>& items){
    std::vector<std::string> out;
    out.reserve(items.size());
    for(const auto& item : items){
      std::string trimmed = trimCopy(item);
      if(trimmed.empty()) continue;
      out.push_back(trimmed);
    }
    return out;
  }

  static std::string buildTaskJsonText(const TodoTask& task,
                                       bool withTodoPlaceholder,
                                       size_t* cursorLineOut = nullptr,
                                       size_t* cursorColOut = nullptr){
    std::vector<std::string> todoItems = normalizedTodoItems(task.todoItems);
    if(withTodoPlaceholder){
      todoItems.push_back("");
    }

    const std::string deadlineText = task.deadlineAt > 0 ? formatTime(task.deadlineAt) : "none";
    const std::string repeatText = task.repeatSeconds > 0
                                     ? (task.repeatExpr.empty() ? repeatExprFromSeconds(task.repeatSeconds) : task.repeatExpr)
                                     : std::string("none");
    const std::string lastUpdateText = task.details.empty() ? std::string() : task.details.back().text;

    std::vector<std::string> lines;
    lines.reserve(32 + task.details.size() * 5 + todoItems.size());
    lines.push_back("{");
    lines.push_back("  \"name\": " + sj::dumpString(task.name) + ",");
    lines.push_back("  \"start_time\": " + sj::dumpString(formatTime(task.startAt)) + ",");
    lines.push_back("  \"deadline\": " + sj::dumpString(deadlineText) + ",");
    lines.push_back("  \"repeat\": " + sj::dumpString(repeatText) + ",");
    lines.push_back("  \"todo\": [");
    if(todoItems.empty()){
      lines.push_back("  ],");
    }else{
      for(size_t i = 0; i < todoItems.size(); ++i){
        std::string itemLine = "    " + sj::dumpString(todoItems[i]);
        if(i + 1 < todoItems.size()) itemLine += ",";
        lines.push_back(itemLine);
        if(withTodoPlaceholder && i + 1 == todoItems.size()){
          if(cursorLineOut) *cursorLineOut = lines.size();
          if(cursorColOut){
            size_t firstQuote = itemLine.find('"');
            *cursorColOut = (firstQuote == std::string::npos) ? 1 : (firstQuote + 2);
          }
        }
      }
      lines.push_back("  ],");
    }

    lines.push_back("  \"last_update\": " + sj::dumpString(lastUpdateText) + ",");
    lines.push_back("  \"created_at\": " + sj::dump(sj::Value(task.createdAt)) + ",");
    lines.push_back("  \"updated_at\": " + sj::dump(sj::Value(task.updatedAt)) + ",");
    lines.push_back("  \"start_at\": " + sj::dump(sj::Value(task.startAt)) + ",");
    lines.push_back("  \"deadline_at\": " + sj::dump(sj::Value(task.deadlineAt)) + ",");
    lines.push_back("  \"repeat_seconds\": " + sj::dump(sj::Value(task.repeatSeconds)) + ",");
    lines.push_back("  \"repeat_expr\": " + sj::dumpString(task.repeatExpr) + ",");
    lines.push_back("  \"details\": [");
    if(task.details.empty()){
      lines.push_back("  ],");
    }else{
      for(size_t i = 0; i < task.details.size(); ++i){
        const auto& detail = task.details[i];
        lines.push_back("    {");
        lines.push_back("      \"time\": " + sj::dumpString(formatTime(detail.ts)) + ",");
        lines.push_back("      \"text\": " + sj::dumpString(detail.text) + ",");
        lines.push_back("      \"ts\": " + sj::dump(sj::Value(detail.ts)));
        lines.push_back(i + 1 < task.details.size() ? "    }," : "    }");
      }
      lines.push_back("  ],");
    }
    lines.push_back("  \"version\": 1");
    lines.push_back("}");

    if(withTodoPlaceholder && cursorLineOut && *cursorLineOut == 0){
      // Fallback when todo section is empty and no placeholder line could be inferred.
      *cursorLineOut = 1;
      if(cursorColOut) *cursorColOut = 1;
    }

    std::ostringstream out;
    for(size_t i = 0; i < lines.size(); ++i){
      out << lines[i];
      if(i + 1 < lines.size()) out << "\n";
    }
    out << "\n";
    return out.str();
  }

  static bool writeTextFile(const std::filesystem::path& path,
                            const std::string& text,
                            std::string& error){
    std::ofstream out(path);
    if(!out.good()){
      error = "failed to write file: " + path.string();
      return false;
    }
    out << text;
    if(!out.good()){
      error = "failed to write file: " + path.string();
      return false;
    }
    return true;
  }

  static bool locateTodoCursor(const std::string& text, size_t& lineOut, size_t& colOut){
    std::vector<std::string> lines;
    std::istringstream iss(text);
    for(std::string line; std::getline(iss, line);){
      lines.push_back(line);
    }
    if(lines.empty()) return false;

    size_t todoStart = std::string::npos;
    for(size_t i = 0; i < lines.size(); ++i){
      if(lines[i].find("\"todo\"") != std::string::npos){
        todoStart = i;
        break;
      }
    }
    if(todoStart == std::string::npos) return false;

    for(size_t i = todoStart; i < lines.size(); ++i){
      const std::string& line = lines[i];
      if(line.find(']') != std::string::npos) break;
      size_t pos = line.find("\"\"");
      if(pos != std::string::npos){
        lineOut = i + 1;
        colOut = pos + 2;
        return true;
      }
    }

    for(size_t i = todoStart; i < lines.size(); ++i){
      if(lines[i].find(']') != std::string::npos){
        lineOut = i + 1;
        colOut = 1;
        return true;
      }
    }
    return false;
  }

  static bool writeDetailFile(const TodoTask& task, std::string& error){
    if(!ensureTodoFolders(error)) return false;
    const std::filesystem::path path = todoDetailFilePath(task.name);
    return writeTextFile(path, buildTaskJsonText(task, false), error);
  }

  static std::vector<TodoTask> loadTasksFromLegacyIndex(){
    std::vector<TodoTask> tasks;
    std::ifstream in(todoIndexPath());
    if(!in.good()) return tasks;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if(content.empty()) return tasks;
    try{
      sj::Value root = sj::parse(content);
      const sj::Value* arr = nullptr;
      if(root.isObject()){
        arr = root.find("tasks");
      }else if(root.isArray()){
        arr = &root;
      }
      if(!arr || !arr->isArray()) return tasks;
      for(const auto& item : arr->asArray()){
        if(!item.isObject()) continue;
        const auto& obj = item.asObject();
        TodoTask task;
        if(auto it = obj.find("name"); it != obj.end() && it->second.isString()){
          task.name = trimCopy(it->second.asString());
        }
        if(task.name.empty()) continue;
        if(auto it = obj.find("created_at"); it != obj.end() && it->second.isNumber()){
          task.createdAt = it->second.asInteger();
        }
        if(auto it = obj.find("updated_at"); it != obj.end() && it->second.isNumber()){
          task.updatedAt = it->second.asInteger();
        }
        if(auto it = obj.find("start_at"); it != obj.end() && it->second.isNumber()){
          task.startAt = it->second.asInteger();
        }
        if(auto it = obj.find("deadline_at"); it != obj.end() && it->second.isNumber()){
          task.deadlineAt = it->second.asInteger();
        }
        if(auto it = obj.find("repeat_seconds"); it != obj.end() && it->second.isNumber()){
          task.repeatSeconds = it->second.asInteger();
        }
        if(auto it = obj.find("repeat_expr"); it != obj.end() && it->second.isString()){
          task.repeatExpr = trimCopy(it->second.asString());
        }
        if(auto it = obj.find("todo"); it != obj.end()){
          if(it->second.isString()){
            std::string text = trimCopy(it->second.asString());
            if(!text.empty()) task.todoItems.push_back(text);
          }else if(it->second.isArray()){
            for(const auto& entry : it->second.asArray()){
              if(!entry.isString()) continue;
              std::string text = trimCopy(entry.asString());
              if(!text.empty()) task.todoItems.push_back(std::move(text));
            }
          }
        }
        if(auto it = obj.find("details"); it != obj.end() && it->second.isArray()){
          for(const auto& detailItem : it->second.asArray()){
            if(!detailItem.isObject()) continue;
            TodoDetailEntry entry;
            if(const sj::Value* v = detailItem.find("ts"); v && v->isNumber()){
              entry.ts = v->asInteger();
            }
            if(const sj::Value* v = detailItem.find("text"); v && v->isString()){
              entry.text = v->asString();
            }
            if(!entry.text.empty()){
              if(entry.ts <= 0) entry.ts = nowSeconds();
              task.details.push_back(std::move(entry));
            }
          }
        }
        if(task.createdAt <= 0) task.createdAt = nowSeconds();
        if(task.updatedAt <= 0) task.updatedAt = task.createdAt;
        if(task.startAt <= 0) task.startAt = task.createdAt;
        if(task.repeatSeconds > 0 && task.deadlineAt <= 0){
          task.deadlineAt = task.startAt + task.repeatSeconds;
        }
        if(task.repeatSeconds > 0 && task.repeatExpr.empty()){
          task.repeatExpr = repeatExprFromSeconds(task.repeatSeconds);
        }
        task.todoItems = normalizedTodoItems(task.todoItems);
        tasks.push_back(std::move(task));
      }
    }catch(...){
      tasks.clear();
    }
    std::sort(tasks.begin(), tasks.end(), [](const TodoTask& lhs, const TodoTask& rhs){
      return lhs.name < rhs.name;
    });
    return tasks;
  }

  static bool loadTaskFromDetailJson(const std::filesystem::path& path,
                                     TodoTask& taskOut,
                                     std::string& error){
    std::ifstream in(path);
    if(!in.good()){
      error = "failed to open " + path.string();
      return false;
    }
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if(raw.empty()){
      error = "empty file: " + path.string();
      return false;
    }

    sj::Value root;
    try{
      root = sj::parse(raw);
    }catch(const std::exception& ex){
      error = std::string("invalid JSON in ") + path.filename().string() + ": " + ex.what();
      return false;
    }
    if(!root.isObject()){
      error = "invalid JSON in " + path.filename().string() + ": root must be object";
      return false;
    }
    const sj::Object& obj = root.asObject();

    TodoTask task;
    task.name = path.stem().string();
    if(auto it = obj.find("name"); it != obj.end()){
      if(!it->second.isString()){
        error = "invalid `name` in " + path.filename().string();
        return false;
      }
      std::string jsonName = trimCopy(it->second.asString());
      if(!jsonName.empty()) task.name = jsonName;
    }
    if(task.name.empty() || !isValidName(task.name)){
      error = "invalid task name in " + path.filename().string();
      return false;
    }

    task.createdAt = nowSeconds();
    task.updatedAt = task.createdAt;
    task.startAt = task.createdAt;
    task.deadlineAt = 0;
    task.repeatSeconds = 0;
    task.repeatExpr.clear();

    if(auto it = obj.find("created_at"); it != obj.end()){
      if(!it->second.isNumber()){
        error = "invalid `created_at` in " + path.filename().string();
        return false;
      }
      task.createdAt = it->second.asInteger();
    }
    if(auto it = obj.find("updated_at"); it != obj.end()){
      if(!it->second.isNumber()){
        error = "invalid `updated_at` in " + path.filename().string();
        return false;
      }
      task.updatedAt = it->second.asInteger();
    }
    if(auto it = obj.find("start_at"); it != obj.end()){
      if(!it->second.isNumber()){
        error = "invalid `start_at` in " + path.filename().string();
        return false;
      }
      task.startAt = it->second.asInteger();
    }
    if(auto it = obj.find("deadline_at"); it != obj.end()){
      if(!it->second.isNumber()){
        error = "invalid `deadline_at` in " + path.filename().string();
        return false;
      }
      task.deadlineAt = it->second.asInteger();
    }
    if(auto it = obj.find("repeat_seconds"); it != obj.end()){
      if(!it->second.isNumber()){
        error = "invalid `repeat_seconds` in " + path.filename().string();
        return false;
      }
      task.repeatSeconds = it->second.asInteger();
      if(task.repeatSeconds < 0){
        error = "invalid `repeat_seconds` in " + path.filename().string();
        return false;
      }
    }
    if(auto it = obj.find("repeat_expr"); it != obj.end()){
      if(!it->second.isString()){
        error = "invalid `repeat_expr` in " + path.filename().string();
        return false;
      }
      task.repeatExpr = trimCopy(it->second.asString());
    }
    if(auto it = obj.find("todo"); it != obj.end()){
      if(it->second.isString()){
        std::string text = trimCopy(it->second.asString());
        if(!text.empty()) task.todoItems.push_back(text);
      }else if(it->second.isArray()){
        for(const auto& item : it->second.asArray()){
          if(!item.isString()){
            error = "invalid `todo` item in " + path.filename().string();
            return false;
          }
          std::string text = trimCopy(item.asString());
          if(!text.empty()) task.todoItems.push_back(std::move(text));
        }
      }else{
        error = "invalid `todo` in " + path.filename().string() + ": must be string or string array";
        return false;
      }
    }
    if(auto it = obj.find("details"); it != obj.end()){
      if(!it->second.isArray()){
        error = "invalid `details` in " + path.filename().string();
        return false;
      }
      for(const auto& item : it->second.asArray()){
        if(!item.isObject()){
          error = "invalid detail entry in " + path.filename().string();
          return false;
        }
        const sj::Object& detailObj = item.asObject();
        TodoDetailEntry entry;
        if(auto v = detailObj.find("text"); v != detailObj.end()){
          if(!v->second.isString()){
            error = "invalid detail text in " + path.filename().string();
            return false;
          }
          entry.text = v->second.asString();
        }
        if(entry.text.empty()) continue;
        if(auto v = detailObj.find("time"); v != detailObj.end()){
          if(!v->second.isString()){
            error = "invalid detail time in " + path.filename().string();
            return false;
          }
          if(!parseTimeExpr(v->second.asString(), nowSeconds(), entry.ts)){
            error = "invalid detail time in " + path.filename().string();
            return false;
          }
        }
        if(entry.ts <= 0){
          if(auto v = detailObj.find("ts"); v != detailObj.end()){
            if(!v->second.isNumber()){
              error = "invalid detail ts in " + path.filename().string();
              return false;
            }
            entry.ts = v->second.asInteger();
          }
        }
        if(entry.ts <= 0) entry.ts = nowSeconds();
        task.details.push_back(std::move(entry));
      }
    }

    // Human-readable fields have higher priority when present.
    std::string payloadError;
    std::optional<TodoEditorPayload> payload = parseEditorPayload(raw, task, payloadError);
    if(!payload){
      error = "invalid editor fields in " + path.filename().string() + ": " + payloadError;
      return false;
    }
    task.startAt = payload->startAt;
    task.deadlineAt = payload->deadlineAt;
    task.repeatSeconds = payload->repeatSeconds;
    task.repeatExpr = payload->repeatExpr;
    task.todoItems = normalizedTodoItems(payload->todoItems);
    if(task.repeatSeconds > 0 && task.repeatExpr.empty()){
      task.repeatExpr = repeatExprFromSeconds(task.repeatSeconds);
    }
    if(task.deadlineAt > 0 && task.startAt > task.deadlineAt){
      error = "start time is after deadline in " + path.filename().string();
      return false;
    }
    if(task.createdAt <= 0) task.createdAt = nowSeconds();
    if(task.updatedAt <= 0) task.updatedAt = task.createdAt;
    if(task.startAt <= 0) task.startAt = task.createdAt;

    taskOut = std::move(task);
    return true;
  }

  static std::vector<TodoTask> loadTasks(){
    std::vector<TodoTask> tasks;
    std::string ensureError;
    if(!ensureTodoFolders(ensureError)) return tasks;

    std::error_code ec;
    std::filesystem::directory_iterator it(todoDetailsDir(), ec);
    if(!ec){
      for(const auto& entry : it){
        if(!entry.is_regular_file()) continue;
        std::string ext = toLowerCopy(entry.path().extension().string());
        if(ext != ".json") continue;
        TodoTask task;
        std::string loadError;
        if(!loadTaskFromDetailJson(entry.path(), task, loadError)){
          continue;
        }
        if(task.name.empty() || !isValidName(task.name)) continue;
        auto dup = std::find_if(tasks.begin(), tasks.end(), [&](const TodoTask& t){ return t.name == task.name; });
        if(dup == tasks.end()){
          tasks.push_back(std::move(task));
        }else if(task.updatedAt >= dup->updatedAt){
          *dup = std::move(task);
        }
      }
    }

    if(tasks.empty()){
      tasks = loadTasksFromLegacyIndex();
      if(!tasks.empty()){
        std::string migrateError;
        (void)syncDetailFiles(tasks, migrateError);
      }
    }

    std::sort(tasks.begin(), tasks.end(), [](const TodoTask& lhs, const TodoTask& rhs){
      return lhs.name < rhs.name;
    });
    return tasks;
  }

  static bool syncDetailFiles(const std::vector<TodoTask>& tasks, std::string& error){
    if(!ensureTodoFolders(error)) return false;
    std::set<std::string> activeNames;
    for(const auto& task : tasks){
      activeNames.insert(task.name);
      if(!writeDetailFile(task, error)) return false;
    }
    std::error_code ec;
    std::filesystem::directory_iterator it(todoDetailsDir(), ec);
    if(ec) return true;
    for(const auto& entry : it){
      if(!entry.is_regular_file()) continue;
      std::string filename = entry.path().filename().string();
      std::string ext = toLowerCopy(entry.path().extension().string());
      if(ext != ".json" && ext != ".tdle") continue;
      std::string stem = entry.path().stem().string();
      if(ext == ".json" && activeNames.count(stem)) continue;
      std::filesystem::remove(entry.path(), ec);
      ec.clear();
    }
    return true;
  }

  static bool persistTasks(const std::vector<TodoTask>& tasks, std::string& error){
    if(!syncDetailFiles(tasks, error)) return false;
    std::error_code ec;
    std::filesystem::remove(todoIndexPath(), ec);
    ec.clear();
    std::filesystem::remove(todoNamePath(), ec);
    ec.clear();
    std::filesystem::remove_all(todoRoot() / ".drafts", ec);
    ec.clear();
    return true;
  }

  static void appendOperation(const std::string& op){
    std::string ensureError;
    if(!ensureTodoFolders(ensureError)) return;
    std::ofstream out(todoOperationPath(), std::ios::app);
    if(!out.good()) return;
    out << formatTime(nowSeconds()) << " " << op << "\n";
  }

  static TodoTask* findTask(std::vector<TodoTask>& tasks, const std::string& name){
    for(auto& task : tasks){
      if(task.name == name) return &task;
    }
    return nullptr;
  }

  static const TodoTask* findTaskConst(const std::vector<TodoTask>& tasks, const std::string& name){
    for(const auto& task : tasks){
      if(task.name == name) return &task;
    }
    return nullptr;
  }

  static bool confirmAction(const std::string& prompt, bool force, bool allowPrompt){
    if(force) return true;
    if(!allowPrompt) return false;
    static const std::vector<std::string> yesNoSuggestions = {"y", "yes", "n", "no", "是", "否"};
    std::string line;
    std::string fullPrompt = prompt + localeText(" [y/N]: ", " [y/N]: ");
    if(!readInteractiveLine(fullPrompt, line, &yesNoSuggestions, nullptr, nullptr, 8)){
      return false;
    }
    bool yes = false;
    if(!parseYesNo(line, yes)) return false;
    return yes;
  }

  static std::optional<TodoEditorPayload> parseEditorPayload(const std::string& raw,
                                                             const TodoTask& task,
                                                             std::string& error){
    sj::Value root;
    try{
      root = sj::parse(raw);
    }catch(const std::exception& ex){
      error = std::string("invalid JSON: ") + ex.what();
      return std::nullopt;
    }
    if(!root.isObject()){
      error = "invalid JSON: root must be an object";
      return std::nullopt;
    }

    const sj::Object& obj = root.asObject();
    auto readString = [&](const std::string& key, std::string& target)->bool{
      auto it = obj.find(key);
      if(it == obj.end()) return true;
      if(!it->second.isString()){
        error = "invalid JSON: `" + key + "` must be a string";
        return false;
      }
      target = trimCopy(it->second.asString());
      return true;
    };

    std::string startExpr = formatTime(task.startAt);
    std::string deadlineExpr = task.deadlineAt > 0 ? formatTime(task.deadlineAt) : "none";
    std::string repeatExpr = (task.repeatSeconds > 0)
                               ? (task.repeatExpr.empty() ? "d" : task.repeatExpr)
                               : "none";
    std::vector<std::string> todoItems = normalizedTodoItems(task.todoItems);
    std::string note;

    if(!readString("start_time", startExpr)) return std::nullopt;
    if(!readString("start", startExpr)) return std::nullopt;
    if(!readString("deadline", deadlineExpr)) return std::nullopt;
    if(!readString("repeat", repeatExpr)) return std::nullopt;
    if(!readString("note", note)) return std::nullopt;

    if(auto it = obj.find("todo"); it != obj.end()){
      todoItems.clear();
      if(it->second.isString()){
        std::string text = trimCopy(it->second.asString());
        if(!text.empty()) todoItems.push_back(text);
      }else if(it->second.isArray()){
        for(const auto& item : it->second.asArray()){
          if(!item.isString()){
            error = "invalid JSON: `todo` must be string array";
            return std::nullopt;
          }
          std::string text = trimCopy(item.asString());
          if(!text.empty()) todoItems.push_back(std::move(text));
        }
      }else{
        error = "invalid JSON: `todo` must be string or string array";
        return std::nullopt;
      }
    }
    note = trimCopy(note);
    if(!note.empty()){
      todoItems.push_back(note);
    }

    TodoEditorPayload payload;
    const long long now = nowSeconds();

    if(startExpr.empty()){
      error = "invalid JSON: `start_time` cannot be empty";
      return std::nullopt;
    }
    if(!parseTimeExpr(startExpr, now, payload.startAt)){
      error = "invalid start_time: " + startExpr;
      return std::nullopt;
    }

    std::string deadlineLower = toLowerCopy(trimCopy(deadlineExpr));
    if(deadlineLower == "none" || deadlineLower.empty()){
      payload.deadlineAt = 0;
    }else{
      if(!parseTimeExpr(deadlineExpr, now, payload.deadlineAt)){
        error = "invalid deadline: " + deadlineExpr;
        return std::nullopt;
      }
    }

    std::string repeatLower = toLowerCopy(trimCopy(repeatExpr));
    if(repeatLower == "none" || repeatLower.empty()){
      payload.repeatSeconds = 0;
      payload.repeatExpr.clear();
    }else{
      if(!parseRepeatExpr(repeatExpr, payload.repeatSeconds, payload.repeatExpr)){
        error = "invalid repeat: " + repeatExpr;
        return std::nullopt;
      }
    }

    if(payload.repeatSeconds > 0 && payload.deadlineAt <= 0){
      payload.deadlineAt = payload.startAt + payload.repeatSeconds;
    }
    if(payload.deadlineAt > 0 && payload.startAt > payload.deadlineAt){
      error = "start time is after deadline";
      return std::nullopt;
    }

    payload.todoItems = normalizedTodoItems(todoItems);
    return payload;
  }

  static std::optional<TodoEditorPayload> openEditorForTask(const ToolExecutionRequest& request,
                                                            const TodoTask& task,
                                                            std::string& error){
    if(request.silent || request.forLLM){
      error = "editor mode is unavailable in silent/LLM invocation";
      return std::nullopt;
    }
    std::string ensureError;
    if(!ensureTodoFolders(ensureError)){
      error = ensureError;
      return std::nullopt;
    }
    const std::filesystem::path detailPath = todoDetailFilePath(task.name);
    size_t cursorLine = 1;
    size_t cursorCol = 1;
    std::string editorText = buildTaskJsonText(task, true, &cursorLine, &cursorCol);
    if(!writeTextFile(detailPath, editorText, error)){
      return std::nullopt;
    }

    const std::vector<std::string> yesNoSuggestions = {"y", "n"};
    while(true){
      std::string gotoArg = detailPath.string() + ":" + std::to_string(cursorLine) + ":" + std::to_string(cursorCol);
      std::string command = "code --wait -g " + shellEscape(gotoArg);
      auto execResult = detail::execute_shell(request, command);
      if(execResult.exitCode != 0){
        error = "failed to run `code --wait` for " + detailPath.string();
        return std::nullopt;
      }

      std::ifstream in(detailPath);
      if(!in.good()){
        error = "failed to read edited file: " + detailPath.string();
        return std::nullopt;
      }
      std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

      std::string parseError;
      try{
        sj::Value root = sj::parse(content);
        if(!root.isObject()){
          parseError = "root must be object";
        }else{
          if(const sj::Value* v = root.find("name"); v){
            if(!v->isString()){
              parseError = "`name` must be string";
            }else{
              std::string editedName = trimCopy(v->asString());
              if(!editedName.empty() && editedName != task.name){
                parseError = "`name` cannot be changed in edit mode";
              }
            }
          }
        }
      }catch(const std::exception& ex){
        parseError = std::string("invalid JSON: ") + ex.what();
      }
      if(parseError.empty()){
        std::optional<TodoEditorPayload> payload = parseEditorPayload(content, task, parseError);
        if(payload){
          TodoTask sanitized = task;
          sanitized.startAt = payload->startAt;
          sanitized.deadlineAt = payload->deadlineAt;
          sanitized.repeatSeconds = payload->repeatSeconds;
          sanitized.repeatExpr = payload->repeatExpr;
          sanitized.todoItems = normalizedTodoItems(payload->todoItems);
          std::string sanitizeError;
          (void)writeDetailFile(sanitized, sanitizeError);
          return payload;
        }
      }

      std::cout << localeText("  invalid todo file: ", "  文件格式无效：")
                << parseError << "\n";
      std::string answer;
      if(!readInteractiveLine(localeText("reopen editor? [Y/n]: ",
                                         "重新打开编辑器？ [Y/n]: "),
                              answer, &yesNoSuggestions, nullptr, nullptr, 8)){
        error = "input aborted";
        return std::nullopt;
      }
      std::string lowered = toLowerCopy(trimCopy(answer));
      if(lowered.empty() || lowered == "y"){
        if(!locateTodoCursor(content, cursorLine, cursorCol)){
          cursorLine = 1;
          cursorCol = 1;
        }
        continue;
      }
      if(lowered == "n"){
        error = "edit cancelled due to invalid format";
        return std::nullopt;
      }
      std::cout << localeText("  invalid answer (use y/n)\n", "  无效输入（请输入 y/n）\n");
    }
  }

  static std::string formatDuration(long long seconds){
    bool negative = seconds < 0;
    long long value = negative ? -seconds : seconds;
    long long days = value / 86400;
    value %= 86400;
    long long hours = value / 3600;
    value %= 3600;
    long long minutes = value / 60;
    long long secs = value % 60;
    std::ostringstream oss;
    if(negative) oss << "-";
    if(days > 0) oss << days << "d";
    if(hours > 0) oss << hours << "h";
    if(minutes > 0) oss << minutes << "m";
    if(days == 0 && hours == 0 && minutes == 0) oss << secs << "s";
    return oss.str();
  }

  static bool isCancelToken(const std::string& value){
    std::string lowered = toLowerCopy(trimCopy(value));
    return lowered == "q" || lowered == "quit" || lowered == "cancel";
  }

  static bool ciStartsWith(const std::string& text, const std::string& prefix){
    if(prefix.size() > text.size()) return false;
    for(size_t i = 0; i < prefix.size(); ++i){
      char a = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
      char b = static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[i])));
      if(a != b) return false;
    }
    return true;
  }

  static void printSuggestionsLine(const std::vector<std::string>& suggestions){
    if(suggestions.empty()) return;
    std::cout << "  " << localeText("suggestions", "候选") << ": ";
    for(size_t i = 0; i < suggestions.size(); ++i){
      if(i) std::cout << ", ";
      std::cout << suggestions[i];
    }
    std::cout << "\n";
  }

  struct PatternFieldConstraint {
    size_t start = 0;
    size_t width = 0;
    int min = 0;
    int max = 0;
  };

  static bool digitPrefixCanReachRange(const std::string& prefix, size_t width, int min, int max){
    if(prefix.size() > width) return false;
    if(prefix.empty()) return true;
    if(!std::all_of(prefix.begin(), prefix.end(), [](unsigned char ch){ return std::isdigit(ch) != 0; })){
      return false;
    }
    if(width == 4 && min == 0 && max == 9999){
      return true;
    }
    for(int value = min; value <= max; ++value){
      std::ostringstream oss;
      if(width > 1){
        oss << std::setw(static_cast<int>(width)) << std::setfill('0') << value;
      }else{
        oss << value;
      }
      const std::string candidate = oss.str();
      if(candidate.size() != width) continue;
      if(startsWith(candidate, prefix)){
        return true;
      }
    }
    return false;
  }

  static bool canCompleteStructuredPattern(const std::string& value,
                                           const std::string& pattern,
                                           const std::vector<PatternFieldConstraint>& fields){
    if(value.size() > pattern.size()) return false;
    for(size_t i = 0; i < value.size(); ++i){
      char p = pattern[i];
      char c = value[i];
      if(p == 'D'){
        if(!std::isdigit(static_cast<unsigned char>(c))) return false;
      }else{
        if(c != p) return false;
      }
    }
    for(const auto& field : fields){
      if(value.size() <= field.start) continue;
      size_t take = std::min(field.width, value.size() - field.start);
      std::string prefix = value.substr(field.start, take);
      if(!digitPrefixCanReachRange(prefix, field.width, field.min, field.max)){
        return false;
      }
    }
    return true;
  }

  static bool isPotentialTimePrefix(const std::string& raw){
    const std::string value = trimCopy(raw);
    if(value.empty() || value == "?"){
      return true;
    }
    if(isCancelToken(value)){
      return true;
    }

    long long parsed = 0;
    if(parseTimeExpr(value, nowSeconds(), parsed)){
      return true;
    }

    const std::string lowered = toLowerCopy(value);
    const std::vector<std::string> keywords = {"now", "today", "tomorrow"};
    for(const auto& keyword : keywords){
      if(ciStartsWith(keyword, lowered)){
        return true;
      }
    }

    if(value[0] == '+'){
      if(value.size() == 1) return true;
      size_t i = 1;
      while(i < value.size() && std::isdigit(static_cast<unsigned char>(value[i]))){
        ++i;
      }
      if(i == 1) return false;
      if(i == value.size()) return true; // waiting for unit
      std::string unit = toLowerCopy(value.substr(i));
      const std::vector<std::string> units = {
        "s", "sec", "secs", "second", "seconds",
        "m", "min", "mins", "minute", "minutes",
        "h", "hr", "hrs", "hour", "hours",
        "d", "day", "days",
        "w", "week", "weeks"
      };
      for(const auto& candidate : units){
        if(ciStartsWith(candidate, unit)){
          return true;
        }
      }
      return false;
    }

    std::string clock = value;
    for(char& ch : clock){
      if(ch == '_') ch = ':';
    }
    const std::vector<std::pair<std::string, std::vector<PatternFieldConstraint>>> clockPatterns = {
      {"D:DD",     {{0, 1, 0, 9},  {2, 2, 0, 59}}},
      {"DD:DD",    {{0, 2, 0, 23}, {3, 2, 0, 59}}},
      {"D:DD:DD",  {{0, 1, 0, 9},  {2, 2, 0, 59}, {5, 2, 0, 59}}},
      {"DD:DD:DD", {{0, 2, 0, 23}, {3, 2, 0, 59}, {6, 2, 0, 59}}}
    };
    for(const auto& item : clockPatterns){
      if(canCompleteStructuredPattern(clock, item.first, item.second)){
        return true;
      }
    }

    std::string normalized = value;
    for(char& ch : normalized){
      if(ch == '-') ch = '.';
      else if(ch == '/') ch = '.';
      else if(ch == 'T' || ch == '_') ch = ' ';
    }
    const std::vector<std::pair<std::string, std::vector<PatternFieldConstraint>>> datePatterns = {
      {"DDDD.DD.DD",             {{0, 4, 0, 9999}, {5, 2, 1, 12}, {8, 2, 1, 31}}},
      {"DDDD.DD.DD DD:DD",       {{0, 4, 0, 9999}, {5, 2, 1, 12}, {8, 2, 1, 31}, {11, 2, 0, 23}, {14, 2, 0, 59}}},
      {"DDDD.DD.DD DD:DD:DD",    {{0, 4, 0, 9999}, {5, 2, 1, 12}, {8, 2, 1, 31}, {11, 2, 0, 23}, {14, 2, 0, 59}, {17, 2, 0, 59}}},
      {"DDDDDDDD",               {{0, 4, 0, 9999}, {4, 2, 1, 12}, {6, 2, 1, 31}}},
      {"DDDDDDDDDDDD",           {{0, 4, 0, 9999}, {4, 2, 1, 12}, {6, 2, 1, 31}, {8, 2, 0, 23}, {10, 2, 0, 59}}},
      {"DDDDDDDDDDDDDD",         {{0, 4, 0, 9999}, {4, 2, 1, 12}, {6, 2, 1, 31}, {8, 2, 0, 23}, {10, 2, 0, 59}, {12, 2, 0, 59}}}
    };
    for(const auto& item : datePatterns){
      if(canCompleteStructuredPattern(normalized, item.first, item.second)){
        return true;
      }
    }

    return false;
  }

  static bool isPotentialDeadlinePrefix(const std::string& raw){
    std::string value = trimCopy(raw);
    if(value.empty() || value == "?"){
      return true;
    }
    if(isCancelToken(value)){
      return true;
    }
    std::string lowered = toLowerCopy(value);
    if(ciStartsWith("none", lowered)){
      return true;
    }
    return isPotentialTimePrefix(value);
  }

  static bool isPotentialRepeatCorePrefix(const std::string& raw){
    std::string value = toLowerCopy(trimCopy(raw));
    if(value.empty()) return true;

    size_t idx = 0;
    while(idx < value.size() && std::isdigit(static_cast<unsigned char>(value[idx]))){
      ++idx;
    }

    if(idx == value.size()){
      if(idx == 0) return false;
      try{
        long long n = std::stoll(value.substr(0, idx));
        return n > 0;
      }catch(...){
        return false;
      }
    }

    if(idx > 0){
      long long n = 0;
      try{
        n = std::stoll(value.substr(0, idx));
      }catch(...){
        return false;
      }
      if(n <= 0) return false;
    }

    if(idx + 1 != value.size()){
      return false;
    }
    char unit = value[idx];
    return unit == 'd' || unit == 'w' || unit == 'm' || unit == 'y';
  }

  static bool isPotentialRepeatPrefix(const std::string& raw){
    std::string value = trimCopy(raw);
    if(value.empty() || value == "?"){
      return true;
    }
    if(isCancelToken(value)){
      return true;
    }
    std::string lowered = toLowerCopy(value);
    if(ciStartsWith("none", lowered)){
      return true;
    }

    long long seconds = 0;
    std::string normalized;
    if(parseRepeatExpr(value, seconds, normalized)){
      return true;
    }

    if(ciStartsWith("per", lowered)){
      if(lowered.size() < 3) return true;
      if(lowered == "per") return true;
      if(lowered.size() >= 4 && lowered.substr(0, 4) == "per "){
        std::string tail = trimCopy(lowered.substr(4));
        return isPotentialRepeatCorePrefix(tail);
      }
      return false;
    }

    return isPotentialRepeatCorePrefix(lowered);
  }

  static bool readInteractiveLine(const std::string& prompt,
                                  std::string& out,
                                  const std::vector<std::string>* suggestions = nullptr,
                                  const std::function<Candidates(const std::string&)>& completionProvider = nullptr,
                                  const std::function<bool(const std::string&)>& prefixValidator = nullptr,
                                  size_t maxLength = 256){
    detail::InteractiveLineOptions options;
    options.prompt = prompt;
    options.suggestions = suggestions;
    options.completionProvider = completionProvider;
    options.prefixValidator = prefixValidator;
    options.invalidSuffix = localeText(" invalid format", " 格式错误");
    options.maxLengthSuffix = localeText(" length limit", " 长度已达上限");
    options.maxLength = maxLength;
    options.trimOutput = true;
    options.suggestionRows = 3;
    auto status = detail::read_interactive_line(options, out);
    return status == detail::InteractiveLineStatus::Ok;
  }

  static std::function<Candidates(const std::string&)> completionProviderForCreateValue(const std::string& optionName){
    const std::string prefix = "todo create __interactive__ " + optionName + " ";
    return [prefix](const std::string& currentValue){
      std::string full = prefix + currentValue;
      auto tokens = splitTokens(full);
      return Todo::complete(full, tokens);
    };
  }

  static bool parseYesNo(const std::string& value, bool& out){
    std::string trimmed = trimCopy(value);
    std::string lowered = toLowerCopy(trimmed);
    if(lowered == "y" || lowered == "yes" || lowered == "true" || lowered == "1"){
      out = true;
      return true;
    }
    if(lowered == "n" || lowered == "no" || lowered == "false" || lowered == "0"){
      out = false;
      return true;
    }
    if(trimmed == "是" || trimmed == "好"){
      out = true;
      return true;
    }
    if(trimmed == "否" || trimmed == "不"){
      out = false;
      return true;
    }
    return false;
  }

  static bool collectCreateInteractive(const ToolExecutionRequest& request,
                                       CreateInteractiveInput& input,
                                       std::string& error){
    if(request.silent || request.forLLM){
      error = localeText("interactive create is unavailable in silent/LLM invocation",
                         "silent/LLM 调用中不能使用交互式 create");
      return false;
    }

    auto tasks = loadTasks();
    std::set<std::string> existingNames;
    for(const auto& task : tasks){
      existingNames.insert(task.name);
    }

    std::cout << localeText("todo create interactive mode\n", "todo create 交互模式\n");
    std::cout << localeText("  - enter `q` to cancel\n", "  - 输入 `q` 取消\n");
    std::cout << localeText("  - enter `?` to show suggestions for current field\n",
                            "  - 输入 `?` 查看当前字段候选\n");

    while(true){
      std::string value;
      if(!readInteractiveLine(localeText("name: ", "名称: "), value, nullptr, nullptr, nullptr, 64)){
        error = localeText("input aborted", "输入中断");
        return false;
      }
      if(isCancelToken(value)){
        input.cancelled = true;
        return true;
      }
      if(value.empty()){
        std::cout << localeText("  name is required\n", "  名称不能为空\n");
        continue;
      }
      if(!isValidName(value)){
        std::cout << localeText("  invalid name: use only letters, digits, underscores\n",
                                "  名称格式错误：只能包含字母、数字、下划线\n");
        continue;
      }
      if(existingNames.count(value)){
        std::cout << localeText("  task already exists: ", "  任务已存在：") << value << "\n";
        continue;
      }
      input.name = value;
      break;
    }

    const std::vector<std::string> timeSuggestions = makeTimeSuggestions();
    while(true){
      std::string value;
      if(!readInteractiveLine(localeText("start time [now]: ", "开始时间 [now]: "),
                              value, nullptr, completionProviderForCreateValue("--start"),
                              isPotentialTimePrefix, 64)){
        error = localeText("input aborted", "输入中断");
        return false;
      }
      if(isCancelToken(value)){
        input.cancelled = true;
        return true;
      }
      if(value == "?"){
        printSuggestionsLine(timeSuggestions);
        continue;
      }
      if(value.empty()){
        value = "now";
      }
      long long parsed = 0;
      std::string normalized;
      if(!parseTimeExpr(value, nowSeconds(), parsed, &normalized)){
        std::cout << localeText("  invalid time: ", "  时间无效：") << value << "\n";
        continue;
      }
      std::string canonical = normalized.empty() ? formatTime(parsed) : normalized;
      if(canonical != value){
        std::cout << localeText("  normalized -> ", "  标准化 -> ") << canonical << "\n";
      }
      input.startExpr = canonical;
      break;
    }

    std::vector<std::string> deadlineSuggestions = timeSuggestions;
    deadlineSuggestions.push_back("none");
    while(true){
      std::string value;
      if(!readInteractiveLine(localeText("deadline [none]: ", "截止时间 [none]: "),
                              value, nullptr, completionProviderForCreateValue("--deadline"),
                              isPotentialDeadlinePrefix, 64)){
        error = localeText("input aborted", "输入中断");
        return false;
      }
      if(isCancelToken(value)){
        input.cancelled = true;
        return true;
      }
      if(value == "?"){
        printSuggestionsLine(deadlineSuggestions);
        continue;
      }
      if(value.empty()){
        input.deadlineExpr.reset();
        break;
      }
      if(toLowerCopy(value) == "none"){
        input.deadlineExpr.reset();
        break;
      }
      long long parsed = 0;
      std::string normalized;
      if(!parseTimeExpr(value, nowSeconds(), parsed, &normalized)){
        std::cout << localeText("  invalid deadline: ", "  截止时间无效：") << value << "\n";
        continue;
      }
      std::string canonical = normalized.empty() ? formatTime(parsed) : normalized;
      if(canonical != value){
        std::cout << localeText("  normalized -> ", "  标准化 -> ") << canonical << "\n";
      }
      input.deadlineExpr = canonical;
      break;
    }

    const std::vector<std::string> repeatSuggestions = {
      "none", "d", "w", "m", "y",
      "2d", "2w", "2m", "2y",
      "3d", "7d", "14d",
      "per d", "per w", "per m", "per y",
      "per 2d", "per 2w", "per 2m", "per 2y"
    };
    while(true){
      std::string value;
      if(!readInteractiveLine(localeText("repeat [none]: ", "重复 [none]: "),
                              value, nullptr, completionProviderForCreateValue("--repeat"),
                              isPotentialRepeatPrefix, 24)){
        error = localeText("input aborted", "输入中断");
        return false;
      }
      if(isCancelToken(value)){
        input.cancelled = true;
        return true;
      }
      if(value == "?"){
        printSuggestionsLine(repeatSuggestions);
        continue;
      }
      if(value.empty()){
        input.repeatExpr.reset();
        break;
      }
      if(toLowerCopy(value) == "none"){
        input.repeatExpr.reset();
        break;
      }
      long long repeatSeconds = 0;
      std::string normalized;
      if(!parseRepeatExpr(value, repeatSeconds, normalized)){
        std::cout << localeText("  invalid repeat: ", "  重复周期无效：") << value << "\n";
        continue;
      }
      input.repeatExpr = normalized;
      break;
    }

    const std::vector<std::string> editorSuggestions = {"y", "n"};
    while(true){
      std::string value;
      if(!readInteractiveLine(localeText("open editor after create? [Y/n]: ",
                                         "创建后打开编辑器？ [Y/n]: "),
                              value, &editorSuggestions, nullptr, nullptr, 8)){
        error = localeText("input aborted", "输入中断");
        return false;
      }
      if(isCancelToken(value)){
        input.cancelled = true;
        return true;
      }
      if(value == "?"){
        printSuggestionsLine(editorSuggestions);
        continue;
      }
      if(value.empty()){
        input.editAfterCreate = true;
        break;
      }
      std::string lowered = toLowerCopy(trimCopy(value));
      if(lowered != "y" && lowered != "n"){
        std::cout << localeText("  invalid answer: ", "  无效输入：") << value
                  << localeText(" (use y/n)\n", "（请输入 y/n）\n");
        continue;
      }
      input.editAfterCreate = (lowered == "y");
      break;
    }

    std::cout << localeText("  create config ready\n", "  创建参数已确认\n");
    return true;
  }

  static ToolExecutionResult handleCreate(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    std::string name;
    std::optional<std::string> startExpr;
    std::optional<std::string> deadlineExpr;
    std::optional<std::string> repeatExpr;
    bool editAfterCreate = !(request.silent || request.forLLM);

    if(args.size() == 2){
      CreateInteractiveInput interactiveInput;
      std::string interactiveError;
      if(!collectCreateInteractive(request, interactiveInput, interactiveError)){
        return detail::text_result(localeText("todo create: ", "todo create：") + interactiveError + "\n", 1);
      }
      if(interactiveInput.cancelled){
        return detail::text_result(localeText("todo create cancelled\n", "todo create 已取消\n"), 1);
      }
      name = interactiveInput.name;
      startExpr = interactiveInput.startExpr;
      deadlineExpr = interactiveInput.deadlineExpr;
      repeatExpr = interactiveInput.repeatExpr;
      editAfterCreate = interactiveInput.editAfterCreate;
    }else{
      if(args.size() < 3){
        g_parse_error_cmd = "todo";
        return detail::text_result("usage: todo create <name> [--start <time>] [--deadline <time>] [--repeat <expr>] [--no-edit] [-c]\n", 1);
      }

      name = args[2];
      for(size_t i = 3; i < args.size(); ++i){
        const std::string& tok = args[i];
        if(tok == "--start"){
          std::string value;
          std::string consumeError;
          if(!consumeTimeExpr(args, i, value, consumeError)){
            g_parse_error_cmd = "todo";
            return detail::text_result("todo create: --start " + consumeError + "\n", 1);
          }
          startExpr = value;
        }else if(tok == "--deadline"){
          std::string value;
          std::string consumeError;
          if(!consumeTimeExpr(args, i, value, consumeError)){
            g_parse_error_cmd = "todo";
            return detail::text_result("todo create: --deadline " + consumeError + "\n", 1);
          }
          deadlineExpr = value;
        }else if(tok == "--repeat"){
          std::string value;
          std::string consumeError;
          if(!consumeRepeatExpr(args, i, value, consumeError)){
            g_parse_error_cmd = "todo";
            return detail::text_result("todo create: --repeat " + consumeError + "\n", 1);
          }
          repeatExpr = value;
        }else if(tok == "--edit"){
          editAfterCreate = true;
        }else if(tok == "--no-edit"){
          editAfterCreate = false;
        }else if(tok == "-c"){
          editAfterCreate = true;
        }else{
          g_parse_error_cmd = "todo";
          return detail::text_result("todo create: unknown option " + tok + "\n", 1);
        }
      }
    }

    if(!isValidName(name)){
      return detail::text_result("todo: name must contain only letters, digits, or underscores\n", 1);
    }

    auto tasks = loadTasks();
    if(findTask(tasks, name)){
      return detail::text_result("todo: task already exists: " + name + "\n", 1);
    }

    long long now = nowSeconds();
    TodoTask task;
    task.name = name;
    task.createdAt = now;
    task.updatedAt = now;
    task.startAt = now;
    task.deadlineAt = 0;
    task.repeatSeconds = 0;

    if(startExpr){
      long long parsed = 0;
      if(!parseTimeExpr(*startExpr, now, parsed)){
        return detail::text_result("todo create: invalid start time: " + *startExpr + "\n", 1);
      }
      task.startAt = parsed;
    }

    if(deadlineExpr){
      long long parsed = 0;
      if(!parseTimeExpr(*deadlineExpr, now, parsed)){
        return detail::text_result("todo create: invalid deadline: " + *deadlineExpr + "\n", 1);
      }
      task.deadlineAt = parsed;
    }

    if(repeatExpr){
      long long repeatSeconds = 0;
      std::string normalized;
      if(!parseRepeatExpr(*repeatExpr, repeatSeconds, normalized)){
        return detail::text_result("todo create: invalid repeat expression: " + *repeatExpr + "\n", 1);
      }
      task.repeatSeconds = repeatSeconds;
      task.repeatExpr = normalized;
      if(task.deadlineAt <= 0){
        task.deadlineAt = task.startAt + repeatSeconds;
      }
    }

    if(task.deadlineAt > 0 && task.startAt > task.deadlineAt){
      return detail::text_result("todo create: start time is after deadline\n", 1);
    }

    appendDetail(task, now, "Create task");
    if(editAfterCreate){
      std::string editorError;
      std::optional<TodoEditorPayload> payload = openEditorForTask(request, task, editorError);
      if(payload){
        (void)applyEditorPayloadToTask(task, *payload);
      }else{
        std::error_code cleanupEc;
        std::filesystem::remove(todoDetailFilePath(task.name), cleanupEc);
        return detail::text_result("todo create: " + editorError + "\n", 1);
      }
    }

    tasks.push_back(task);
    std::sort(tasks.begin(), tasks.end(), [](const TodoTask& lhs, const TodoTask& rhs){
      return lhs.name < rhs.name;
    });

    std::string persistError;
    if(!persistTasks(tasks, persistError)){
      return detail::text_result("todo: " + persistError + "\n", 1);
    }
    appendOperation("create " + task.name);

    std::ostringstream oss;
    oss << "todo created: " << task.name << "\n";
    oss << "start: " << formatTime(task.startAt) << "\n";
    oss << "deadline: " << formatTime(task.deadlineAt) << "\n";
    if(task.repeatSeconds > 0){
      oss << "repeat: " << task.repeatExpr << "\n";
    }
    return detail::text_result(oss.str());
  }

  static ToolExecutionResult handleUpdate(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    const std::string usage =
      "usage: todo update <name> <add|start|deadline|edit> ... [-c]\n"
      "       todo update <name> -c\n";
    if(args.size() < 4){
      g_parse_error_cmd = "todo";
      return detail::text_result(usage, 1);
    }
    const std::string name = args[2];
    auto tasks = loadTasks();
    TodoTask* task = findTask(tasks, name);
    if(!task){
      return detail::text_result("todo: task not found: " + name + "\n", 1);
    }
    long long now = nowSeconds();

    bool openInCode = false;
    std::vector<std::string> rest;
    rest.reserve(args.size());
    for(size_t i = 3; i < args.size(); ++i){
      if(args[i] == "-c"){
        openInCode = true;
      }else{
        rest.push_back(args[i]);
      }
    }

    std::string action;
    if(rest.empty()){
      if(!openInCode){
        g_parse_error_cmd = "todo";
        return detail::text_result(usage, 1);
      }
      action = "edit";
    }else{
      action = rest[0];
    }

    bool actionChanged = false;
    if(action == "add"){
      if(rest.size() < 2){
        return detail::text_result("usage: todo update <name> add <text...>\n", 1);
      }
      std::vector<std::string> textParts(rest.begin() + 1, rest.end());
      std::string text = trimCopy(join(textParts));
      if(text.empty()){
        return detail::text_result("todo update: empty detail text\n", 1);
      }
      task->todoItems.push_back(text);
      task->todoItems = normalizedTodoItems(task->todoItems);
      appendDetail(*task, now, "Add \"" + text + "\"");
      actionChanged = true;
    }else if(action == "start" || action == "deadline"){
      if(rest.size() < 2){
        return detail::text_result("usage: todo update <name> " + action + " <time>\n", 1);
      }
      std::string expr;
      if(rest.size() == 2){
        expr = rest[1];
      }else if(rest.size() == 3 && looksLikeDateToken(rest[1]) && looksLikeClockToken(rest[2])){
        expr = rest[1] + " " + rest[2];
      }else{
        return detail::text_result("todo update: invalid time expression\n", 1);
      }

      long long parsed = 0;
      if(!parseTimeExpr(expr, now, parsed)){
        return detail::text_result("todo update: invalid time expression: " + expr + "\n", 1);
      }
      if(action == "start"){
        task->startAt = parsed;
        appendDetail(*task, now, "Reset StartTime -> " + formatTime(parsed));
      }else{
        task->deadlineAt = parsed;
        appendDetail(*task, now, "Reset Deadline -> " + formatTime(parsed));
      }
      if(task->deadlineAt > 0 && task->startAt > task->deadlineAt){
        return detail::text_result("todo update: start time is after deadline\n", 1);
      }
      actionChanged = true;
    }else if(action == "edit"){
      if(!rest.empty() && rest.size() != 1){
        return detail::text_result("usage: todo update <name> [edit] [-c]\n", 1);
      }
    }else{
      g_parse_error_cmd = "todo";
      return detail::text_result("todo update: unknown action " + action + "\n", 1);
    }

    if(action == "edit" || openInCode){
      std::string editorError;
      std::optional<TodoEditorPayload> payload = openEditorForTask(request, *task, editorError);
      if(!payload){
        return detail::text_result("todo update: " + editorError + "\n", 1);
      }
      bool changed = applyEditorPayloadToTask(*task, *payload);
      if(!changed && !actionChanged){
        return detail::text_result("todo update: no content captured\n");
      }
    }

    std::string persistError;
    if(!persistTasks(tasks, persistError)){
      return detail::text_result("todo: " + persistError + "\n", 1);
    }
    appendOperation("update " + task->name + " " + action);

    std::ostringstream oss;
    oss << "todo updated: " << task->name << "\n";
    oss << "start: " << formatTime(task->startAt) << "\n";
    oss << "deadline: " << formatTime(task->deadlineAt) << "\n";
    return detail::text_result(oss.str());
  }

  static ToolExecutionResult handleEdit(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 3 || args.size() > 4){
      g_parse_error_cmd = "todo";
      return detail::text_result("usage: todo edit <name> [-c]\n", 1);
    }
    if(args.size() == 4 && args[3] != "-c"){
      g_parse_error_cmd = "todo";
      return detail::text_result("usage: todo edit <name> [-c]\n", 1);
    }
    auto tasks = loadTasks();
    TodoTask* task = findTask(tasks, args[2]);
    if(!task){
      return detail::text_result("todo: task not found: " + args[2] + "\n", 1);
    }
    std::string editorError;
    std::optional<TodoEditorPayload> payload = openEditorForTask(request, *task, editorError);
    if(!payload){
      return detail::text_result("todo edit: " + editorError + "\n", 1);
    }
    bool changed = applyEditorPayloadToTask(*task, *payload);
    if(!changed){
      return detail::text_result("todo edit: no content captured\n");
    }

    std::string persistError;
    if(!persistTasks(tasks, persistError)){
      return detail::text_result("todo: " + persistError + "\n", 1);
    }
    appendOperation("edit " + task->name);
    return detail::text_result("todo edited: " + task->name + "\n");
  }

  static ToolExecutionResult handleDelete(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 3){
      g_parse_error_cmd = "todo";
      return detail::text_result("usage: todo delete <name> [per] [-f]\n", 1);
    }
    std::optional<std::string> name;
    bool removePeriodicPlan = false;
    bool force = false;
    for(size_t i = 2; i < args.size(); ++i){
      const std::string& token = args[i];
      if(token == "per"){
        removePeriodicPlan = true;
      }else if(token == "-f"){
        force = true;
      }else if(startsWith(token, "-")){
        g_parse_error_cmd = "todo";
        return detail::text_result("todo delete: unknown option " + token + "\n", 1);
      }else if(!name){
        name = token;
      }else{
        g_parse_error_cmd = "todo";
        return detail::text_result("usage: todo delete <name> [per] [-f]\n", 1);
      }
    }
    if(!name){
      g_parse_error_cmd = "todo";
      return detail::text_result("usage: todo delete <name> [per] [-f]\n", 1);
    }

    auto tasks = loadTasks();
    TodoTask* task = findTask(tasks, *name);
    if(!task){
      return detail::text_result("todo: task not found: " + *name + "\n", 1);
    }

    const bool allowPrompt = !(request.silent || request.forLLM);
    const bool isPeriodic = task->repeatSeconds > 0;
    long long now = nowSeconds();

    if(isPeriodic && !removePeriodicPlan){
      if(!confirmAction(localeText("Delete current cycle of ", "删除当前循环周期：") + task->name +
                        localeText("?", "？"), force, allowPrompt)){
        return detail::text_result(localeText("delete cancelled\n", "删除已取消\n"), 1);
      }
      TodoResolvedTiming timing = resolveTiming(*task, now);
      if(timing.hasDeadline){
        task->startAt = timing.startAt + task->repeatSeconds;
        task->deadlineAt = timing.deadlineAt + task->repeatSeconds;
      }else{
        task->startAt = timing.startAt + task->repeatSeconds;
      }
      appendDetail(*task, now, "Delete current periodic cycle");
      std::string persistError;
      if(!persistTasks(tasks, persistError)){
        return detail::text_result("todo: " + persistError + "\n", 1);
      }
      appendOperation("delete-cycle " + task->name);
      return detail::text_result(localeText("todo periodic cycle deleted: ", "已删除循环周期：") + task->name + "\n");
    }

    if(!confirmAction(localeText("Delete task ", "删除任务：") + task->name +
                      localeText("?", "？"), force, allowPrompt)){
      return detail::text_result(localeText("delete cancelled\n", "删除已取消\n"), 1);
    }

    std::string targetName = task->name;
    tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [&](const TodoTask& item){
      return item.name == targetName;
    }), tasks.end());
    std::string persistError;
    if(!persistTasks(tasks, persistError)){
      return detail::text_result("todo: " + persistError + "\n", 1);
    }
    appendOperation("delete " + *name);
    return detail::text_result(localeText("todo deleted: ", "已删除任务：") + *name + "\n");
  }

  static ToolExecutionResult handleQuery(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() > 3){
      g_parse_error_cmd = "todo";
      return detail::text_result("usage: todo query [<+time>]\n", 1);
    }
    long long now = nowSeconds();
    std::optional<long long> deadlineUpper;
    if(args.size() == 3){
      long long delta = 0;
      if(!parseRelativeSeconds(args[2], delta)){
        return detail::text_result("todo query: invalid range, expected +30m/+1h/+3d\n", 1);
      }
      deadlineUpper = now + delta;
    }

    auto tasks = loadTasks();
    std::vector<TodoListItem> items;
    for(const auto& task : tasks){
      TodoResolvedTiming timing = resolveTiming(task, now);
      if(task.repeatSeconds <= 0 && timing.hasDeadline && timing.deadlineAt < now){
        continue;
      }
      if(deadlineUpper){
        if(!timing.hasDeadline) continue;
        if(timing.deadlineAt > *deadlineUpper) continue;
      }
      TodoListItem item;
      item.task = &task;
      item.timing = timing;
      items.push_back(item);
    }
    std::sort(items.begin(), items.end(), [](const TodoListItem& lhs, const TodoListItem& rhs){
      if(lhs.timing.hasDeadline != rhs.timing.hasDeadline){
        return lhs.timing.hasDeadline > rhs.timing.hasDeadline;
      }
      if(lhs.timing.hasDeadline && rhs.timing.hasDeadline){
        if(lhs.timing.deadlineAt != rhs.timing.deadlineAt){
          return lhs.timing.deadlineAt < rhs.timing.deadlineAt;
        }
      }
      return lhs.task->name < rhs.task->name;
    });

    if(items.empty()){
      return detail::text_result("no pending tasks\n");
    }
    std::ostringstream oss;
    for(const auto& item : items){
      oss << item.task->name << "  deadline: ";
      if(item.timing.hasDeadline){
        oss << formatTime(item.timing.deadlineAt) << " (" << formatDuration(item.timing.deadlineAt - now) << ")";
      }else{
        oss << "none";
      }
      oss << "\n";
    }
    return detail::text_result(oss.str());
  }

  static ToolExecutionResult handleToday(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    bool deadlineOnly = false;
    if(args.size() > 3){
      g_parse_error_cmd = "todo";
      return detail::text_result("usage: todo today [deadline]\n", 1);
    }
    if(args.size() == 3){
      if(toLowerCopy(args[2]) != "deadline"){
        return detail::text_result("todo today: only optional keyword `deadline` is supported\n", 1);
      }
      deadlineOnly = true;
    }

    long long now = nowSeconds();
    std::time_t nowT = static_cast<std::time_t>(now);
    std::tm dayTm{};
#ifdef _WIN32
    localtime_s(&dayTm, &nowT);
#else
    dayTm = *std::localtime(&nowT);
#endif
    dayTm.tm_hour = 0;
    dayTm.tm_min = 0;
    dayTm.tm_sec = 0;
    long long dayStart = static_cast<long long>(std::mktime(&dayTm));
    long long dayEnd = dayStart + 86400 - 1;

    auto tasks = loadTasks();
    std::vector<TodoListItem> items;
    for(const auto& task : tasks){
      TodoResolvedTiming timing = resolveTiming(task, now);
      bool include = false;
      if(deadlineOnly){
        include = timing.hasDeadline && timing.deadlineAt >= dayStart && timing.deadlineAt <= dayEnd;
      }else{
        if(timing.hasDeadline){
          include = (timing.startAt <= dayEnd && timing.deadlineAt >= dayStart);
          if(task.repeatSeconds <= 0 && timing.deadlineAt < now){
            include = false;
          }
        }else{
          include = timing.startAt <= dayEnd;
        }
      }
      if(!include) continue;
      TodoListItem item;
      item.task = &task;
      item.timing = timing;
      items.push_back(item);
    }

    std::sort(items.begin(), items.end(), [](const TodoListItem& lhs, const TodoListItem& rhs){
      if(lhs.timing.hasDeadline != rhs.timing.hasDeadline){
        return lhs.timing.hasDeadline > rhs.timing.hasDeadline;
      }
      if(lhs.timing.hasDeadline && rhs.timing.hasDeadline){
        if(lhs.timing.deadlineAt != rhs.timing.deadlineAt){
          return lhs.timing.deadlineAt < rhs.timing.deadlineAt;
        }
      }
      return lhs.task->name < rhs.task->name;
    });

    if(items.empty()){
      return detail::text_result("no tasks matched today\n");
    }
    std::ostringstream oss;
    for(const auto& item : items){
      oss << item.task->name << "  deadline: ";
      if(item.timing.hasDeadline){
        oss << formatTime(item.timing.deadlineAt);
      }else{
        oss << "none";
      }
      oss << "\n";
    }
    return detail::text_result(oss.str());
  }

  static ToolExecutionResult handleDetail(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 3 || args.size() > 4){
      g_parse_error_cmd = "todo";
      return detail::text_result("usage: todo detail <name> [-c]\n", 1);
    }
    bool openInCode = false;
    if(args.size() == 4){
      if(args[3] != "-c"){
        g_parse_error_cmd = "todo";
        return detail::text_result("usage: todo detail <name> [-c]\n", 1);
      }
      openInCode = true;
    }
    auto tasks = loadTasks();
    TodoTask* task = findTask(tasks, args[2]);
    if(!task){
      return detail::text_result("todo: task not found: " + args[2] + "\n", 1);
    }
    if(openInCode){
      std::string editorError;
      std::optional<TodoEditorPayload> payload = openEditorForTask(request, *task, editorError);
      if(!payload){
        return detail::text_result("todo detail: " + editorError + "\n", 1);
      }
      if(applyEditorPayloadToTask(*task, *payload)){
        std::string persistError;
        if(!persistTasks(tasks, persistError)){
          return detail::text_result("todo: " + persistError + "\n", 1);
        }
        appendOperation("edit " + task->name);
      }
    }
    long long now = nowSeconds();
    TodoResolvedTiming timing = resolveTiming(*task, now);
    long long length = 0;
    if(timing.hasDeadline){
      length = std::max<long long>(0, timing.deadlineAt - timing.startAt);
    }

    std::ostringstream oss;
    oss << "Name: " << task->name << "\n";
    oss << "Type: " << (task->repeatSeconds > 0 ? ("per " + (task->repeatExpr.empty() ? "d" : task->repeatExpr)) : "none") << "\n";
    oss << "Created: " << formatTime(task->createdAt) << "\n";
    oss << "Updated: " << formatTime(task->updatedAt) << "\n";
    oss << "StartTime: " << formatTime(timing.startAt) << "\n";
    oss << "Deadline: " << formatTime(timing.deadlineAt) << "\n";
    oss << "Length: " << length << "(s)\n";
    oss << "Todo:\n";
    oss << "[\n";
    for(const auto& item : task->todoItems){
      oss << "  - " << summarizeText(item) << "\n";
    }
    oss << "]\n";
    oss << "Details:\n";
    oss << "{\n";
    for(const auto& detail : task->details){
      oss << "  Update on " << formatTime(detail.ts) << " " << summarizeText(detail.text) << "\n";
    }
    oss << "}\n";
    oss << "File: " << todoDetailFilePath(task->name).string() << "\n";
    return detail::text_result(oss.str());
  }

  static ToolExecutionResult handleLast(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 3 || args.size() > 4){
      g_parse_error_cmd = "todo";
      return detail::text_result("usage: todo last <name> [-c]\n", 1);
    }
    bool openInCode = false;
    if(args.size() == 4){
      if(args[3] != "-c"){
        g_parse_error_cmd = "todo";
        return detail::text_result("usage: todo last <name> [-c]\n", 1);
      }
      openInCode = true;
    }
    auto tasks = loadTasks();
    TodoTask* task = findTask(tasks, args[2]);
    if(!task){
      return detail::text_result("todo: task not found: " + args[2] + "\n", 1);
    }
    if(openInCode){
      std::string editorError;
      std::optional<TodoEditorPayload> payload = openEditorForTask(request, *task, editorError);
      if(!payload){
        return detail::text_result("todo last: " + editorError + "\n", 1);
      }
      if(applyEditorPayloadToTask(*task, *payload)){
        std::string persistError;
        if(!persistTasks(tasks, persistError)){
          return detail::text_result("todo: " + persistError + "\n", 1);
        }
        appendOperation("edit " + task->name);
      }
    }
    if(task->details.empty()){
      return detail::text_result("todo: no update records for " + task->name + "\n", 1);
    }
    const auto& last = task->details.back();
    std::ostringstream oss;
    oss << "Update on " << formatTime(last.ts) << " " << summarizeText(last.text) << "\n";
    return detail::text_result(oss.str());
  }

  static ToolExecutionResult handleFinished(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    bool force = false;
    bool purge = false;
    for(size_t i = 2; i < args.size(); ++i){
      if(args[i] == "-f") force = true;
      else if(args[i] == "--purge") purge = true;
      else{
        g_parse_error_cmd = "todo";
        return detail::text_result("usage: todo finished [--purge] [-f]\n", 1);
      }
    }

    auto tasks = loadTasks();
    long long now = nowSeconds();
    std::vector<std::string> finishedNames;
    for(const auto& task : tasks){
      if(isFinishedTask(task, now)){
        finishedNames.push_back(task.name);
      }
    }
    if(finishedNames.empty()){
      return detail::text_result("no finished tasks\n");
    }

    std::ostringstream oss;
    oss << "Finished tasks:\n";
    for(const auto& name : finishedNames){
      const TodoTask* task = findTaskConst(tasks, name);
      if(!task) continue;
      oss << "- " << name << "  deadline: " << formatTime(task->deadlineAt) << "\n";
    }

    const bool allowPrompt = !(request.silent || request.forLLM);
    bool shouldPurge = purge;
    if(!shouldPurge){
      shouldPurge = confirmAction("Purge all finished tasks?", force, allowPrompt);
    }else if(!force && !allowPrompt){
      return detail::text_result("todo finished: use -f with --purge in silent mode\n", 1);
    }else if(!force){
      shouldPurge = confirmAction("Purge all finished tasks?", false, allowPrompt);
    }

    if(!shouldPurge){
      return detail::text_result(oss.str());
    }

    tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [&](const TodoTask& task){
      return isFinishedTask(task, now);
    }), tasks.end());

    std::string persistError;
    if(!persistTasks(tasks, persistError)){
      return detail::text_result("todo: " + persistError + "\n", 1);
    }
    appendOperation("purge-finished count=" + std::to_string(finishedNames.size()));
    oss << "purged: " << finishedNames.size() << "\n";
    return detail::text_result(oss.str());
  }
};

inline ToolDefinition make_todo_tool(){
  ToolDefinition def;
  def.ui = Todo::ui();
  def.executor = Todo::run;
  def.completion = Todo::complete;
  return def;
}

} // namespace tool
