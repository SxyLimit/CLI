#pragma once

#include "../globals.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace std::chrono;

const std::vector<std::string> kUrgencyLevels = {"none", "low", "normal", "high", "critical"};
static constexpr const char* kTimeFormatHelp =
    "时间格式错误：请使用 yyyy.mm.dd[ HH:MM[:SS]]，或相对时间 +1d/+2h/+30m/+45s，或周期 per d/per 2w/per m";

namespace todo_detail {

static const char* kConfigFile = "./tdle_config.json";

std::filesystem::path ensure_parent(const std::filesystem::path& p){
  auto parent = p.parent_path();
  if(!parent.empty() && !std::filesystem::exists(parent)){
    std::filesystem::create_directories(parent);
  }
  return p;
}

static bool readFileLines(const std::filesystem::path& p, std::vector<std::string>& out){
  std::ifstream ifs(p);
  if(!ifs.good()) return false;
  std::string line;
  while(std::getline(ifs, line)) out.push_back(line);
  return true;
}

static std::string lowercase(const std::string& s){
  std::string o=s;
  std::transform(o.begin(), o.end(), o.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
  return o;
}

static bool equalsIgnoreCase(const std::string& a, const std::string& b){
  return lowercase(a)==lowercase(b);
}

static bool isValidName(const std::string& name){
  static const std::regex re("^[A-Za-z0-9_]+$");
  return std::regex_match(name, re);
}

static std::vector<std::string> splitBy(const std::string& s, char sep){
  std::vector<std::string> out;
  std::string cur;
  for(char c: s){
    if(c==sep){
      if(!cur.empty()) out.push_back(cur);
      cur.clear();
    }else{
      cur.push_back(c);
    }
  }
  if(!cur.empty()) out.push_back(cur);
  return out;
}

static std::string trim(const std::string& s){
  size_t b=0,e=s.size();
  while(b<e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while(e>b && std::isspace(static_cast<unsigned char>(s[e-1]))) --e;
  return s.substr(b,e-b);
}

static std::string joinWords(const std::vector<std::string>& words, size_t start, size_t end){
  std::string out;
  for(size_t i=start;i<end;++i){
    if(i>start) out.push_back(' ');
    out += words[i];
  }
  return out;
}

static std::string ensureQuoted(const std::string& s){
  if(s.find(' ')!=std::string::npos){
    std::string o="\"";
    o += s;
    o += "\"";
    return o;
  }
  return s;
}

static std::string fileSafe(const std::string& name){
  std::string safe;
  for(char c: name){
    if(std::isalnum(static_cast<unsigned char>(c))||c=='_'||c=='-') safe.push_back(c);
    else safe.push_back('_');
  }
  return safe;
}

static std::chrono::seconds parsePeriod(const std::string& spec, std::string& normalized){
  std::string s = lowercase(spec);
  if(!startsWith(s, "per")) return std::chrono::seconds(0);
  std::string rest = trim(s.substr(3));
  if(rest.empty()) return std::chrono::seconds(0);
  int value = 1;
  char unit = 0;
  std::string digits;
  for(char c: rest){
    if(std::isdigit(static_cast<unsigned char>(c))) digits.push_back(c);
    else if(!std::isspace(static_cast<unsigned char>(c))) { unit = c; break; }
  }
  if(!digits.empty()) value = std::stoi(digits);
  if(unit==0){
    if(!rest.empty()) unit = rest.back();
  }
  if(unit==0) return std::chrono::seconds(0);
  std::chrono::seconds secs(0);
  switch(unit){
    case 'y': secs = std::chrono::hours(24*365) * value; break;
    case 'm': secs = std::chrono::hours(24*30) * value; break;
    case 'w': secs = std::chrono::hours(24*7) * value; break;
    case 'd': secs = std::chrono::hours(24) * value; break;
    case 'h': secs = std::chrono::hours(1) * value; break;
    default: return std::chrono::seconds(0);
  }
  normalized = "per ";
  normalized += digits.empty()? std::string("1") : digits;
  normalized.push_back(unit);
  return secs;
}

static std::string percentString(double v){
  std::ostringstream oss;
  oss<<std::fixed<<std::setprecision(1)<<v;
  return oss.str();
}

static std::string nowString(){
  auto tp = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(tp);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y.%m.%d %H:%M:%S", &tm);
  return std::string(buf);
}

} // namespace todo_detail

ToDoManager& ToDoManager::instance(){
  static ToDoManager inst;
  return inst;
}

ToDoManager::ToDoManager(){
  configPath_ = todo_detail::kConfigFile;
}

void ToDoManager::initialize(){
  ready_ = false;
  storageDir_.clear();
  if(!g_settings.todoStorageDir.empty()){
    std::string err;
    if(setupStorage(g_settings.todoStorageDir, err)){
      return;
    }
  }
  loadConfig();
  if(ready_) loadData();
}

void ToDoManager::loadConfig(){
  std::ifstream ifs(todo_detail::kConfigFile);
  if(!ifs.good()){
    ready_ = false;
    storageDir_.clear();
    return;
  }
  std::getline(ifs, storageDir_);
  storageDir_ = todo_detail::trim(storageDir_);
  if(!storageDir_.empty()){
    ready_ = true;
    try{
      g_settings.todoStorageDir = std::filesystem::absolute(storageDir_).string();
    }catch(const std::exception&){
      g_settings.todoStorageDir = storageDir_;
    }
  }
}

bool ToDoManager::setupStorage(const std::string& path, std::string& err){
  std::filesystem::path p = path;
  try{
    if(p.empty()){
      err = "路径不能为空";
      return false;
    }
    if(std::filesystem::exists(p)){
      if(!std::filesystem::is_directory(p)){
        err = "路径不是目录";
        return false;
      }
    }else{
      std::filesystem::create_directories(p);
    }
    auto detail = p / "Details";
    auto finished = p / "Finished";
    auto templates = p / "Templates";
    std::filesystem::create_directories(detail);
    std::filesystem::create_directories(finished);
    std::filesystem::create_directories(templates);
    storageDir_ = std::filesystem::absolute(p).string();
    g_settings.todoStorageDir = storageDir_;
    ready_ = true;
    std::ofstream ofs(todo_detail::kConfigFile, std::ios::trunc);
    ofs<<storageDir_;
    ofs.close();
    loadData();
    return true;
  }catch(const std::exception& ex){
    err = ex.what();
    return false;
  }
}

void ToDoManager::loadData(){
  tasks_.clear();
  finished_.clear();
  templates_.clear();
  operationLog_.clear();
  loadTemplates();
  loadTasksFromFiles();
  loadFinishedTasks();
  std::filesystem::path opPath = std::filesystem::path(storageDir_) / "operation.tdle";
  todo_detail::readFileLines(opPath, operationLog_);
}

std::vector<std::string> ToDoManager::taskNames(bool includeFinished) const{
  std::vector<std::string> names;
  for(auto &kv : tasks_) names.push_back(kv.first);
  if(includeFinished){
    for(auto &kv : finished_) names.push_back(kv.first);
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::vector<std::string> ToDoManager::templateNames() const{
  std::vector<std::string> names;
  for(auto &kv : templates_) names.push_back(kv.first);
  std::sort(names.begin(), names.end());
  return names;
}

std::set<std::string> ToDoManager::categorySet() const{
  std::set<std::string> s;
  for(auto &kv : tasks_){
    s.insert(kv.second.categories.begin(), kv.second.categories.end());
  }
  for(auto &kv : templates_){
    s.insert(kv.second.categories.begin(), kv.second.categories.end());
  }
  return s;
}

static std::string historyLine(const std::string& action, const std::string& payload){
  std::ostringstream oss;
  oss << "Updata on " << todo_detail::nowString() << " " << action;
  if(!payload.empty()) oss << " " << payload;
  return oss.str();
}

bool ToDoManager::createTask(const TaskCreationOptions& opts, std::string& err){
  if(!ready_){ err="未设置保存目录"; return false; }
  if(opts.name.empty()){
    err = "任务名称不能为空";
    return false;
  }
  if(!todo_detail::isValidName(opts.name)){
    err = "任务名只能包含字母、数字和下划线";
    return false;
  }
  if(tasks_.count(opts.name) || finished_.count(opts.name)){
    err = "任务已存在";
    return false;
  }
  ToDoTask task;
  task.name = opts.name;
  task.urgency = opts.urgency;
  task.categories = opts.categories;
  task.progressPercent = opts.progressPercent.value_or(-1.0);
  task.progressStep = opts.progressStep.value_or(-1);
  task.lastUpdated = now();
  task.hasStart = false;
  task.hasDeadline = false;
  if(opts.startInput){
    auto parsed = parseTimeValue(*opts.startInput, false);
    if(!parsed.ok){ err = parsed.error.empty()? "开始时间格式错误" : parsed.error; return false; }
    if(parsed.hasValue){ task.startTime = parsed.value; task.hasStart = true; }
    if(!parsed.periodSpec.empty()){
      task.periodic = true;
      task.periodSpec = parsed.periodSpec;
      task.periodInterval = parsed.period;
    }
  }
  if(opts.deadlineInput){
    auto parsed = parseTimeValue(*opts.deadlineInput, true);
    if(!parsed.ok){ err = parsed.error.empty()? "截止时间格式错误" : parsed.error; return false; }
    if(parsed.hasValue){ task.deadline = parsed.value; task.hasDeadline = true; }
    if(!parsed.periodSpec.empty()){
      task.periodic = true;
      task.periodSpec = parsed.periodSpec;
      task.periodInterval = parsed.period;
      if(!task.hasDeadline && parsed.hasValue){ task.deadline = parsed.value; task.hasDeadline = true; }
    }
  }
  if(opts.periodInput){
    std::string norm;
    auto secs = todo_detail::parsePeriod(*opts.periodInput, norm);
    if(secs.count()==0){ err="周期格式错误：示例 per d、per 2w、per 3m"; return false; }
    task.periodic = true;
    task.periodSpec = norm;
    task.periodInterval = secs;
    if(!task.hasDeadline){ task.deadline = now() + secs; task.hasDeadline = true; }
  }
  task.subtasks = opts.subtasks;
  task.predecessors = opts.predecessors;
  task.successors = opts.successors;
  for(auto &tplName : opts.templates){
    auto it = templates_.find(tplName);
    if(it!=templates_.end()){
      for(auto &c : it->second.categories){
        if(std::find(task.categories.begin(), task.categories.end(), c)==task.categories.end())
          task.categories.push_back(c);
      }
      if(!it->second.urgency.empty()) task.urgency = it->second.urgency;
      if(it->second.progressPercent>=0) task.progressPercent = it->second.progressPercent;
      if(it->second.progressStep>=0) task.progressStep = it->second.progressStep;
      for(auto &sub : it->second.subtasks){
        auto sit = std::find_if(task.subtasks.begin(), task.subtasks.end(), [&](const ToDoSubtask& s){ return todo_detail::equalsIgnoreCase(s.name, sub.name); });
        if(sit==task.subtasks.end()) task.subtasks.push_back(sub);
      }
      for(auto &p : it->second.predecessors){
        if(std::find(task.predecessors.begin(), task.predecessors.end(), p)==task.predecessors.end()) task.predecessors.push_back(p);
      }
      for(auto &s : it->second.successors){
        if(std::find(task.successors.begin(), task.successors.end(), s)==task.successors.end()) task.successors.push_back(s);
      }
      task.templatesApplied.push_back(tplName);
    }
  }
  for(auto &entry : opts.addEntries){
    task.history.push_back(historyLine("Add", todo_detail::ensureQuoted(entry)));
  }
  tasks_[task.name] = task;
  appendOperation("Creat " + task.name);
  for(auto &entry : opts.addEntries){
    appendOperation("Updata " + task.name + " Add " + todo_detail::ensureQuoted(entry));
  }
  saveTask(task);
  saveAll();
  return true;
}

bool ToDoManager::addTodoEntry(const std::string& name, const std::string& text, std::string& err){
  auto it = tasks_.find(name);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  it->second.history.push_back(historyLine("Add", todo_detail::ensureQuoted(text)));
  it->second.lastUpdated = now();
  appendOperation("Updata " + name + " Add " + todo_detail::ensureQuoted(text));
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::resetStart(const std::string& name, const std::string& value, std::string& err){
  auto it = tasks_.find(name);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  auto parsed = parseTimeValue(value, false);
  if(!parsed.ok){ err = parsed.error.empty()? "开始时间格式错误" : parsed.error; return false; }
  if(parsed.hasValue){ it->second.startTime = parsed.value; it->second.hasStart = true; }
  if(!parsed.periodSpec.empty()){
    it->second.periodic = true;
    it->second.periodSpec = parsed.periodSpec;
    it->second.periodInterval = parsed.period;
  }
  it->second.lastUpdated = now();
  appendOperation("Updata " + name + " Reset StartTime " + value);
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::resetDeadline(const std::string& name, const std::string& value, std::string& err){
  auto it = tasks_.find(name);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  auto parsed = parseTimeValue(value, true);
  if(!parsed.ok){ err = parsed.error.empty()? "截止时间格式错误" : parsed.error; return false; }
  if(parsed.hasValue){ it->second.deadline = parsed.value; it->second.hasDeadline = true; }
  if(!parsed.periodSpec.empty()){
    it->second.periodic = true;
    it->second.periodSpec = parsed.periodSpec;
    it->second.periodInterval = parsed.period;
  }
  it->second.lastUpdated = now();
  appendOperation("Updata " + name + " Reset Deadline " + value);
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::setUrgency(const std::string& name, const std::string& value, std::string& err){
  auto it = tasks_.find(name);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  auto lv = todo_detail::lowercase(value);
  if(std::find_if(kUrgencyLevels.begin(), kUrgencyLevels.end(), [&](const std::string& v){ return todo_detail::lowercase(v)==lv; })==kUrgencyLevels.end()){
    err = "紧急程度必须为";
    for(size_t i=0;i<kUrgencyLevels.size();++i){ if(i) err += ","; err += kUrgencyLevels[i]; }
    return false;
  }
  it->second.urgency = value;
  it->second.lastUpdated = now();
  appendOperation("Updata " + name + " Urgency " + value);
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::addCategory(const std::string& name, const std::string& tag, std::string& err){
  auto it = tasks_.find(name);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  if(std::find(it->second.categories.begin(), it->second.categories.end(), tag)==it->second.categories.end())
    it->second.categories.push_back(tag);
  it->second.lastUpdated = now();
  appendOperation("Updata " + name + " Tag Add " + tag);
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::removeCategory(const std::string& name, const std::string& tag, std::string& err){
  auto it = tasks_.find(name);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  auto& vec = it->second.categories;
  vec.erase(std::remove(vec.begin(), vec.end(), tag), vec.end());
  it->second.lastUpdated = now();
  appendOperation("Updata " + name + " Tag Remove " + tag);
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::setProgressPercent(const std::string& name, double percent, std::string& err){
  if(percent<0 || percent>100){ err="百分比应在0-100"; return false; }
  auto it = tasks_.find(name);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  it->second.progressPercent = percent;
  it->second.lastUpdated = now();
  appendOperation("Updata " + name + " Progress Percent " + todo_detail::percentString(percent));
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::setProgressStep(const std::string& name, int step, std::string& err){
  if(step<0){ err="步数应为非负"; return false; }
  auto it = tasks_.find(name);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  it->second.progressStep = step;
  it->second.lastUpdated = now();
  appendOperation("Updata " + name + " Progress Step " + std::to_string(step));
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::addSubtask(const std::string& taskName, const ToDoSubtask& sub, std::string& err){
  auto it = tasks_.find(taskName);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  if(std::find_if(it->second.subtasks.begin(), it->second.subtasks.end(), [&](const ToDoSubtask& s){ return todo_detail::equalsIgnoreCase(s.name, sub.name); })!=it->second.subtasks.end()){
    err="子任务已存在";
    return false;
  }
  it->second.subtasks.push_back(sub);
  it->second.lastUpdated = now();
  appendOperation("Updata " + taskName + " Subtask Add " + sub.name);
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::updateSubtaskPercent(const std::string& taskName, const std::string& sub, double percent, std::string& err){
  auto it = tasks_.find(taskName);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  auto st = std::find_if(it->second.subtasks.begin(), it->second.subtasks.end(), [&](const ToDoSubtask& s){ return todo_detail::equalsIgnoreCase(s.name, sub); });
  if(st==it->second.subtasks.end()){ err="未找到子任务"; return false; }
  if(percent<0||percent>100){ err="百分比应在0-100"; return false; }
  st->percent = percent;
  st->step = -1;
  st->completed = percent>=100.0;
  it->second.lastUpdated = now();
  appendOperation("Updata " + taskName + " Subtask " + st->name + " Percent " + todo_detail::percentString(percent));
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::updateSubtaskStep(const std::string& taskName, const std::string& sub, int step, std::string& err){
  auto it = tasks_.find(taskName);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  auto st = std::find_if(it->second.subtasks.begin(), it->second.subtasks.end(), [&](const ToDoSubtask& s){ return todo_detail::equalsIgnoreCase(s.name, sub); });
  if(st==it->second.subtasks.end()){ err="未找到子任务"; return false; }
  if(step<0){ err="步数应为非负"; return false; }
  st->step = step;
  st->percent = -1.0;
  st->completed = false;
  it->second.lastUpdated = now();
  appendOperation("Updata " + taskName + " Subtask " + st->name + " Step " + std::to_string(step));
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::removeSubtask(const std::string& taskName, const std::string& sub, std::string& err){
  auto it = tasks_.find(taskName);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  auto& vec = it->second.subtasks;
  auto before = vec.size();
  vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const ToDoSubtask& s){ return todo_detail::equalsIgnoreCase(s.name, sub); }), vec.end());
  if(vec.size()==before){ err="未找到子任务"; return false; }
  it->second.lastUpdated = now();
  appendOperation("Updata " + taskName + " Subtask Remove " + sub);
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::linkPredecessor(const std::string& taskName, const std::string& other, std::string& err){
  if(tasks_.find(other)==tasks_.end() && finished_.find(other)==finished_.end()){
    err="前驱任务不存在";
    return false;
  }
  auto it = tasks_.find(taskName);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  if(std::find(it->second.predecessors.begin(), it->second.predecessors.end(), other)==it->second.predecessors.end())
    it->second.predecessors.push_back(other);
  it->second.lastUpdated = now();
  appendOperation("Updata " + taskName + " Link Pre " + other);
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::linkSuccessor(const std::string& taskName, const std::string& other, std::string& err){
  if(tasks_.find(other)==tasks_.end() && finished_.find(other)==finished_.end()){
    err="后继任务不存在";
    return false;
  }
  auto it = tasks_.find(taskName);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  if(std::find(it->second.successors.begin(), it->second.successors.end(), other)==it->second.successors.end())
    it->second.successors.push_back(other);
  it->second.lastUpdated = now();
  appendOperation("Updata " + taskName + " Link Post " + other);
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::unlinkPredecessor(const std::string& taskName, const std::string& other, std::string& err){
  auto it = tasks_.find(taskName);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  auto& vec = it->second.predecessors;
  vec.erase(std::remove(vec.begin(), vec.end(), other), vec.end());
  it->second.lastUpdated = now();
  appendOperation("Updata " + taskName + " Unlink Pre " + other);
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::unlinkSuccessor(const std::string& taskName, const std::string& other, std::string& err){
  auto it = tasks_.find(taskName);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  auto& vec = it->second.successors;
  vec.erase(std::remove(vec.begin(), vec.end(), other), vec.end());
  it->second.lastUpdated = now();
  appendOperation("Updata " + taskName + " Unlink Post " + other);
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::applyTemplate(const std::string& taskName, const std::string& tpl, std::string& err){
  auto tt = templates_.find(tpl);
  if(tt==templates_.end()){ err="模板不存在"; return false; }
  auto it = tasks_.find(taskName);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  for(auto &c : tt->second.categories){
    if(std::find(it->second.categories.begin(), it->second.categories.end(), c)==it->second.categories.end())
      it->second.categories.push_back(c);
  }
  if(!tt->second.urgency.empty()) it->second.urgency = tt->second.urgency;
  if(tt->second.progressPercent>=0) it->second.progressPercent = tt->second.progressPercent;
  if(tt->second.progressStep>=0) it->second.progressStep = tt->second.progressStep;
  for(auto &sub : tt->second.subtasks){
    auto sit = std::find_if(it->second.subtasks.begin(), it->second.subtasks.end(), [&](const ToDoSubtask& s){ return todo_detail::equalsIgnoreCase(s.name, sub.name); });
    if(sit==it->second.subtasks.end()) it->second.subtasks.push_back(sub);
  }
  for(auto &p : tt->second.predecessors){
    if(std::find(it->second.predecessors.begin(), it->second.predecessors.end(), p)==it->second.predecessors.end()) it->second.predecessors.push_back(p);
  }
  for(auto &s : tt->second.successors){
    if(std::find(it->second.successors.begin(), it->second.successors.end(), s)==it->second.successors.end()) it->second.successors.push_back(s);
  }
  if(std::find(it->second.templatesApplied.begin(), it->second.templatesApplied.end(), tpl)==it->second.templatesApplied.end())
    it->second.templatesApplied.push_back(tpl);
  it->second.lastUpdated = now();
  appendOperation("Updata " + taskName + " Template Apply " + tpl);
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::removeTemplate(const std::string& taskName, const std::string& tpl, std::string& err){
  auto it = tasks_.find(taskName);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  auto& vec = it->second.templatesApplied;
  vec.erase(std::remove(vec.begin(), vec.end(), tpl), vec.end());
  it->second.lastUpdated = now();
  appendOperation("Updata " + taskName + " Template Remove " + tpl);
  saveTask(it->second);
  saveOperationLog();
  return true;
}

bool ToDoManager::deleteTask(const std::string& name, bool whole, bool& removed, std::string& err){
  removed = false;
  auto it = tasks_.find(name);
  if(it==tasks_.end()){ err="未找到任务"; return false; }
  if(it->second.periodic && !whole){
    if(it->second.periodInterval.count()==0){ err="任务没有周期"; return false; }
    if(it->second.hasStart) it->second.startTime += it->second.periodInterval;
    if(it->second.hasDeadline) it->second.deadline += it->second.periodInterval;
    it->second.history.push_back(historyLine("Cycle Completed", ""));
    it->second.lastUpdated = now();
    appendOperation("Delete " + name + " cycle");
    saveTask(it->second);
    saveOperationLog();
    return true;
  }
  it->second.finished = true;
  finished_[name] = it->second;
  tasks_.erase(it);
  removed = true;
  appendOperation(std::string("Delete ") + name + (whole? " per":""));
  saveAll();
  return true;
}

bool ToDoManager::clearFinished(std::string& err){
  (void)err;
  finished_.clear();
  saveAll();
  return true;
}

std::vector<ToDoTask> ToDoManager::queryUpcoming(std::optional<std::chrono::system_clock::time_point> until) const{
  std::vector<ToDoTask> items;
  for(auto &kv : tasks_){
    if(!kv.second.hasDeadline) continue;
    if(until && kv.second.deadline > *until) continue;
    items.push_back(kv.second);
  }
  std::sort(items.begin(), items.end(), [](const ToDoTask& a, const ToDoTask& b){ return a.deadline < b.deadline; });
  return items;
}

std::vector<ToDoTask> ToDoManager::queryToday(bool deadlineOnly) const{
  auto nowTp = now();
  std::time_t t = std::chrono::system_clock::to_time_t(nowTp);
  std::tm tm{};
  localtime_r(&t, &tm);
  tm.tm_hour = 0; tm.tm_min=0; tm.tm_sec=0;
  auto dayStart = std::chrono::system_clock::from_time_t(std::mktime(&tm));
  auto dayEnd = dayStart + std::chrono::hours(24);
  std::vector<ToDoTask> out;
  for(auto &kv : tasks_){
    const auto& task = kv.second;
    if(deadlineOnly){
      if(task.hasDeadline && task.deadline>=dayStart && task.deadline<dayEnd) out.push_back(task);
    }else{
      bool active = false;
      if(task.hasStart && task.hasDeadline){
        active = task.startTime<=nowTp && nowTp<=task.deadline;
      }else if(task.hasDeadline){
        active = task.deadline>=dayStart;
      }
      if(active) out.push_back(task);
    }
  }
  std::sort(out.begin(), out.end(), [](const ToDoTask& a, const ToDoTask& b){ return a.deadline < b.deadline; });
  return out;
}

std::optional<ToDoTask> ToDoManager::findTask(const std::string& name, bool includeFinished) const{
  auto it = tasks_.find(name);
  if(it!=tasks_.end()) return it->second;
  if(includeFinished){
    auto fi = finished_.find(name);
    if(fi!=finished_.end()) return fi->second;
  }
  return std::nullopt;
}

std::optional<std::string> ToDoManager::lastUpdate(const std::string& name) const{
  auto task = findTask(name, true);
  if(!task) return std::nullopt;
  if(task->history.empty()) return std::nullopt;
  return task->history.back();
}

std::vector<ToDoTask> ToDoManager::finishedTasks() const{
  std::vector<ToDoTask> out;
  for(auto &kv : finished_) out.push_back(kv.second);
  std::sort(out.begin(), out.end(), [](const ToDoTask& a, const ToDoTask& b){ return a.lastUpdated > b.lastUpdated; });
  return out;
}

std::vector<ToDoTask> ToDoManager::tasksByCategory(const std::string& category) const{
  std::vector<ToDoTask> out;
  for(auto &kv : tasks_){
    if(std::find(kv.second.categories.begin(), kv.second.categories.end(), category)!=kv.second.categories.end())
      out.push_back(kv.second);
  }
  std::sort(out.begin(), out.end(), [](const ToDoTask& a, const ToDoTask& b){ return a.deadline < b.deadline; });
  return out;
}

void ToDoManager::remindUrgent() const{
  std::vector<std::string> urgent;
  for(auto &kv : tasks_){
    if(todo_detail::equalsIgnoreCase(kv.second.urgency, "critical")) urgent.push_back(kv.first);
  }
  if(!urgent.empty()){
    std::cout<<ansi::RED<<"提醒：最高优先任务 "<<joinList(urgent)<<ansi::RESET<<"\n";
  }
}

std::string ToDoManager::urgentStatus() const{
  std::vector<std::string> urgent;
  for(auto &kv : tasks_){
    if(todo_detail::equalsIgnoreCase(kv.second.urgency, "critical")) urgent.push_back(kv.first);
  }
  if(urgent.empty()) return "";
  return std::string("[") + "URGENT:" + joinList(urgent) + "] ";
}

void ToDoManager::appendOperation(const std::string& line){
  operationLog_.push_back(todo_detail::nowString() + ": " + line);
  saveOperationLog();
}

TimeValueParseResult ToDoManager::parseTimeValue(const std::string& input, bool forDeadline) const{
  TimeValueParseResult res;
  std::string trimmed = todo_detail::trim(input);
  if(trimmed.empty()){ res.error = kTimeFormatHelp; return res; }
  if(trimmed[0]=='+'){
    std::string digits;
    char unit = 0;
    for(size_t i=1;i<trimmed.size();++i){
      char c = trimmed[i];
      if(std::isdigit(static_cast<unsigned char>(c))) digits.push_back(c);
      else { unit = c; break; }
    }
    if(digits.empty()){ res.error = "时间格式错误：+ 后需要数字，例如 +3d"; return res; }
    if(unit==0){ res.error = "时间格式错误：缺少单位，可使用 d/h/m/s"; return res; }
    long long value = std::stoll(digits);
    std::chrono::seconds offset(0);
    switch(unit){
      case 'd': offset = std::chrono::hours(24) * value; break;
      case 'm': offset = std::chrono::minutes(1) * value; break;
      case 's': offset = std::chrono::seconds(1) * value; break;
      case 'h': offset = std::chrono::hours(1) * value; break;
      default: res.error = "时间格式错误：单位必须为 d/h/m/s"; return res;
    }
    res.ok = true;
    res.hasValue = true;
    res.value = now() + offset;
    return res;
  }
  if(startsWith(trimmed, "per")){
    res.period = todo_detail::parsePeriod(trimmed, res.periodSpec);
    if(res.period.count()==0){ res.ok=false; res.error = "周期格式错误：示例 per d、per 2w、per 3m"; return res; }
    res.ok = true;
    res.hasValue = true;
    if(forDeadline) res.value = now() + res.period;
    else res.value = now();
    return res;
  }
  std::tm tm{};
  if(parseAbsoluteDate(trimmed, tm)){
    std::time_t tt = std::mktime(&tm);
    res.ok = true;
    res.hasValue = true;
    res.value = std::chrono::system_clock::from_time_t(tt);
    return res;
  }
  res.error = kTimeFormatHelp;
  return res;
}

void ToDoManager::ensureConsistency(){
  saveAll();
}

void ToDoManager::showDetails(const ToDoTask& task) const{
  std::cout<<"Name: "<<task.name<<"\n";
  std::cout<<"Type: "<<(task.periodic? (task.periodSpec.empty()?"per":task.periodSpec):"none")<<"\n";
  long long length = 0;
  if(task.hasStart && task.hasDeadline){
    length = std::chrono::duration_cast<std::chrono::seconds>(task.deadline - task.startTime).count();
  }
  std::cout<<"Length: "<<length<<"(s)\n";
  if(task.hasStart) std::cout<<"Start: "<<formatTime(task.startTime)<<"\n";
  if(task.hasDeadline) std::cout<<"Deadline: "<<formatTime(task.deadline)<<"\n";
  if(!task.categories.empty()) std::cout<<"Categories: "<<joinList(task.categories)<<"\n";
  if(!task.urgency.empty()) std::cout<<"Urgency: "<<task.urgency<<"\n";
  if(task.progressPercent>=0) std::cout<<"Progress Percent: "<<todo_detail::percentString(task.progressPercent)<<"%\n";
  if(task.progressStep>=0) std::cout<<"Progress Step: "<<task.progressStep<<"\n";
  if(!task.predecessors.empty()) std::cout<<"Predecessors: "<<joinList(task.predecessors)<<"\n";
  if(!task.successors.empty()) std::cout<<"Successors: "<<joinList(task.successors)<<"\n";
  if(!task.subtasks.empty()){
    std::cout<<"Subtasks:\n";
    for(auto &sub : task.subtasks){
      std::cout<<"  - "<<sub.name;
      if(sub.percent>=0) std::cout<<" ("<<todo_detail::percentString(sub.percent)<<"%)";
      if(sub.step>=0) std::cout<<" [step="<<sub.step<<"]";
      if(sub.completed) std::cout<<" ✔";
      std::cout<<"\n";
    }
  }
  std::cout<<"Details:\n{\n";
  for(auto &line : task.history){
    std::cout<<" "<<line<<",\n";
  }
  std::cout<<"}\n";
}

std::string ToDoManager::formatTime(const std::chrono::system_clock::time_point& tp){
  std::time_t t = std::chrono::system_clock::to_time_t(tp);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y.%m.%d %H:%M:%S", &tm);
  return std::string(buf);
}

std::chrono::system_clock::time_point ToDoManager::now(){
  return std::chrono::system_clock::now();
}

bool ToDoManager::parseAbsoluteDate(const std::string& input, std::tm& tm){
  tm = {};
  std::string s = input;
  std::string datePart, timePart;
  size_t space = s.find(' ');
  if(space!=std::string::npos){
    datePart = s.substr(0, space);
    timePart = s.substr(space+1);
  }else{
    if(s.find(':')!=std::string::npos) timePart = s; else datePart = s;
  }
  std::tm cur{};
  auto nowTp = now();
  std::time_t t = std::chrono::system_clock::to_time_t(nowTp);
  localtime_r(&t, &cur);
  try{
    tm.tm_year = cur.tm_year;
    tm.tm_mon = cur.tm_mon;
    tm.tm_mday = cur.tm_mday;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    if(!datePart.empty()){
      auto parts = todo_detail::splitBy(datePart, '.');
      if(parts.size()==3){
        tm.tm_year = std::stoi(parts[0]) - 1900;
        tm.tm_mon = std::stoi(parts[1]) - 1;
        tm.tm_mday = std::stoi(parts[2]);
      }else if(parts.size()==2){
        tm.tm_mon = std::stoi(parts[0]) - 1;
        tm.tm_mday = std::stoi(parts[1]);
      }else if(parts.size()==1){
        tm.tm_mday = std::stoi(parts[0]);
      }else{
        return false;
      }
    }
    if(!timePart.empty()){
      auto parts = todo_detail::splitBy(timePart, ':');
      if(parts.size()==3){
        tm.tm_hour = std::stoi(parts[0]);
        tm.tm_min = std::stoi(parts[1]);
        tm.tm_sec = std::stoi(parts[2]);
      }else if(parts.size()==2){
        tm.tm_hour = std::stoi(parts[0]);
        tm.tm_min = std::stoi(parts[1]);
      }else if(parts.size()==1){
        tm.tm_hour = std::stoi(parts[0]);
      }else{
        return false;
      }
    }
    return true;
  }catch(const std::exception&){
    return false;
  }
}

std::string ToDoManager::joinList(const std::vector<std::string>& v, const std::string& sep){
  std::string out;
  for(size_t i=0;i<v.size();++i){ if(i) out += sep; out += v[i]; }
  return out;
}

std::string ToDoManager::sanitizeName(const std::string& name){
  return todo_detail::fileSafe(name);
}

void ToDoManager::saveAll() const{
  if(!ready_) return;
  saveNames();
  for(auto &kv : tasks_) saveTask(kv.second);
  for(auto &kv : finished_) saveFinishedTask(kv.second);
  saveTemplates();
  saveOperationLog();
}

void ToDoManager::saveTask(const ToDoTask& task) const{
  std::filesystem::path dir = std::filesystem::path(storageDir_) / "Details";
  std::filesystem::create_directories(dir);
  std::filesystem::path file = dir / (sanitizeName(task.name) + ".tdle");
  todo_detail::ensure_parent(file);
  std::ofstream ofs(file, std::ios::trunc);
  ofs<<"Name: "<<task.name<<"\n";
  ofs<<"Type: "<<(task.periodic? (task.periodSpec.empty()?"per":task.periodSpec):"none")<<"\n";
  long long length = 0;
  if(task.hasStart && task.hasDeadline){
    length = std::chrono::duration_cast<std::chrono::seconds>(task.deadline - task.startTime).count();
  }
  ofs<<"Length: "<<length<<"(s)\n";
  if(task.hasStart) ofs<<"Start: "<<formatTime(task.startTime)<<"\n";
  if(task.hasDeadline) ofs<<"Deadline: "<<formatTime(task.deadline)<<"\n";
  if(!task.categories.empty()) ofs<<"Categories: "<<joinList(task.categories)<<"\n";
  if(!task.urgency.empty()) ofs<<"Urgency: "<<task.urgency<<"\n";
  if(task.progressPercent>=0) ofs<<"ProgressPercent: "<<todo_detail::percentString(task.progressPercent)<<"\n";
  if(task.progressStep>=0) ofs<<"ProgressStep: "<<task.progressStep<<"\n";
  if(!task.templatesApplied.empty()) ofs<<"Templates: "<<joinList(task.templatesApplied)<<"\n";
  if(!task.predecessors.empty()) ofs<<"Predecessors: "<<joinList(task.predecessors)<<"\n";
  if(!task.successors.empty()) ofs<<"Successors: "<<joinList(task.successors)<<"\n";
  if(!task.subtasks.empty()){
    ofs<<"Subtasks:\n";
    for(auto &sub : task.subtasks){
      ofs<<"  - "<<sub.name;
      if(sub.percent>=0) ofs<<" ("<<todo_detail::percentString(sub.percent)<<"%)";
      if(sub.step>=0) ofs<<" [step="<<sub.step<<"]";
      if(sub.completed) ofs<<" ✔";
      ofs<<"\n";
    }
  }
  ofs<<"Details:\n{\n";
  for(auto &line : task.history){
    ofs<<" "<<line<<",\n";
  }
  ofs<<"}\n";
}

void ToDoManager::saveFinishedTask(const ToDoTask& task) const{
  std::filesystem::path dir = std::filesystem::path(storageDir_) / "Finished";
  std::filesystem::create_directories(dir);
  std::filesystem::path file = dir / (sanitizeName(task.name) + ".tdle");
  todo_detail::ensure_parent(file);
  std::ofstream ofs(file, std::ios::trunc);
  ofs<<"Name: "<<task.name<<"\n";
  ofs<<"Finished: yes\n";
  if(!task.categories.empty()) ofs<<"Categories: "<<joinList(task.categories)<<"\n";
  if(!task.urgency.empty()) ofs<<"Urgency: "<<task.urgency<<"\n";
  ofs<<"Details:\n{\n";
  for(auto &line : task.history){ ofs<<" "<<line<<",\n"; }
  ofs<<"}\n";
}

void ToDoManager::saveNames() const{
  std::filesystem::path file = std::filesystem::path(storageDir_) / "name.tdle";
  todo_detail::ensure_parent(file);
  std::ofstream ofs(file, std::ios::trunc);
  ofs<<tasks_.size()<<"\n";
  for(auto &kv : tasks_) ofs<<kv.first<<"\n";
}

void ToDoManager::saveTemplates() const{
  std::filesystem::path dir = std::filesystem::path(storageDir_) / "Templates";
  std::filesystem::create_directories(dir);
  for(auto &kv : templates_){
    std::filesystem::path file = dir / (sanitizeName(kv.first) + ".tdle");
    todo_detail::ensure_parent(file);
    std::ofstream ofs(file, std::ios::trunc);
    ofs<<"Name: "<<kv.first<<"\n";
    if(!kv.second.categories.empty()) ofs<<"Categories: "<<joinList(kv.second.categories)<<"\n";
    if(!kv.second.urgency.empty()) ofs<<"Urgency: "<<kv.second.urgency<<"\n";
    if(kv.second.progressPercent>=0) ofs<<"ProgressPercent: "<<todo_detail::percentString(kv.second.progressPercent)<<"\n";
    if(kv.second.progressStep>=0) ofs<<"ProgressStep: "<<kv.second.progressStep<<"\n";
    if(!kv.second.predecessors.empty()) ofs<<"Predecessors: "<<joinList(kv.second.predecessors)<<"\n";
    if(!kv.second.successors.empty()) ofs<<"Successors: "<<joinList(kv.second.successors)<<"\n";
    if(!kv.second.subtasks.empty()){
      ofs<<"Subtasks:\n";
      for(auto &sub : kv.second.subtasks){
        ofs<<"  - "<<sub.name;
        if(sub.percent>=0) ofs<<" ("<<todo_detail::percentString(sub.percent)<<"%)";
        if(sub.step>=0) ofs<<" [step="<<sub.step<<"]";
        ofs<<"\n";
      }
    }
  }
}

void ToDoManager::loadTemplates(){
  std::filesystem::path dir = std::filesystem::path(storageDir_) / "Templates";
  if(!std::filesystem::exists(dir)) return;
  for(auto &entry : std::filesystem::directory_iterator(dir)){
    if(!entry.is_regular_file()) continue;
    std::ifstream ifs(entry.path());
    if(!ifs.good()) continue;
    ToDoTemplate tpl;
    std::string line;
    while(std::getline(ifs, line)){
      if(line.rfind("Name:",0)==0){ tpl.name = todo_detail::trim(line.substr(5)); }
      else if(line.rfind("Categories:",0)==0){ tpl.categories = todo_detail::splitBy(todo_detail::trim(line.substr(11)), ','); for(auto &c: tpl.categories) c=todo_detail::trim(c); }
      else if(line.rfind("Urgency:",0)==0){ tpl.urgency = todo_detail::trim(line.substr(8)); }
      else if(line.rfind("ProgressPercent:",0)==0){ tpl.progressPercent = std::stod(todo_detail::trim(line.substr(16))); }
      else if(line.rfind("ProgressStep:",0)==0){ tpl.progressStep = std::stoi(todo_detail::trim(line.substr(13))); }
      else if(line.rfind("Predecessors:",0)==0){ tpl.predecessors = todo_detail::splitBy(todo_detail::trim(line.substr(13)), ','); for(auto &c: tpl.predecessors) c=todo_detail::trim(c); }
      else if(line.rfind("Successors:",0)==0){ tpl.successors = todo_detail::splitBy(todo_detail::trim(line.substr(11)), ','); for(auto &c: tpl.successors) c=todo_detail::trim(c); }
      else if(line.rfind("  -",0)==0){
        ToDoSubtask sub;
        std::string rest = todo_detail::trim(line.substr(3));
        size_t pos = rest.find(" (");
        if(pos!=std::string::npos){
          sub.name = rest.substr(0,pos);
          std::string remain = rest.substr(pos+2);
          size_t end = remain.find(')');
          if(end!=std::string::npos){
            sub.percent = std::stod(remain.substr(0,end));
          }
        }else{
          size_t stepPos = rest.find("[step=");
          if(stepPos!=std::string::npos){
            sub.name = todo_detail::trim(rest.substr(0, stepPos));
            size_t close = rest.find(']', stepPos);
            if(close!=std::string::npos){
              sub.step = std::stoi(rest.substr(stepPos+6, close-stepPos-6));
            }
          }else{
            sub.name = rest;
          }
        }
        if(sub.name.empty()) sub.name = rest;
        tpl.subtasks.push_back(sub);
      }
    }
    if(!tpl.name.empty()) templates_[tpl.name] = tpl;
  }
}

void ToDoManager::loadTasksFromFiles(){
  std::filesystem::path dir = std::filesystem::path(storageDir_) / "Details";
  if(!std::filesystem::exists(dir)) return;
  for(auto &entry : std::filesystem::directory_iterator(dir)){
    if(!entry.is_regular_file()) continue;
    std::ifstream ifs(entry.path());
    if(!ifs.good()) continue;
    ToDoTask task;
    std::string line;
    bool details = false;
    while(std::getline(ifs, line)){
      if(line == "Details:" || line=="Details:\r"){ details=true; continue; }
      if(details){
        if(line.find('{')!=std::string::npos || line.find('}')!=std::string::npos) continue;
        std::string trimmedLine = todo_detail::trim(line);
        if(!trimmedLine.empty()){ if(trimmedLine.back()==',') trimmedLine.pop_back(); task.history.push_back(trimmedLine); }
        continue;
      }
      if(line.rfind("Name:",0)==0){ task.name = todo_detail::trim(line.substr(5)); }
      else if(line.rfind("Type:",0)==0){ std::string type = todo_detail::trim(line.substr(5)); if(type.find("per")==0){ task.periodic = true; task.periodSpec = type; std::string norm; task.periodInterval = todo_detail::parsePeriod(type, norm); if(!norm.empty()) task.periodSpec = norm; } }
      else if(line.rfind("Start:",0)==0){ std::tm tm{}; if(parseAbsoluteDate(todo_detail::trim(line.substr(6)), tm)){ task.startTime = std::chrono::system_clock::from_time_t(std::mktime(&tm)); task.hasStart = true; } }
      else if(line.rfind("Deadline:",0)==0){ std::tm tm{}; if(parseAbsoluteDate(todo_detail::trim(line.substr(9)), tm)){ task.deadline = std::chrono::system_clock::from_time_t(std::mktime(&tm)); task.hasDeadline = true; } }
      else if(line.rfind("Categories:",0)==0){ task.categories = todo_detail::splitBy(todo_detail::trim(line.substr(11)), ','); for(auto &c: task.categories) c=todo_detail::trim(c); }
      else if(line.rfind("Urgency:",0)==0){ task.urgency = todo_detail::trim(line.substr(8)); }
      else if(line.rfind("ProgressPercent:",0)==0){ task.progressPercent = std::stod(todo_detail::trim(line.substr(16))); }
      else if(line.rfind("ProgressStep:",0)==0){ task.progressStep = std::stoi(todo_detail::trim(line.substr(13))); }
      else if(line.rfind("Templates:",0)==0){ task.templatesApplied = todo_detail::splitBy(todo_detail::trim(line.substr(10)), ','); for(auto &c: task.templatesApplied) c=todo_detail::trim(c); }
      else if(line.rfind("Predecessors:",0)==0){ task.predecessors = todo_detail::splitBy(todo_detail::trim(line.substr(13)), ','); for(auto &c: task.predecessors) c=todo_detail::trim(c); }
      else if(line.rfind("Successors:",0)==0){ task.successors = todo_detail::splitBy(todo_detail::trim(line.substr(11)), ','); for(auto &c: task.successors) c=todo_detail::trim(c); }
      else if(line.rfind("  -",0)==0){
        ToDoSubtask sub;
        std::string rest = todo_detail::trim(line.substr(3));
        size_t pos = rest.find(" (");
        if(pos!=std::string::npos){
          sub.name = rest.substr(0,pos);
          std::string remain = rest.substr(pos+2);
          size_t end = remain.find(')');
          if(end!=std::string::npos) sub.percent = std::stod(remain.substr(0,end));
          size_t stepPos = remain.find("[step=");
          if(stepPos!=std::string::npos){
            size_t close = remain.find(']', stepPos);
            if(close!=std::string::npos) sub.step = std::stoi(remain.substr(stepPos+6, close-stepPos-6));
          }
        }else{
          size_t stepPos = rest.find("[step=");
          if(stepPos!=std::string::npos){
            sub.name = todo_detail::trim(rest.substr(0, stepPos));
            size_t close = rest.find(']', stepPos);
            if(close!=std::string::npos) sub.step = std::stoi(rest.substr(stepPos+6, close-stepPos-6));
          }else{
            sub.name = rest;
          }
        }
        task.subtasks.push_back(sub);
      }
    }
    if(!task.name.empty()) tasks_[task.name] = task;
  }
}

void ToDoManager::loadFinishedTasks(){
  std::filesystem::path dir = std::filesystem::path(storageDir_) / "Finished";
  if(!std::filesystem::exists(dir)) return;
  for(auto &entry : std::filesystem::directory_iterator(dir)){
    if(!entry.is_regular_file()) continue;
    std::ifstream ifs(entry.path());
    if(!ifs.good()) continue;
    ToDoTask task;
    task.finished = true;
    std::string line;
    while(std::getline(ifs, line)){
      if(line.rfind("Name:",0)==0){ task.name = todo_detail::trim(line.substr(5)); }
      else if(line.rfind("Categories:",0)==0){ task.categories = todo_detail::splitBy(todo_detail::trim(line.substr(11)), ','); for(auto &c: task.categories) c=todo_detail::trim(c); }
      else if(line.rfind("Urgency:",0)==0){ task.urgency = todo_detail::trim(line.substr(8)); }
      else if(line.rfind("Details:",0)==0){
        while(std::getline(ifs, line)){
          std::string trimmedLine = todo_detail::trim(line);
          if(trimmedLine.empty()) continue;
          if(trimmedLine.front()=='}') break;
          if(trimmedLine.back()==',') trimmedLine.pop_back();
          task.history.push_back(trimmedLine);
        }
        break;
      }
    }
    if(!task.name.empty()) finished_[task.name] = task;
  }
}

void ToDoManager::saveOperationLog() const{
  std::filesystem::path file = std::filesystem::path(storageDir_) / "operation.tdle";
  todo_detail::ensure_parent(file);
  std::ofstream ofs(file, std::ios::trunc);
  for(auto &line : operationLog_) ofs<<line<<"\n";
}

static std::string joinTokens(const std::vector<std::string>& toks, size_t start){
  std::string out;
  for(size_t i=start;i<toks.size();++i){ if(i>start) out+=' '; out+=toks[i]; }
  return out;
}

static bool promptConfirm(const std::string& message){
  std::cout<<message<<" (y/n): ";
  std::cout.flush();
  std::string input;
  std::getline(std::cin, input);
  return !input.empty() && (input[0]=='y' || input[0]=='Y');
}

static bool parseDouble(const std::string& s, double& value){
  try{ value = std::stod(s); return true; }catch(...){ return false; }
}

static bool parseInt(const std::string& s, int& value){
  try{ value = std::stoi(s); return true; }catch(...){ return false; }
}

static std::vector<std::string> collectArguments(const std::vector<std::string>& toks, size_t& idx, const std::set<std::string>& stops){
  size_t start = idx;
  while(idx < toks.size() && !stops.count(toks[idx])) ++idx;
  return std::vector<std::string>(toks.begin()+start, toks.begin()+idx);
}

static std::string rebuildText(const std::vector<std::string>& parts){
  std::string out;
  for(size_t i=0;i<parts.size();++i){ if(i) out+=' '; out+=parts[i]; }
  return out;
}

static void printTasksList(const std::vector<ToDoTask>& tasks){
  for(auto &task : tasks){
    std::cout<<task.name;
    if(task.hasDeadline){
      std::cout<<" - deadline "<<ToDoManager::formatTime(task.deadline);
    }
    if(!task.categories.empty()){
      std::cout<<" ["<<ToDoManager::joinList(task.categories)<<"]";
    }
    std::cout<<"\n";
  }
}

static void handleCreat(const std::vector<std::string>& toks){
  if(toks.size()<3){
    std::cout<<ansi::RED<<"缺少任务名称"<<ansi::RESET<<"\n";
    return;
  }
  TaskCreationOptions opts;
  opts.name = toks[2];
  std::set<std::string> keywords = {"Add","StartTime","Deadline","Tag","Urgency","ProgressPercent","ProgressStep","Template","Subtask","Pre","Post","Category","Per"};
  size_t idx = 3;
  while(idx < toks.size()){
    const std::string& token = toks[idx];
    if(token=="Add"){
      ++idx;
      auto parts = collectArguments(toks, idx, keywords);
      opts.addEntries.push_back(rebuildText(parts));
    }else if(token=="StartTime"){
      if(idx+1>=toks.size()){ std::cout<<ansi::RED<<"缺少开始时间"<<ansi::RESET<<"\n"; return; }
      opts.startInput = toks[++idx];
      ++idx;
    }else if(token=="Deadline"){
      if(idx+1>=toks.size()){ std::cout<<ansi::RED<<"缺少截止时间"<<ansi::RESET<<"\n"; return; }
      opts.deadlineInput = toks[++idx];
      ++idx;
    }else if(token=="Tag" || token=="Category"){
      ++idx;
      auto parts = collectArguments(toks, idx, keywords);
      for(auto &p : parts) opts.categories.push_back(p);
    }else if(token=="Urgency"){
      if(idx+1>=toks.size()){ std::cout<<ansi::RED<<"缺少紧急程度"<<ansi::RESET<<"\n"; return; }
      opts.urgency = toks[++idx];
      ++idx;
    }else if(token=="ProgressPercent"){
      if(idx+1>=toks.size()){ std::cout<<ansi::RED<<"缺少百分比"<<ansi::RESET<<"\n"; return; }
      double v; if(!parseDouble(toks[++idx], v)){ std::cout<<ansi::RED<<"百分比格式错误"<<ansi::RESET<<"\n"; return; }
      opts.progressPercent = v;
      ++idx;
    }else if(token=="ProgressStep"){
      if(idx+1>=toks.size()){ std::cout<<ansi::RED<<"缺少步数"<<ansi::RESET<<"\n"; return; }
      int v; if(!parseInt(toks[++idx], v)){ std::cout<<ansi::RED<<"步数格式错误"<<ansi::RESET<<"\n"; return; }
      opts.progressStep = v;
      ++idx;
    }else if(token=="Template"){
      ++idx;
      auto parts = collectArguments(toks, idx, keywords);
      for(auto &p : parts) opts.templates.push_back(p);
    }else if(token=="Subtask"){
      if(idx+1>=toks.size()){ std::cout<<ansi::RED<<"缺少子任务名称"<<ansi::RESET<<"\n"; return; }
      ToDoSubtask st; st.name = toks[++idx];
      opts.subtasks.push_back(st);
      ++idx;
    }else if(token=="Pre"){
      ++idx;
      auto parts = collectArguments(toks, idx, keywords);
      for(auto &p : parts) opts.predecessors.push_back(p);
    }else if(token=="Post"){
      ++idx;
      auto parts = collectArguments(toks, idx, keywords);
      for(auto &p : parts) opts.successors.push_back(p);
    }else if(token=="Per"){
      if(idx+1>=toks.size()){ std::cout<<ansi::RED<<"缺少周期"<<ansi::RESET<<"\n"; return; }
      opts.periodInput = toks[++idx];
      ++idx;
    }else{
      std::cout<<ansi::RED<<"未知关键字: "<<token<<ansi::RESET<<"\n";
      return;
    }
  }
  std::string err;
  if(!ToDoManager::instance().createTask(opts, err)){
    std::cout<<ansi::RED<<err<<ansi::RESET<<"\n";
    return;
  }
  std::cout<<ansi::CYAN<<"已创建任务 "<<opts.name<<ansi::RESET<<"\n";
}

static void handleUpdata(const std::vector<std::string>& toks){
  if(toks.size()<3){ std::cout<<ansi::RED<<"缺少任务名称"<<ansi::RESET<<"\n"; return; }
  const std::string& name = toks[2];
  if(toks.size()<4){ std::cout<<ansi::RED<<"缺少操作"<<ansi::RESET<<"\n"; return; }
  const std::string& op = toks[3];
  ToDoManager& mgr = ToDoManager::instance();
  std::string err;
  if(op=="Add"){
    if(toks.size()<5){ std::cout<<ansi::RED<<"缺少 ToDo 内容"<<ansi::RESET<<"\n"; return; }
    std::string text = joinTokens(toks, 4);
    if(!mgr.addTodoEntry(name, text, err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n";
    else std::cout<<"已添加 ToDo\n";
    return;
  }
  if(op=="Reset"){
    if(toks.size()<6){ std::cout<<ansi::RED<<"缺少参数"<<ansi::RESET<<"\n"; return; }
    const std::string& target = toks[4];
    const std::string value = joinTokens(toks, 5);
    if(target=="StartTime"){
      if(!mgr.resetStart(name, value, err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n"; else std::cout<<"已更新开始时间\n";
      return;
    }
    if(target=="Deadline"){
      if(!mgr.resetDeadline(name, value, err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n"; else std::cout<<"已更新截止时间\n";
      return;
    }
    std::cout<<ansi::RED<<"未知重置目标"<<ansi::RESET<<"\n";
    return;
  }
  if(op=="Tag" || op=="Category"){
    if(toks.size()<6){ std::cout<<ansi::RED<<"缺少参数"<<ansi::RESET<<"\n"; return; }
    const std::string& action = toks[4];
    for(size_t i=5;i<toks.size();++i){
      if(action=="Add"){
        if(!mgr.addCategory(name, toks[i], err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n";
      }else if(action=="Remove"){
        if(!mgr.removeCategory(name, toks[i], err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n";
      }else{
        std::cout<<ansi::RED<<"未知操作"<<ansi::RESET<<"\n"; return;
      }
    }
    std::cout<<"分类已更新\n";
    return;
  }
  if(op=="Urgency"){
    if(toks.size()<5){ std::cout<<ansi::RED<<"缺少紧急程度"<<ansi::RESET<<"\n"; return; }
    if(!mgr.setUrgency(name, toks[4], err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n"; else std::cout<<"已更新紧急程度\n";
    return;
  }
  if(op=="Progress"){
    if(toks.size()<6){ std::cout<<ansi::RED<<"缺少参数"<<ansi::RESET<<"\n"; return; }
    const std::string& mode = toks[4];
    if(mode=="Percent"){
      double v; if(!parseDouble(toks[5], v)){ std::cout<<ansi::RED<<"百分比格式错误"<<ansi::RESET<<"\n"; return; }
      if(!mgr.setProgressPercent(name, v, err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n"; else std::cout<<"已更新百分比\n";
    }else if(mode=="Step"){
      int v; if(!parseInt(toks[5], v)){ std::cout<<ansi::RED<<"步数格式错误"<<ansi::RESET<<"\n"; return; }
      if(!mgr.setProgressStep(name, v, err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n"; else std::cout<<"已更新步数\n";
    }else{
      std::cout<<ansi::RED<<"未知进度类型"<<ansi::RESET<<"\n";
    }
    return;
  }
  if(op=="Template"){
    if(toks.size()<6){ std::cout<<ansi::RED<<"缺少模板操作"<<ansi::RESET<<"\n"; return; }
    const std::string& action = toks[4];
    for(size_t i=5;i<toks.size();++i){
      if(action=="Apply"){
        if(!mgr.applyTemplate(name, toks[i], err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n";
      }else if(action=="Remove"){
        if(!mgr.removeTemplate(name, toks[i], err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n";
      }
    }
    std::cout<<"模板已更新\n";
    return;
  }
  if(op=="Subtask"){
    if(toks.size()<5){ std::cout<<ansi::RED<<"缺少子任务操作"<<ansi::RESET<<"\n"; return; }
    const std::string& action = toks[4];
    if(action=="Add"){
      if(toks.size()<6){ std::cout<<ansi::RED<<"缺少子任务名称"<<ansi::RESET<<"\n"; return; }
      ToDoSubtask st; st.name = toks[5];
      if(!mgr.addSubtask(name, st, err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n"; else std::cout<<"子任务已添加\n";
      return;
    }
    if(action=="Remove"){
      if(toks.size()<6){ std::cout<<ansi::RED<<"缺少子任务名称"<<ansi::RESET<<"\n"; return; }
      if(!mgr.removeSubtask(name, toks[5], err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n"; else std::cout<<"子任务已移除\n";
      return;
    }
    if(action=="Percent"){
      if(toks.size()<7){ std::cout<<ansi::RED<<"缺少参数"<<ansi::RESET<<"\n"; return; }
      double v; if(!parseDouble(toks[6], v)){ std::cout<<ansi::RED<<"百分比格式错误"<<ansi::RESET<<"\n"; return; }
      if(!mgr.updateSubtaskPercent(name, toks[5], v, err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n"; else std::cout<<"子任务进度已更新\n";
      return;
    }
    if(action=="Step"){
      if(toks.size()<7){ std::cout<<ansi::RED<<"缺少参数"<<ansi::RESET<<"\n"; return; }
      int v; if(!parseInt(toks[6], v)){ std::cout<<ansi::RED<<"步数格式错误"<<ansi::RESET<<"\n"; return; }
      if(!mgr.updateSubtaskStep(name, toks[5], v, err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n"; else std::cout<<"子任务步数已更新\n";
      return;
    }
    std::cout<<ansi::RED<<"未知子任务操作"<<ansi::RESET<<"\n";
    return;
  }
  if(op=="Link"){
    if(toks.size()<6){ std::cout<<ansi::RED<<"缺少参数"<<ansi::RESET<<"\n"; return; }
    const std::string& mode = toks[4];
    for(size_t i=5;i<toks.size();++i){
      if(mode=="Pre"){
        if(!mgr.linkPredecessor(name, toks[i], err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n";
      }else if(mode=="Post"){
        if(!mgr.linkSuccessor(name, toks[i], err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n";
      }else if(mode=="Unpre"){
        if(!mgr.unlinkPredecessor(name, toks[i], err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n";
      }else if(mode=="Unpost"){
        if(!mgr.unlinkSuccessor(name, toks[i], err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n";
      }
    }
    std::cout<<"依赖已更新\n";
    return;
  }
  std::cout<<ansi::RED<<"未知操作"<<ansi::RESET<<"\n";
}

static void handleDelete(const std::vector<std::string>& toks){
  if(toks.size()<3){ std::cout<<ansi::RED<<"缺少任务名称"<<ansi::RESET<<"\n"; return; }
  const std::string& name = toks[2];
  bool whole = (toks.size()>=4 && toks[3]=="per");
  if(!promptConfirm("确认删除" + name + (whole? " (包括周期任务)":""))){
    std::cout<<"已取消\n";
    return;
  }
  bool removed=false; std::string err;
  if(!ToDoManager::instance().deleteTask(name, whole, removed, err)){
    std::cout<<ansi::RED<<err<<ansi::RESET<<"\n";
    return;
  }
  if(removed) std::cout<<"任务已删除\n";
  else std::cout<<"周期任务已进入下一轮\n";
}

static void handleQuery(const std::vector<std::string>& toks){
  ToDoManager& mgr = ToDoManager::instance();
  if(toks.size()==2){
    auto list = mgr.queryUpcoming(std::nullopt);
    printTasksList(list);
    return;
  }
  if(toks.size()>=3 && toks[2]=="Tag"){
    if(toks.size()<4){ std::cout<<ansi::RED<<"缺少分类"<<ansi::RESET<<"\n"; return; }
    auto list = mgr.tasksByCategory(toks[3]);
    printTasksList(list);
    return;
  }
  TimeValueParseResult res = mgr.parseTimeValue(toks[2], true);
  if(!res.ok || !res.hasValue){
    std::string msg = res.error.empty()? std::string("时间格式错误") : res.error;
    std::cout<<ansi::RED<<msg<<ansi::RESET<<"\n";
    return;
  }
  auto list = mgr.queryUpcoming(res.value);
  printTasksList(list);
}

static void handleToday(const std::vector<std::string>& toks){
  bool deadlineOnly = (toks.size()>=3 && toks[2]=="Deadline");
  auto list = ToDoManager::instance().queryToday(deadlineOnly);
  printTasksList(list);
}

static void handleQueryDetail(const std::vector<std::string>& toks){
  if(toks.size()<3){ std::cout<<ansi::RED<<"缺少任务名称"<<ansi::RESET<<"\n"; return; }
  auto task = ToDoManager::instance().findTask(toks[2], true);
  if(!task){ std::cout<<ansi::RED<<"未找到任务"<<ansi::RESET<<"\n"; return; }
  ToDoManager::instance().showDetails(*task);
}

static void handleQueryLast(const std::vector<std::string>& toks){
  if(toks.size()<3){ std::cout<<ansi::RED<<"缺少任务名称"<<ansi::RESET<<"\n"; return; }
  auto last = ToDoManager::instance().lastUpdate(toks[2]);
  if(!last){ std::cout<<ansi::RED<<"无记录"<<ansi::RESET<<"\n"; return; }
  std::cout<<*last<<"\n";
}

static void handleFinished(const std::vector<std::string>& toks){
  auto list = ToDoManager::instance().finishedTasks();
  if(list.empty()){ std::cout<<"暂无已完成任务\n"; return; }
  for(auto &task : list){
    std::cout<<task.name<<" - "<<ToDoManager::joinList(task.categories)<<"\n";
  }
  if(toks.size()>=3 && toks[2]=="clear"){
    if(promptConfirm("确认删除所有已完成任务")){
      std::string err; if(!ToDoManager::instance().clearFinished(err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n"; else std::cout<<"已清空\n";
    }
  }
}

static void handleSetup(const std::vector<std::string>& toks){
  if(toks.size()<3){ std::cout<<ansi::RED<<"缺少目录"<<ansi::RESET<<"\n"; return; }
  std::string err;
  if(!ToDoManager::instance().setupStorage(toks[2], err)) std::cout<<ansi::RED<<err<<ansi::RESET<<"\n";
  else {
    save_settings(settings_file_path());
    std::cout<<"已设置目录\n";
  }
}

static void handleTemplate(const std::vector<std::string>& toks){
  if(toks.size()<3){ std::cout<<ansi::RED<<"缺少模板操作"<<ansi::RESET<<"\n"; return; }
  const std::string& action = toks[2];
  ToDoManager& mgr = ToDoManager::instance();
  if(!mgr.ready()){
    std::cout<<ansi::RED<<"请先使用 Setup 设置目录"<<ansi::RESET<<"\n";
    return;
  }
  if(action=="List"){
    auto names = mgr.templateNames();
    for(auto &n : names) std::cout<<n<<"\n";
    return;
  }
  if(action=="Delete"){
    if(toks.size()<4){ std::cout<<ansi::RED<<"缺少模板名称"<<ansi::RESET<<"\n"; return; }
    auto names = mgr.templateNames();
    if(names.empty()){
      std::cout<<ansi::RED<<"无可删除模板"<<ansi::RESET<<"\n";
      return;
    }
    if(std::find(names.begin(), names.end(), toks[3])==names.end()){
      std::cout<<ansi::RED<<"模板不存在"<<ansi::RESET<<"\n";
      return;
    }
    std::filesystem::path file = std::filesystem::path(mgr.storageDir()) / "Templates" / (todo_detail::fileSafe(toks[3]) + ".tdle");
    if(std::filesystem::exists(file)) std::filesystem::remove(file);
    mgr.initialize();
    std::cout<<"模板已删除\n";
    return;
  }
  if(action=="Show"){
    if(toks.size()<4){ std::cout<<ansi::RED<<"缺少模板名称"<<ansi::RESET<<"\n"; return; }
    auto names = mgr.templateNames();
    if(std::find(names.begin(), names.end(), toks[3])==names.end()){ std::cout<<ansi::RED<<"模板不存在"<<ansi::RESET<<"\n"; return; }
    auto path = std::filesystem::path(mgr.storageDir())/"Templates"/(todo_detail::fileSafe(toks[3]) + ".tdle");
    std::ifstream ifs(path);
    std::string line;
    while(std::getline(ifs,line)) std::cout<<line<<"\n";
    return;
  }
  if(action=="Creat"){
    if(toks.size()<4){ std::cout<<ansi::RED<<"缺少模板名称"<<ansi::RESET<<"\n"; return; }
    ToDoTemplate tpl; tpl.name = toks[3];
    std::set<std::string> keywords = {"Tag","Urgency","ProgressPercent","ProgressStep","Subtask","Pre","Post"};
    size_t idx = 4;
    while(idx < toks.size()){
      const std::string& token = toks[idx];
      if(token=="Tag"){
        ++idx; auto parts = collectArguments(toks, idx, keywords); for(auto &p : parts) tpl.categories.push_back(p);
      }else if(token=="Urgency"){
        if(idx+1>=toks.size()){ std::cout<<ansi::RED<<"缺少紧急程度"<<ansi::RESET<<"\n"; return; }
        tpl.urgency = toks[++idx]; ++idx;
      }else if(token=="ProgressPercent"){
        if(idx+1>=toks.size()){ std::cout<<ansi::RED<<"缺少百分比"<<ansi::RESET<<"\n"; return; }
        tpl.progressPercent = std::stod(toks[++idx]); ++idx;
      }else if(token=="ProgressStep"){
        if(idx+1>=toks.size()){ std::cout<<ansi::RED<<"缺少步数"<<ansi::RESET<<"\n"; return; }
        tpl.progressStep = std::stoi(toks[++idx]); ++idx;
      }else if(token=="Subtask"){
        if(idx+1>=toks.size()){ std::cout<<ansi::RED<<"缺少子任务名称"<<ansi::RESET<<"\n"; return; }
        ToDoSubtask st; st.name = toks[++idx]; tpl.subtasks.push_back(st); ++idx;
      }else if(token=="Pre"){
        ++idx; auto parts = collectArguments(toks, idx, keywords); for(auto &p : parts) tpl.predecessors.push_back(p);
      }else if(token=="Post"){
        ++idx; auto parts = collectArguments(toks, idx, keywords); for(auto &p : parts) tpl.successors.push_back(p);
      }else{
        std::cout<<ansi::RED<<"未知关键字"<<ansi::RESET<<"\n"; return;
      }
    }
    auto names = mgr.templateNames();
    if(std::find(names.begin(), names.end(), tpl.name)!=names.end()){
      std::cout<<ansi::RED<<"模板已存在"<<ansi::RESET<<"\n"; return;
    }
    std::filesystem::path file = std::filesystem::path(mgr.storageDir()) / "Templates" / (todo_detail::fileSafe(tpl.name) + ".tdle");
    std::filesystem::create_directories(file.parent_path());
    std::ofstream ofs(file, std::ios::trunc);
    ofs<<"Name: "<<tpl.name<<"\n";
    if(!tpl.categories.empty()) ofs<<"Categories: "<<ToDoManager::joinList(tpl.categories)<<"\n";
    if(!tpl.urgency.empty()) ofs<<"Urgency: "<<tpl.urgency<<"\n";
    if(tpl.progressPercent>=0) ofs<<"ProgressPercent: "<<todo_detail::percentString(tpl.progressPercent)<<"\n";
    if(tpl.progressStep>=0) ofs<<"ProgressStep: "<<tpl.progressStep<<"\n";
    if(!tpl.predecessors.empty()) ofs<<"Predecessors: "<<ToDoManager::joinList(tpl.predecessors)<<"\n";
    if(!tpl.successors.empty()) ofs<<"Successors: "<<ToDoManager::joinList(tpl.successors)<<"\n";
    if(!tpl.subtasks.empty()){
      ofs<<"Subtasks:\n";
      for(auto &st : tpl.subtasks){ ofs<<"  - "<<st.name<<"\n"; }
    }
    mgr.initialize();
    std::cout<<"模板已创建\n";
    return;
  }
  std::cout<<ansi::RED<<"未知模板操作"<<ansi::RESET<<"\n";
}

ToolSpec make_todo_tool(){
  ToolSpec t; t.name="ToDoListEditor"; t.summary="任务管理器";
  t.subs = {
    SubcommandSpec{"Creat", {}, {"<name>"}, {}, handleCreat},
    SubcommandSpec{"Updata", {}, {"<name>","<...>"}, {}, handleUpdata},
    SubcommandSpec{"Delete", {}, {"<name>"}, {}, handleDelete},
    SubcommandSpec{"Query", {}, {}, {}, handleQuery},
    SubcommandSpec{"Today", {}, {}, {}, handleToday},
    SubcommandSpec{"QueryDetail", {}, {"<name>"}, {}, handleQueryDetail},
    SubcommandSpec{"QueryLast", {}, {"<name>"}, {}, handleQueryLast},
    SubcommandSpec{"Finished", {}, {}, {}, handleFinished},
    SubcommandSpec{"Setup", {}, {"<dir>"}, {}, handleSetup},
    SubcommandSpec{"Template", {}, {}, {}, handleTemplate}
  };
  t.handler = [](const std::vector<std::string>&){ std::cout<<"用法: ToDoListEditor <subcommand>"<<"\n"; };
  return t;
}

static std::string lowerCopy(const std::string& v){
  std::string out = v;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
  return out;
}

static bool tokenEquals(const std::string& a, const std::string& b){
  return lowerCopy(a) == lowerCopy(b);
}

static std::vector<std::string> todoKeywordsAfterName(){
  return {"Add","StartTime","Deadline","Tag","Urgency","ProgressPercent","ProgressStep","Template","Subtask","Pre","Post","Category","Per"};
}

static std::vector<std::string> periodSuggestionList(){
  return {"per d","per 2d","per 3d","per w","per 2w","per m","per y"};
}

static bool isTimeTemplatePlaceholder(char c){
  switch(c){
    case 'y': case 'm': case 'd': case 'a': case 'b': case 'c':
      return true;
    default:
      return false;
  }
}

struct TimeTemplateMatch {
  std::string text;
  std::vector<int> positions;
};

static bool buildTimeTemplateMatch(const std::string& input, const std::string& templ, TimeTemplateMatch& out){
  out.text.clear();
  out.positions.clear();
  size_t inputIndex = 0;
  bool addedLiteral = false;
  for(size_t i=0;i<templ.size();++i){
    char t = templ[i];
    if(isTimeTemplatePlaceholder(t)){
      if(inputIndex < input.size()){
        char ch = input[inputIndex];
        if(!std::isdigit(static_cast<unsigned char>(ch))) return false;
        out.text.push_back(ch);
        out.positions.push_back(static_cast<int>(i));
        ++inputIndex;
        continue;
      }
      break;
    }
    if(inputIndex < input.size()){
      char ch = input[inputIndex];
      if(ch != t) return false;
      out.text.push_back(t);
      out.positions.push_back(static_cast<int>(i));
      ++inputIndex;
      continue;
    }
    out.text.push_back(t);
    out.positions.push_back(static_cast<int>(i));
    addedLiteral = true;
    break;
  }
  if(inputIndex < input.size()) return false;
  if(!addedLiteral && out.text.size() < input.size()) return false;
  if(out.text.empty() && input.empty()){
    // allow showing the template when nothing typed
    return true;
  }
  return true;
}

static std::vector<std::string> relativeTimeSuggestionList(){
  return {"+15m","+30m","+1h","+6h","+12h","+1d","+3d","+7d","+30d"};
}

static std::vector<std::string> sampleTimeValues(){
  std::vector<std::string> values;
  auto addUnique = [&](const std::string& v){
    if(v.empty()) return;
    if(std::find(values.begin(), values.end(), v)==values.end()) values.push_back(v);
  };
  auto addFrom = [&](const std::chrono::system_clock::time_point& tp){
    std::string stamp = ToDoManager::formatTime(tp);
    addUnique(stamp);
    auto space = stamp.find(' ');
    if(space != std::string::npos){
      addUnique(stamp.substr(0, space));
      addUnique(stamp.substr(space+1));
    }
  };
  auto now = std::chrono::system_clock::now();
  addFrom(now);
  addFrom(now + std::chrono::hours(24));
  return values;
}

static Candidates timeCandidates(const std::string& buf){
  Candidates cand;
  auto sw = splitLastWord(buf);
  std::set<std::string> seen;
  auto addCandidate = [&](const std::string& insertion, const std::string& label, const std::vector<int>& positions){
    if(seen.insert(label).second){
      cand.items.push_back(sw.before + insertion);
      cand.labels.push_back(label);
      cand.matchPositions.push_back(positions);
    }
  };

  static const std::vector<std::string> kTemplates = {
    "yyyy.mm.dd aa:bb:cc","yyyy.mm.dd aa:bb","yyyy.mm.dd"
  };
  for(const auto& templ : kTemplates){
    TimeTemplateMatch match;
    if(buildTimeTemplateMatch(sw.word, templ, match)){
      std::string insertion = match.text.empty()? sw.word : match.text;
      addCandidate(insertion, templ, match.positions);
    }
  }

  auto appendList = [&](const std::vector<std::string>& list){
    for(const auto& item : list){
      MatchResult m = compute_match(item, sw.word);
      if(!sw.word.empty() && !m.matched) continue;
      if(sw.word.empty() && !m.matched) m.matched = true;
      if(!m.matched) continue;
      addCandidate(item, item, m.positions);
    }
  };

  appendList(relativeTimeSuggestionList());
  appendList(periodSuggestionList());
  appendList(sampleTimeValues());

  return cand;
}

static Candidates listToCandidates(const std::vector<std::string>& list, const std::string& buf){
  Candidates cand;
  auto sw = splitLastWord(buf);
  for(auto &item : list){
    MatchResult m = compute_match(item, sw.word);
    if(!sw.word.empty() && !m.matched) continue;
    if(sw.word.empty() && !m.matched) m.matched = true;
    if(!m.matched) continue;
    cand.items.push_back(sw.before + item);
    cand.labels.push_back(item);
    cand.matchPositions.push_back(m.positions);
  }
  return cand;
}

Candidates todoCandidates(const ToolSpec& spec, const std::string& buf){
  Candidates empty;
  auto toks = splitTokens(buf);
  auto sw = splitLastWord(buf);
  if(toks.empty()) return empty;
  if(toks.size()==1){
    std::vector<std::string> subs;
    for(auto &s : spec.subs) subs.push_back(s.name);
    return listToCandidates(subs, buf);
  }
  const std::string& cmd = toks[1];
  ToDoManager& mgr = ToDoManager::instance();
  if(tokenEquals(cmd, "Creat")){
    if(toks.size()==2 || (toks.size()==3 && toks.back()==sw.word)){
      return empty;
    }
    if(toks.size()>=3){
      auto keywords = todoKeywordsAfterName();
      auto categories = [&](){
        std::vector<std::string> cats(mgr.categorySet().begin(), mgr.categorySet().end());
        return cats;
      };
      auto tasks = [&](){ return mgr.taskNames(true); };
      auto valueSuggestions = [&](const std::string& key, bool& matched)->Candidates{
        if(tokenEquals(key, "Tag") || tokenEquals(key, "Category")){
          matched = true;
          return listToCandidates(categories(), buf);
        }
        if(tokenEquals(key, "Urgency")){
          matched = true;
          return listToCandidates(kUrgencyLevels, buf);
        }
        if(tokenEquals(key, "Template")){
          matched = true;
          return listToCandidates(mgr.templateNames(), buf);
        }
        if(tokenEquals(key, "StartTime") || tokenEquals(key, "Deadline")){
          matched = true;
          return timeCandidates(buf);
        }
        if(tokenEquals(key, "Per")){
          matched = true;
          return listToCandidates(periodSuggestionList(), buf);
        }
        if(tokenEquals(key, "Pre") || tokenEquals(key, "Post")){
          matched = true;
          return listToCandidates(tasks(), buf);
        }
        matched = false;
        return Candidates{};
      };
      auto keywordList = [&](){ return listToCandidates(keywords, buf); };
      if(sw.word.empty()){
        if(!toks.empty()){
          const std::string& last = toks.back();
          if(tokenEquals(last, "StartTime") || tokenEquals(last, "Deadline") ||
             tokenEquals(last, "Tag") || tokenEquals(last, "Category") ||
             tokenEquals(last, "Urgency") || tokenEquals(last, "Template") ||
             tokenEquals(last, "Per") || tokenEquals(last, "Pre") || tokenEquals(last, "Post")){
            bool matched=false;
            Candidates cand = valueSuggestions(last, matched);
            if(matched) return cand;
          }
        }
        return keywordList();
      }
      if(toks.size()>=4 && toks.back()==sw.word){
        std::string prev = toks[toks.size()-2];
        if(tokenEquals(prev, toks[2])){
          return keywordList();
        }
        bool matched=false;
        Candidates cand = valueSuggestions(prev, matched);
        if(matched) return cand;
      }
      if(toks.size()>=4){
        const std::string& prev = toks.back();
        if(tokenEquals(prev, "StartTime") || tokenEquals(prev, "Deadline") ||
           tokenEquals(prev, "Tag") || tokenEquals(prev, "Category") ||
           tokenEquals(prev, "Urgency") || tokenEquals(prev, "Template") ||
           tokenEquals(prev, "Per") || tokenEquals(prev, "Pre") || tokenEquals(prev, "Post")){
          bool matched=false;
          Candidates cand = valueSuggestions(prev, matched);
          if(matched) return cand;
        }
      }
      return keywordList();
    }
  }else if(tokenEquals(cmd, "Updata")){
    if(toks.size()==2 || (toks.size()==3 && toks.back()==sw.word)){
      return listToCandidates(mgr.taskNames(), buf);
    }
    if(toks.size()>=3){
      auto cats = [&](){ std::vector<std::string> v(mgr.categorySet().begin(), mgr.categorySet().end()); return v; };
      auto topLevel = std::vector<std::string>{"Add","Reset","Tag","Urgency","Progress","Template","Subtask","Link","Category"};
      auto showTopLevel = [&](){ return listToCandidates(topLevel, buf); };
      auto subtaskNames = [&](){
        std::vector<std::string> names;
        auto task = mgr.findTask(toks[2]);
        if(task){ for(auto &st : task->subtasks) names.push_back(st.name); }
        return names;
      };
      auto linkModes = std::vector<std::string>{"Pre","Post","Unpre","Unpost"};
      auto templateActions = std::vector<std::string>{"Apply","Remove"};
      auto progressModes = std::vector<std::string>{"Percent","Step"};
      std::string last = toks.back();
      std::string prev = toks.size()>=2? toks[toks.size()-2] : std::string();
      std::string prev2 = toks.size()>=3? toks[toks.size()-3] : std::string();

      if(sw.word.empty()){
        if(tokenEquals(last, "Tag") || tokenEquals(last, "Category")) return listToCandidates({"Add","Remove"}, buf);
        if((tokenEquals(last, "Add") || tokenEquals(last, "Remove")) && (tokenEquals(prev, "Tag") || tokenEquals(prev, "Category")))
          return listToCandidates(cats(), buf);
        if(tokenEquals(last, "Urgency")) return listToCandidates(kUrgencyLevels, buf);
        if(tokenEquals(last, "Progress")) return listToCandidates(progressModes, buf);
        if(tokenEquals(last, "Reset")) return listToCandidates({"StartTime","Deadline"}, buf);
        if(tokenEquals(last, "Template")) return listToCandidates(templateActions, buf);
        if(tokenEquals(last, "Subtask")) return listToCandidates({"Add","Remove","Percent","Step"}, buf);
        if(tokenEquals(last, "Link")) return listToCandidates(linkModes, buf);
        if((tokenEquals(last, "Apply") || tokenEquals(last, "Remove")) && tokenEquals(prev, "Template"))
          return listToCandidates(mgr.templateNames(), buf);
        if((tokenEquals(last, "Percent") || tokenEquals(last, "Step")) && tokenEquals(prev, "Subtask"))
          return listToCandidates(subtaskNames(), buf);
        if((tokenEquals(last, "Percent") || tokenEquals(last, "Step")) && tokenEquals(prev, "Progress"))
          return Candidates{};
        if(tokenEquals(last, "StartTime") || tokenEquals(last, "Deadline")) return timeCandidates(buf);
        if(tokenEquals(last, "Per")) return listToCandidates(periodSuggestionList(), buf);
        if((tokenEquals(last, "Pre") || tokenEquals(last, "Post") || tokenEquals(last, "Unpre") || tokenEquals(last, "Unpost")) && tokenEquals(prev, "Link"))
          return listToCandidates(mgr.taskNames(true), buf);
        return showTopLevel();
      }

      if(toks.back()==sw.word){
        if(tokenEquals(prev, toks[2])) return showTopLevel();
        if(tokenEquals(prev, "Tag") || tokenEquals(prev, "Category")) return listToCandidates({"Add","Remove"}, buf);
        if((tokenEquals(prev, "Add") || tokenEquals(prev, "Remove")) && (tokenEquals(prev2, "Tag") || tokenEquals(prev2, "Category")))
          return listToCandidates(cats(), buf);
        if(tokenEquals(prev, "Urgency")) return listToCandidates(kUrgencyLevels, buf);
        if(tokenEquals(prev, "Progress")) return listToCandidates(progressModes, buf);
        if(tokenEquals(prev, "Reset")) return listToCandidates({"StartTime","Deadline"}, buf);
        if(tokenEquals(prev, "Template")) return listToCandidates(templateActions, buf);
        if((tokenEquals(prev, "Apply") || tokenEquals(prev, "Remove")) && tokenEquals(prev2, "Template"))
          return listToCandidates(mgr.templateNames(), buf);
        if(tokenEquals(prev, "Subtask")) return listToCandidates({"Add","Remove","Percent","Step"}, buf);
        if((tokenEquals(prev, "Percent") || tokenEquals(prev, "Step")) && tokenEquals(prev2, "Subtask"))
          return listToCandidates(subtaskNames(), buf);
        if((tokenEquals(prev, "Percent") || tokenEquals(prev, "Step")) && tokenEquals(prev2, "Progress"))
          return Candidates{};
        if(tokenEquals(prev, "StartTime") || tokenEquals(prev, "Deadline")) return timeCandidates(buf);
        if(tokenEquals(prev, "Per")) return listToCandidates(periodSuggestionList(), buf);
        if(tokenEquals(prev, "Link")) return listToCandidates(linkModes, buf);
        if((tokenEquals(prev, "Pre") || tokenEquals(prev, "Post") || tokenEquals(prev, "Unpre") || tokenEquals(prev, "Unpost")) && tokenEquals(prev2, "Link"))
          return listToCandidates(mgr.taskNames(true), buf);
      }
    }
  }else if(tokenEquals(cmd, "Delete")){
    if(toks.size()==2 || (toks.size()==3 && toks.back()==sw.word)) return listToCandidates(mgr.taskNames(), buf);
    if(toks.size()>=3 && toks.back()==sw.word) return listToCandidates({"per"}, buf);
    if(sw.word.empty() && !toks.empty() && tokenEquals(toks.back(), "per")) return listToCandidates({"per"}, buf);
  }else if(tokenEquals(cmd, "Query")){
    if(toks.size()==2) return empty;
    if(sw.word.empty() && toks.size()>=3){
      const std::string& last = toks.back();
      if(tokenEquals(last, "Tag")){
        std::vector<std::string> cats(mgr.categorySet().begin(), mgr.categorySet().end());
        return listToCandidates(cats, buf);
      }
    }
    if(toks.size()>=3 && toks.back()==sw.word){
      Candidates base = timeCandidates(buf);
      MatchResult match = compute_match("Tag", sw.word);
      if((sw.word.empty() && !match.matched) || match.matched){
        base.items.push_back(sw.before + "Tag");
        base.labels.push_back("Tag");
        if(match.matched) base.matchPositions.push_back(match.positions);
        else base.matchPositions.push_back({});
      }
      return base;
    }
    if(toks.size()>=4 && tokenEquals(toks[toks.size()-2], "Tag") && toks.back()==sw.word){
      std::vector<std::string> cats(mgr.categorySet().begin(), mgr.categorySet().end());
      return listToCandidates(cats, buf);
    }
    if(sw.word.empty() && toks.size()>=3){
      Candidates base = timeCandidates(buf);
      base.items.push_back(sw.before + "Tag");
      base.labels.push_back("Tag");
      base.matchPositions.push_back({});
      return base;
    }
  }else if(tokenEquals(cmd, "Today")){
    if(toks.size()==3 && toks.back()==sw.word) return listToCandidates({"Deadline"}, buf);
    if(sw.word.empty() && toks.size()>=3 && tokenEquals(toks.back(), "Deadline")) return listToCandidates({"Deadline"}, buf);
  }else if(tokenEquals(cmd, "QueryDetail") || tokenEquals(cmd, "QueryLast")){
    if(toks.size()==2 || (toks.size()==3 && toks.back()==sw.word)) return listToCandidates(mgr.taskNames(true), buf);
    if(sw.word.empty() && toks.size()>=3) return listToCandidates(mgr.taskNames(true), buf);
  }else if(tokenEquals(cmd, "Finished")){
    if(toks.size()==3 && toks.back()==sw.word) return listToCandidates({"clear"}, buf);
    if(sw.word.empty() && toks.size()>=3 && tokenEquals(toks.back(), "clear")) return listToCandidates({"clear"}, buf);
  }else if(tokenEquals(cmd, "Template")){
    if(toks.size()==2 || (toks.size()==3 && toks.back()==sw.word)) return listToCandidates({"Creat","Delete","List","Show"}, buf);
    if(sw.word.empty() && toks.size()>=3 && tokenEquals(toks.back(), "Creat"))
      return listToCandidates({"Tag","Urgency","ProgressPercent","ProgressStep","Subtask","Pre","Post"}, buf);
    if(toks.size()>=4){
      if(tokenEquals(toks[2], "Delete") && toks.back()==sw.word) return listToCandidates(mgr.templateNames(), buf);
      if(tokenEquals(toks[2], "Show") && toks.back()==sw.word) return listToCandidates(mgr.templateNames(), buf);
      if(tokenEquals(toks[2], "Creat") && toks.back()==sw.word){
        return listToCandidates({"Tag","Urgency","ProgressPercent","ProgressStep","Subtask","Pre","Post"}, buf);
      }
    }
  }else if(tokenEquals(cmd, "Setup")){
    if(toks.size()>=3 && toks.back()==sw.word){
      return pathCandidatesForWord(buf, sw.word, PathKind::Dir);
    }
  }
  return empty;
}

std::string todoContextGhost(const ToolSpec& spec, const std::vector<std::string>& toks){
  if(toks.size()==1) return " <subcommand>";
  const std::string& cmd = toks[1];
  if(cmd=="Creat"){
    if(toks.size()==2) return " <name>";
    return " [Add <todo>] [StartTime <time>] [Deadline <time>] ...";
  }
  if(cmd=="Updata"){
    if(toks.size()==2) return " <name>";
  }
  if(cmd=="Delete"){
    if(toks.size()==2) return " <name>";
  }
  if(cmd=="Query"){
    if(toks.size()==2) return " [+time|Tag <tag>]";
  }
  if(cmd=="Today"){
    if(toks.size()==2) return " [Deadline]";
  }
  if(cmd=="QueryDetail" || cmd=="QueryLast"){
    if(toks.size()==2) return " <name>";
  }
  if(cmd=="Finished"){
    if(toks.size()==2) return " [clear]";
  }
  if(cmd=="Template"){
    if(toks.size()==2) return " <Creat|Delete|List|Show>";
  }
  (void)spec;
  return "";
}

