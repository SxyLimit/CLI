#pragma once

#include "../globals.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

struct ToDoTask;
struct ToDoTemplate;
struct ToDoSubtask;

struct TimeValueParseResult {
  bool ok = false;
  std::chrono::system_clock::time_point value{};
  bool hasValue = false;
  std::string periodSpec;          // e.g. "per 2d"
  std::chrono::seconds period{};
  std::string error;               // human readable validation message
};

struct ToDoSubtask {
  std::string name;
  double percent = -1.0;
  int step = -1;
  bool completed = false;
};

struct ToDoTask {
  std::string name;
  bool periodic = false;
  std::string periodSpec;
  std::chrono::seconds periodInterval{0};
  std::chrono::system_clock::time_point startTime{};
  std::chrono::system_clock::time_point deadline{};
  bool hasStart = false;
  bool hasDeadline = false;
  std::vector<std::string> categories;
  std::string urgency;
  double progressPercent = -1.0;
  int progressStep = -1;
  std::vector<ToDoSubtask> subtasks;
  std::vector<std::string> predecessors;
  std::vector<std::string> successors;
  std::vector<std::string> history;   // textual history lines
  std::chrono::system_clock::time_point lastUpdated{};
  bool finished = false;
  std::vector<std::string> templatesApplied;
};

struct ToDoTemplate {
  std::string name;
  std::vector<std::string> categories;
  std::string urgency;
  double progressPercent = -1.0;
  int progressStep = -1;
  std::vector<ToDoSubtask> subtasks;
  std::vector<std::string> predecessors;
  std::vector<std::string> successors;
};

struct TaskCreationOptions {
  std::string name;
  std::vector<std::string> addEntries;
  std::optional<std::string> startInput;
  std::optional<std::string> deadlineInput;
  std::vector<std::string> categories;
  std::string urgency;
  std::optional<double> progressPercent;
  std::optional<int> progressStep;
  std::vector<std::string> templates;
  std::vector<ToDoSubtask> subtasks;
  std::vector<std::string> predecessors;
  std::vector<std::string> successors;
  std::optional<std::string> periodInput;
};

class ToDoManager {
public:
  static ToDoManager& instance();

  void initialize();
  bool ready() const { return ready_; }
  const std::string& storageDir() const { return storageDir_; }

  bool setupStorage(const std::string& path, std::string& err);

  std::vector<std::string> taskNames(bool includeFinished = false) const;
  std::vector<std::string> templateNames() const;
  std::set<std::string> categorySet() const;

  bool createTask(const TaskCreationOptions& opts, std::string& err);
  bool addTodoEntry(const std::string& name, const std::string& text, std::string& err);
  bool resetStart(const std::string& name, const std::string& value, std::string& err);
  bool resetDeadline(const std::string& name, const std::string& value, std::string& err);
  bool setUrgency(const std::string& name, const std::string& value, std::string& err);
  bool addCategory(const std::string& name, const std::string& tag, std::string& err);
  bool removeCategory(const std::string& name, const std::string& tag, std::string& err);
  bool setProgressPercent(const std::string& name, double percent, std::string& err);
  bool setProgressStep(const std::string& name, int step, std::string& err);
  bool addSubtask(const std::string& task, const ToDoSubtask& sub, std::string& err);
  bool updateSubtaskPercent(const std::string& task, const std::string& sub, double percent, std::string& err);
  bool updateSubtaskStep(const std::string& task, const std::string& sub, int step, std::string& err);
  bool removeSubtask(const std::string& task, const std::string& sub, std::string& err);
  bool linkPredecessor(const std::string& task, const std::string& other, std::string& err);
  bool linkSuccessor(const std::string& task, const std::string& other, std::string& err);
  bool unlinkPredecessor(const std::string& task, const std::string& other, std::string& err);
  bool unlinkSuccessor(const std::string& task, const std::string& other, std::string& err);
  bool applyTemplate(const std::string& task, const std::string& tpl, std::string& err);
  bool removeTemplate(const std::string& task, const std::string& tpl, std::string& err);
  bool deleteTask(const std::string& name, bool whole, bool& removed, std::string& err);
  bool clearFinished(std::string& err);

  std::vector<ToDoTask> queryUpcoming(std::optional<std::chrono::system_clock::time_point> until) const;
  std::vector<ToDoTask> queryToday(bool deadlineOnly) const;
  std::optional<ToDoTask> findTask(const std::string& name, bool includeFinished = false) const;
  std::optional<std::string> lastUpdate(const std::string& name) const;
  std::vector<ToDoTask> finishedTasks() const;
  std::vector<ToDoTask> tasksByCategory(const std::string& category) const;

  void remindUrgent() const;
  std::string urgentStatus() const;

  static std::string formatTime(const std::chrono::system_clock::time_point& tp);
  static std::string joinList(const std::vector<std::string>& v, const std::string& sep = ", ");

  void appendOperation(const std::string& line);

  TimeValueParseResult parseTimeValue(const std::string& input, bool forDeadline) const;

  void ensureConsistency();

  void showDetails(const ToDoTask& task) const;

private:
  ToDoManager();
  void loadConfig();
  void loadData();
  void saveAll() const;
  void saveTask(const ToDoTask& task) const;
  void saveFinishedTask(const ToDoTask& task) const;
  void saveNames() const;
  void saveTemplates() const;
  void loadTemplates();
  void loadTasksFromFiles();
  void loadFinishedTasks();
  void saveOperationLog() const;

  static std::chrono::system_clock::time_point now();
  static bool parseAbsoluteDate(const std::string& input, std::tm& out);
  static std::string sanitizeName(const std::string& name);

  std::map<std::string, ToDoTask> tasks_;
  std::map<std::string, ToDoTask> finished_;
  std::map<std::string, ToDoTemplate> templates_;
  std::vector<std::string> operationLog_;
  bool ready_ = false;
  std::string storageDir_;
  std::filesystem::path configPath_;
};

ToolSpec make_todo_tool();
Candidates todoCandidates(const ToolSpec& spec, const std::string& buf);
std::string todoContextGhost(const ToolSpec& spec, const std::vector<std::string>& toks);

extern const std::vector<std::string> kUrgencyLevels;

#include "todo_impl.hpp"

