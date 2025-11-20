#pragma once

#include "../tool_common.hpp"
#include "fs_common.hpp"
#include "fs_read.hpp"
#include "fs_write.hpp"
#include "fs_create.hpp"
#include "fs_tree.hpp"
#include "fs_exec.hpp"
#include "../../utils/agent_state.hpp"
#include "../../utils/json.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <optional>
#include <chrono>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <thread>
#include <system_error>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <cerrno>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <random>
#include <algorithm>
#include <utility>

#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#else
#include <windows.h>
#endif

namespace tool {

enum class AgentManualReviewScope {
  None,
  FsShellAndNonFs,
  AllTools
};

struct GuardPromptState {
  std::string id;
  std::string sessionId;
  std::string command;
  std::string reason;
  std::atomic<bool> resolved{false};
  std::atomic<bool> approved{false};
  std::mutex mutex;
  std::condition_variable cv;
};

inline std::string random_guard_prompt_id(){
  static std::mutex rngMutex;
  static std::mt19937_64 rng{std::random_device{}()};
  std::lock_guard<std::mutex> lock(rngMutex);
  std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream oss;
  oss << "guard-" << std::hex << dist(rng);
  return oss.str();
}

static std::mutex g_guard_prompt_mutex;
static std::unordered_map<std::string, std::deque<std::shared_ptr<GuardPromptState>>> g_guard_prompts_by_session;

inline std::shared_ptr<GuardPromptState> register_guard_prompt(const std::string& sessionId,
                                                              const std::string& command,
                                                              const std::string& reason){
  auto prompt = std::make_shared<GuardPromptState>();
  prompt->id = random_guard_prompt_id();
  prompt->sessionId = sessionId;
  prompt->command = command;
  prompt->reason = reason;
  {
    std::lock_guard<std::mutex> lock(g_guard_prompt_mutex);
    g_guard_prompts_by_session[sessionId].push_back(prompt);
  }
  agent_indicator_guard_alert_inc();
  return prompt;
}

inline std::shared_ptr<GuardPromptState> next_guard_prompt_for_session(const std::string& sessionId){
  std::lock_guard<std::mutex> lock(g_guard_prompt_mutex);
  auto it = g_guard_prompts_by_session.find(sessionId);
  if(it == g_guard_prompts_by_session.end()) return nullptr;
  for(const auto& prompt : it->second){
    if(!prompt->resolved.load(std::memory_order_acquire)) return prompt;
  }
  return nullptr;
}

inline void resolve_guard_prompt(const std::shared_ptr<GuardPromptState>& prompt, bool approved){
  if(!prompt) return;
  {
    std::lock_guard<std::mutex> lock(prompt->mutex);
    prompt->approved.store(approved, std::memory_order_release);
    prompt->resolved.store(true, std::memory_order_release);
  }
  prompt->cv.notify_all();
  {
    std::lock_guard<std::mutex> lock(g_guard_prompt_mutex);
    auto it = g_guard_prompts_by_session.find(prompt->sessionId);
    if(it != g_guard_prompts_by_session.end()){
      auto& dq = it->second;
      dq.erase(std::remove(dq.begin(), dq.end(), prompt), dq.end());
      if(dq.empty()) g_guard_prompts_by_session.erase(it);
    }
  }
  agent_indicator_guard_alert_dec();
}

inline bool wait_for_guard_prompt_decision(const std::shared_ptr<GuardPromptState>& prompt){
  if(!prompt) return false;
  std::unique_lock<std::mutex> lock(prompt->mutex);
  prompt->cv.wait(lock, [&]{ return prompt->resolved.load(std::memory_order_acquire); });
  return prompt->approved.load(std::memory_order_acquire);
}

inline std::vector<ToolDefinition> agent_builtin_tools(){
  std::vector<ToolDefinition> defs;
  defs.push_back(tool::make_fs_read_tool());
  defs.push_back(tool::make_fs_write_tool());
  defs.push_back(tool::make_fs_create_tool());
  defs.push_back(tool::make_fs_tree_tool());
  ToolDefinition execShell;
  execShell.ui = tool::FsExecShell::ui();
  execShell.executor = tool::FsExecShell::run;
  defs.push_back(execShell);
  return defs;
}

inline std::string sanitize_property_name(const std::string& raw){
  std::string name = raw;
  if(name.size() >= 2 && ((name.front() == '<' && name.back() == '>') || (name.front() == '[' && name.back() == ']'))){
    name = name.substr(1, name.size() - 2);
  }
  std::string out;
  out.reserve(name.size());
  for(char ch : name){
    if(ch == '.' || ch == ' '){
      out.push_back('_');
    }else if(ch == '-'){
      out.push_back('_');
    }else if(ch == '<' || ch == '>' || ch == '[' || ch == ']' || ch == ':'){
      continue;
    }else{
      out.push_back(ch);
    }
  }
  if(out.empty()) out = "arg";
  return out;
}

inline std::string option_name_to_key(const std::string& option){
  std::string name = option;
  while(!name.empty() && name.front() == '-') name.erase(name.begin());
  for(char& ch : name){
    if(ch == '-' || ch == '.') ch = '_';
  }
  if(name.empty()) name = "option";
  return name;
}

inline sj::Value build_path_metadata(bool isPath, PathKind kind, bool allowDirectory, const std::vector<std::string>& allowed){
  if(!isPath) return sj::Value();
  sj::Object obj;
  switch(kind){
    case PathKind::Any: obj.emplace("kind", sj::Value("any")); break;
    case PathKind::File: obj.emplace("kind", sj::Value("file")); break;
    case PathKind::Dir: obj.emplace("kind", sj::Value("dir")); break;
  }
  obj.emplace("allowDirectory", sj::Value(allowDirectory));
  sj::Array exts;
  for(const auto& ext : allowed){
    exts.push_back(sj::Value(ext));
  }
  obj.emplace("allowedExtensions", sj::Value(std::move(exts)));
  return sj::Value(std::move(obj));
}

inline sj::Value tool_spec_to_catalog(const ToolDefinition& def){
  const ToolSpec& spec = def.ui;
  sj::Object obj;
  obj.emplace("name", sj::Value(spec.name));
  obj.emplace("summary", sj::Value(spec.summary));
  obj.emplace("help", sj::Value(spec.help));
  sj::Object schema;
  schema.emplace("type", sj::Value("object"));
  sj::Object properties;
  sj::Array required;

  std::set<std::string> numericKeys{"max_bytes", "head", "tail", "offset", "length", "depth", "max_entries"};

  for(size_t i = 0; i < spec.positional.size(); ++i){
    const auto& pos = spec.positional[i];
    std::string key = sanitize_property_name(pos.placeholder.empty() ? ("arg" + std::to_string(i+1)) : pos.placeholder);
    sj::Object prop;
    prop.emplace("type", sj::Value("string"));
    prop.emplace("description", sj::Value(pos.placeholder));
    sj::Value meta = build_path_metadata(pos.isPath, pos.pathKind, pos.allowDirectory, pos.allowedExtensions);
    if(meta.type() != sj::Value::Type::Null) prop.emplace("x-path", meta);
    properties.emplace(key, sj::Value(std::move(prop)));
    required.push_back(sj::Value(key));
  }

  for(const auto& opt : spec.options){
    std::string key = option_name_to_key(opt.name);
    sj::Object prop;
    if(!opt.takesValue){
      prop.emplace("type", sj::Value("boolean"));
    }else{
      std::string type = numericKeys.count(key) ? "integer" : "string";
      prop.emplace("type", sj::Value(type));
      if(!opt.placeholder.empty()){
        prop.emplace("description", sj::Value(opt.placeholder));
      }
      if(!opt.valueSuggestions.empty()){
        sj::Array enums;
        for(const auto& val : opt.valueSuggestions){
          enums.push_back(sj::Value(val));
        }
        prop.emplace("enum", sj::Value(std::move(enums)));
      }
      sj::Value meta = build_path_metadata(opt.isPath, opt.pathKind, opt.allowDirectory, opt.allowedExtensions);
      if(meta.type() != sj::Value::Type::Null) prop.emplace("x-path", meta);
    }
    properties.emplace(key, sj::Value(std::move(prop)));
  }

  schema.emplace("properties", sj::Value(std::move(properties)));
  if(!required.empty()) schema.emplace("required", sj::Value(std::move(required)));

  if(spec.name == "fs.write"){
    sj::Array oneOf;
    sj::Object branch1;
    branch1.emplace("required", sj::Value(sj::Array{sj::Value("content")}));
    sj::Object branch2;
    branch2.emplace("required", sj::Value(sj::Array{sj::Value("content_file")}));
    oneOf.push_back(sj::Value(std::move(branch1)));
    oneOf.push_back(sj::Value(std::move(branch2)));
    schema.emplace("oneOf", sj::Value(std::move(oneOf)));
  }

  obj.emplace("args_schema", sj::Value(std::move(schema)));
  return sj::Value(std::move(obj));
}

inline sj::Value build_tool_catalog(){
  sj::Array toolsArray;
  for(const auto& def : agent_builtin_tools()){
    toolsArray.push_back(tool_spec_to_catalog(def));
  }
  sj::Object root;
  root.emplace("tools", sj::Value(std::move(toolsArray)));
  return sj::Value(std::move(root));
}

inline std::string json_line(const sj::Value& value){
  std::string dumped = sj::dump(value);
  dumped.push_back('\n');
  return dumped;
}

inline std::string now_timestamp(){
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  std::ostringstream oss;
  oss << ms;
  return oss.str();
}

struct TranscriptWriter {
  std::ofstream stream;

  bool open(const std::filesystem::path& path){
    stream.open(path, std::ios::out | std::ios::app);
    return stream.good();
  }

  void append(const sj::Value& value){
    if(!stream.good()) return;
    stream << sj::dump(value) << "\n";
    stream.flush();
  }
};

#ifndef _WIN32
struct AgentProcess {
  FILE* in = nullptr;
  FILE* out = nullptr;
  pid_t pid = -1;
};

inline AgentProcess spawn_agent_process(const std::string& executable, const std::vector<std::string>& args){
  int inPipe[2];
  int outPipe[2];
  AgentProcess proc;
  if(pipe(inPipe) != 0) return proc;
  if(pipe(outPipe) != 0){
    close(inPipe[0]);
    close(inPipe[1]);
    return proc;
  }
  pid_t pid = fork();
  if(pid == -1){
    close(inPipe[0]); close(inPipe[1]); close(outPipe[0]); close(outPipe[1]);
    return proc;
  }
  if(pid == 0){
    dup2(inPipe[0], STDIN_FILENO);
    dup2(outPipe[1], STDOUT_FILENO);
    dup2(outPipe[1], STDERR_FILENO);
    close(inPipe[0]); close(inPipe[1]);
    close(outPipe[0]); close(outPipe[1]);
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(executable.c_str()));
    for(const auto& arg : args){
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    execvp(executable.c_str(), argv.data());
    std::exit(1);
  }
  close(inPipe[0]);
  close(outPipe[1]);
  proc.in = fdopen(inPipe[1], "w");
  proc.out = fdopen(outPipe[0], "r");
  proc.pid = pid;
  if(proc.in) setvbuf(proc.in, nullptr, _IONBF, 0);
  if(proc.out) setvbuf(proc.out, nullptr, _IONBF, 0);
  return proc;
}

inline void close_agent_process(AgentProcess& proc){
  if(proc.in){ fclose(proc.in); proc.in = nullptr; }
  if(proc.out){ fclose(proc.out); proc.out = nullptr; }
  if(proc.pid > 0){
    int status = 0;
    waitpid(proc.pid, &status, 0);
    proc.pid = -1;
  }
}

inline bool read_line(FILE* stream, std::string& out){
  out.clear();
  if(!stream) return false;
  char buffer[4096];
  while(true){
    if(!std::fgets(buffer, sizeof(buffer), stream)){
      if(out.empty()) return false;
      break;
    }
    out.append(buffer);
    if(!out.empty() && (out.back() == '\n' || out.back() == '\r')){
      while(!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
      break;
    }
    if(std::feof(stream)) break;
  }
  return true;
}
#else
struct AgentProcess {
  HANDLE inWrite = INVALID_HANDLE_VALUE;
  HANDLE outRead = INVALID_HANDLE_VALUE;
};

inline AgentProcess spawn_agent_process(const std::string&, const std::vector<std::string>&){
  return AgentProcess{};
}

inline void close_agent_process(AgentProcess&){ }

inline bool read_line(void*, std::string&){ return false; }
#endif

inline sj::Value meta_from_result(const ToolExecutionResult& result){
  if(result.metaJson.has_value()){
    try{
      return sj::parse(*result.metaJson);
    }catch(...){
    }
  }
  sj::Object obj;
  return sj::Value(std::move(obj));
}

inline std::string clamp_stdout(const std::string& text, size_t limit, bool& truncated){
  if(text.size() <= limit){
    truncated = false;
    return text;
  }
  truncated = true;
  return text.substr(0, limit);
}

struct AgentSession {
  AgentFsConfig cfg;
  std::string sessionId;
  std::filesystem::path artifactDir;
  TranscriptWriter transcript;
  AgentProcess process;
  size_t stdoutLimit = 4096;
  std::string finalAnswer;
  bool finalReceived = false;
  std::string finalSummary;
  AgentManualReviewScope manualReviewScope = AgentManualReviewScope::None;
  std::string launchMode = "run";

  AgentSession(){
    cfg = default_agent_fs_config();
    sessionId = random_session_id();
    artifactDir = std::filesystem::current_path() / "artifacts" / sessionId;
    std::filesystem::create_directories(artifactDir);
    transcript.open(transcript_path());
  }

  std::string manual_review_policy_name() const {
    switch(manualReviewScope){
      case AgentManualReviewScope::FsShellAndNonFs:
        return "saferun_fs";
      case AgentManualReviewScope::AllTools:
        return "saferun_all";
      default:
        return std::string();
    }
  }

  std::optional<std::string> manual_review_reason(const std::string& toolName) const {
    switch(manualReviewScope){
      case AgentManualReviewScope::None:
        return std::nullopt;
      case AgentManualReviewScope::FsShellAndNonFs:{
        bool isFs = (toolName.rfind("fs.", 0) == 0);
        if(toolName == "fs.exec.shell"){
          return std::string("manual approval required for fs.exec.shell (agent saferun)");
        }
        if(!isFs){
          if(toolName.empty()) return std::string("manual approval required (agent saferun)");
          return std::string("manual approval required for ") + toolName + " (agent saferun)";
        }
        return std::nullopt;
      }
      case AgentManualReviewScope::AllTools:{
        if(toolName.empty()) return std::string("manual approval required (-a) (agent saferun)");
        return std::string("manual approval required (-a) for ") + toolName + " (agent saferun)";
      }
    }
    return std::nullopt;
  }

  template <typename Fn>
  ToolExecutionResult run_with_manual_review(const std::string& command,
                                             const std::string& reason,
                                             Fn&& action){
    auto prompt = register_guard_prompt(sessionId, command, reason);
    sj::Object payload;
    payload.emplace("command", sj::Value(command));
    payload.emplace("reason", sj::Value(reason));
    payload.emplace("prompt_id", sj::Value(prompt->id));
    record_event("guard_blocked", sj::Value(std::move(payload)));
    bool approved = wait_for_guard_prompt_decision(prompt);
    sj::Object decisionPayload;
    decisionPayload.emplace("command", sj::Value(command));
    decisionPayload.emplace("reason", sj::Value(reason));
    decisionPayload.emplace("prompt_id", sj::Value(prompt->id));
    decisionPayload.emplace("approved", sj::Value(approved));
    record_event("guard_decision", sj::Value(std::move(decisionPayload)));
    if(!approved){
      return json_error("command rejected by guard", "guard_rejected");
    }
    return std::forward<Fn>(action)();
  }

  std::filesystem::path transcript_path() const{
    return artifactDir / "transcript.jsonl";
  }

  std::filesystem::path summary_path() const{
    return artifactDir / "summary.txt";
  }

  void mark_latest_session() const{
    std::filesystem::path marker = artifactDir.parent_path() / "latest_agent_session";
    std::error_code ec;
    std::filesystem::create_directories(marker.parent_path(), ec);
    std::ofstream ofs(marker, std::ios::trunc);
    if(!ofs.good()) return;
    ofs << sessionId << "\n";
    ofs << transcript_path().string() << "\n";
  }

  void update_summary(const std::string& text) {
    finalSummary = text;
    std::ofstream ofs(summary_path(), std::ios::trunc);
    if(ofs.good()){
      ofs << text;
      if(!text.empty() && text.back() != '\n') ofs << '\n';
      ofs.close();
    }
  }

  ~AgentSession(){
#ifndef _WIN32
    close_agent_process(process);
#endif
  }

  void record_event(const std::string& kind, const sj::Value& payload){
    sj::Object rec;
    rec.emplace("ts", sj::Value(now_timestamp()));
    rec.emplace("event", sj::Value(kind));
    rec.emplace("data", payload);
    transcript.append(sj::Value(std::move(rec)));
  }

#ifndef _WIN32
  bool start(){
    std::filesystem::path scriptPath = cli_root_directory() / "tools" / "agent" / "agent.py";
    if(!std::filesystem::exists(scriptPath)){
      scriptPath = std::filesystem::path("tools") / "agent" / "agent.py";
    }
    process = spawn_agent_process("python3", {scriptPath.string()});
    if(!process.in || !process.out){
      return false;
    }
    return true;
  }

  bool send_message(const sj::Value& value){
    if(!process.in) return false;
    std::string line = json_line(value);
    if(std::fwrite(line.data(), 1, line.size(), process.in) != line.size()){
      return false;
    }
    std::fflush(process.in);
    return true;
  }

  bool receive_message(std::string& line){
    return read_line(process.out, line);
  }
#endif

  std::vector<std::string> args_to_tokens_fs_read(const sj::Value& args){
    std::vector<std::string> tokens{"fs.read"};
    if(!args.isObject()) return tokens;
    const auto& obj = args.asObject();
    auto it = obj.find("path");
    if(it != obj.end()) tokens.push_back(it->second.asString());
    auto addString = [&](const std::string& key, const std::string& optName){
      auto iter = obj.find(key);
      if(iter != obj.end()){
        tokens.push_back(optName);
        tokens.push_back(iter->second.asString());
      }
    };
    auto addInteger = [&](const std::string& key, const std::string& optName){
      auto iter = obj.find(key);
      if(iter != obj.end()){
        tokens.push_back(optName);
        tokens.push_back(std::to_string(iter->second.asInteger()));
      }
    };
    auto addFlag = [&](const std::string& key, const std::string& optName){
      auto iter = obj.find(key);
      if(iter != obj.end() && iter->second.asBool(false)) tokens.push_back(optName);
    };
    addString("encoding", "--encoding");
    addInteger("max_bytes", "--max-bytes");
    addInteger("head", "--head");
    addInteger("tail", "--tail");
    addInteger("offset", "--offset");
    addInteger("length", "--length");
    addFlag("with_line_numbers", "--with-line-numbers");
    addFlag("hash_only", "--hash-only");
    return tokens;
  }

  std::vector<std::string> args_to_tokens_fs_write(const sj::Value& args){
    std::vector<std::string> tokens{"fs.write"};
    if(!args.isObject()) return tokens;
    const auto& obj = args.asObject();
    auto pathIt = obj.find("path");
    if(pathIt != obj.end()) tokens.push_back(pathIt->second.asString());
    auto contentIt = obj.find("content");
    auto contentFileIt = obj.find("content_file");
    if(contentIt != obj.end()){
      tokens.push_back("--content");
      tokens.push_back(contentIt->second.asString());
    }
    if(contentFileIt != obj.end()){
      tokens.push_back("--content-file");
      tokens.push_back(contentFileIt->second.asString());
    }
    auto addString = [&](const std::string& key, const std::string& optName){
      auto iter = obj.find(key);
      if(iter != obj.end()){
        tokens.push_back(optName);
        tokens.push_back(iter->second.asString());
      }
    };
    addString("mode", "--mode");
    addString("encoding", "--encoding");
    addString("eol", "--eol");
    auto addFlag = [&](const std::string& key, const std::string& optName){
      auto iter = obj.find(key);
      if(iter != obj.end() && iter->second.asBool(false)) tokens.push_back(optName);
    };
    addFlag("create_parents", "--create-parents");
    addFlag("backup", "--backup");
    addFlag("atomic", "--atomic");
    addFlag("dry_run", "--dry-run");
    return tokens;
  }

  std::vector<std::string> args_to_tokens_fs_create(const sj::Value& args){
    std::vector<std::string> tokens{"fs.create"};
    if(!args.isObject()) return tokens;
    const auto& obj = args.asObject();
    auto pathIt = obj.find("path");
    if(pathIt != obj.end()) tokens.push_back(pathIt->second.asString());
    auto addString = [&](const std::string& key, const std::string& optName){
      auto iter = obj.find(key);
      if(iter != obj.end()){
        tokens.push_back(optName);
        tokens.push_back(iter->second.asString());
      }
    };
    auto addFlag = [&](const std::string& key, const std::string& optName){
      auto iter = obj.find(key);
      if(iter != obj.end() && iter->second.asBool(false)) tokens.push_back(optName);
    };
    addString("content", "--content");
    addString("content_file", "--content-file");
    addString("encoding", "--encoding");
    addString("eol", "--eol");
    addFlag("create_parents", "--create-parents");
    addFlag("atomic", "--atomic");
    addFlag("dry_run", "--dry-run");
    return tokens;
  }

  std::vector<std::string> args_to_tokens_fs_tree(const sj::Value& args){
    std::vector<std::string> tokens{"fs.tree"};
    if(!args.isObject()) return tokens;
    const auto& obj = args.asObject();
    auto rootIt = obj.find("root");
    if(rootIt != obj.end()) tokens.push_back(rootIt->second.asString());
    auto addInteger = [&](const std::string& key, const std::string& optName){
      auto iter = obj.find(key);
      if(iter != obj.end()){
        tokens.push_back(optName);
        tokens.push_back(std::to_string(iter->second.asInteger()));
      }
    };
    auto addString = [&](const std::string& key, const std::string& optName){
      auto iter = obj.find(key);
      if(iter != obj.end()){
        tokens.push_back(optName);
        tokens.push_back(iter->second.asString());
      }
    };
    auto addFlag = [&](const std::string& key, const std::string& optName){
      auto iter = obj.find(key);
      if(iter != obj.end() && iter->second.asBool(false)) tokens.push_back(optName);
    };
    addInteger("depth", "--depth");
    addFlag("include_hidden", "--include-hidden");
    addFlag("follow_symlinks", "--follow-symlinks");
    addString("ignore_file", "--ignore-file");
    addString("ext", "--ext");
    addString("format", "--format");
    addInteger("max_entries", "--max-entries");
    return tokens;
  }

  static ToolExecutionResult json_success(sj::Value data){
    sj::Object root;
    root.emplace("ok", sj::Value(true));
    root.emplace("data", std::move(data));
    ToolExecutionResult result;
    result.exitCode = 0;
    result.output = sj::dump(sj::Value(std::move(root)));
    result.display = result.output;
    return result;
  }

  static ToolExecutionResult json_error(const std::string& message,
                                        const std::string& code = "bad_request"){
    sj::Object root;
    root.emplace("ok", sj::Value(false));
    root.emplace("error", sj::Value(message));
    root.emplace("code", sj::Value(code));
    ToolExecutionResult result;
    result.exitCode = 1;
    result.output = sj::dump(sj::Value(std::move(root)));
    result.display = result.output;
    return result;
  }

  ToolExecutionResult invoke_tool(const std::string& name, const sj::Value& args){
    auto run_or_review = [&](const std::string& label, auto&& fn) -> ToolExecutionResult {
      if(auto reason = manual_review_reason(name)){
        return run_with_manual_review(label, *reason, std::forward<decltype(fn)>(fn));
      }
      return fn();
    };

    if(name == "fs.read"){
      return run_or_review(name, [&]() {
        ToolExecutionRequest req;
        req.silent = true;
        req.forLLM = true;
        req.tokens = args_to_tokens_fs_read(args);
        return FsRead::run(req);
      });
    }
    if(name == "fs.write"){
      return run_or_review(name, [&]() {
        ToolExecutionRequest req;
        req.silent = true;
        req.forLLM = true;
        req.tokens = args_to_tokens_fs_write(args);
        return FsWrite::run(req);
      });
    }
    if(name == "fs.create"){
      return run_or_review(name, [&]() {
        ToolExecutionRequest req;
        req.silent = true;
        req.forLLM = true;
        req.tokens = args_to_tokens_fs_create(args);
        return FsCreate::run(req);
      });
    }
    if(name == "fs.tree"){
      return run_or_review(name, [&]() {
        ToolExecutionRequest req;
        req.silent = true;
        req.forLLM = true;
        req.tokens = args_to_tokens_fs_tree(args);
        return FsTree::run(req);
      });
    }
    if(name == "fs.exec.shell"){
      std::string command;
      if(args.isObject()){
        const auto& obj = args.asObject();
        auto it = obj.find("command");
        if(it != obj.end()) command = it->second.asString();
      }
      if(command.empty()){
        return json_error("missing command");
      }
      auto run_command = [&]() -> ToolExecutionResult {
        ToolExecutionRequest execReq;
        execReq.silent = true;
        execReq.forLLM = true;
        auto execRes = tool::detail::execute_shell(execReq, command, true);
        sj::Object data;
        data.emplace("exit_code", sj::Value(execRes.exitCode));
        data.emplace("stdout", sj::Value(execRes.output));
        return json_success(sj::Value(std::move(data)));
      };
      auto guard_review = [&](const std::string& reason){
        return run_with_manual_review(command, reason, run_command);
      };
      auto decision = agent::state().guard.shell_guard(command);
      if(!decision.allowed){
        return guard_review(decision.reason);
      }
      if(auto reason = manual_review_reason(name)){
        return run_with_manual_review(command, *reason, run_command);
      }
      return run_command();
    }
    ToolExecutionResult res;
    res.exitCode = 1;
    res.output = "unknown tool";
    return res;
  }
};

inline std::optional<std::pair<std::string, std::filesystem::path>> load_latest_agent_session_marker(){
  std::filesystem::path marker = std::filesystem::current_path() / "artifacts" / "latest_agent_session";
  std::ifstream in(marker);
  if(!in.good()) return std::nullopt;
  std::string sessionId;
  std::string transcript;
  std::getline(in, sessionId);
  std::getline(in, transcript);
  auto strip_newlines = [](std::string& value){
    while(!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
  };
  strip_newlines(sessionId);
  strip_newlines(transcript);
  std::filesystem::path transcriptPath;
  if(!transcript.empty()){
    transcriptPath = std::filesystem::path(transcript);
  }
  if(sessionId.empty() && !transcriptPath.empty() && transcriptPath.has_parent_path()){
    sessionId = transcriptPath.parent_path().filename().string();
  }
  if(transcriptPath.empty() && !sessionId.empty()){
    transcriptPath = std::filesystem::current_path() / "artifacts" / sessionId / "transcript.jsonl";
  }
  if(transcriptPath.is_relative()){
    transcriptPath = std::filesystem::current_path() / transcriptPath;
  }
  if(sessionId.empty() || transcriptPath.empty()) return std::nullopt;
  return std::make_pair(sessionId, transcriptPath.lexically_normal());
}

inline std::string truncate_summary(const std::string& text, size_t limit){
  auto glyphs = utf8Glyphs(text);
  if(limit <= 3){
    std::string out;
    size_t collected = 0;
    for(const auto& g : glyphs){
      if(collected + 1 > limit) break;
      out += g.bytes;
      collected += 1;
    }
    return out;
  }
  size_t glyphLimit = limit;
  if(glyphs.size() <= glyphLimit) return text;
  if(glyphLimit <= 3) glyphLimit = 3;
  std::string out;
  for(size_t i = 0; i + 3 < glyphLimit && i < glyphs.size(); ++i){
    out += glyphs[i].bytes;
  }
  out += "...";
  return out;
}

struct AgentSessionCompletionEntry {
  std::string sessionId;
  std::string summary;
  std::filesystem::file_time_type updatedAt;
};

inline std::vector<AgentSessionCompletionEntry> agent_session_completion_entries(){
  std::vector<AgentSessionCompletionEntry> entries;
  std::filesystem::path root = std::filesystem::current_path() / "artifacts";
  std::error_code ec;
  if(!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)){
    return entries;
  }
  std::filesystem::directory_iterator it(root, ec);
  std::filesystem::directory_iterator end;
  if(ec) return entries;
  for(; it != end; it.increment(ec)){
    if(ec){
      ec.clear();
      continue;
    }
    const auto& entry = *it;
    if(!entry.is_directory(ec)){
      ec.clear();
      continue;
    }
    std::filesystem::path dir = entry.path();
    std::filesystem::path transcript = dir / "transcript.jsonl";
    if(!std::filesystem::exists(transcript, ec) || !std::filesystem::is_regular_file(transcript, ec)){
      ec.clear();
      continue;
    }
    AgentSessionCompletionEntry info;
    info.sessionId = dir.filename().string();
    info.updatedAt = std::filesystem::file_time_type::min();
    auto stamp = std::filesystem::last_write_time(transcript, ec);
    if(!ec){
      info.updatedAt = stamp;
    }else{
      ec.clear();
    }
    std::filesystem::path summaryPath = dir / "summary.txt";
    std::ifstream summary(summaryPath);
    if(summary.good()){
      std::string line;
      std::getline(summary, line);
      while(!line.empty() && (line.back() == '\r' || line.back() == '\n')){
        line.pop_back();
      }
      info.summary = truncate_summary(line, 80);
    }
    entries.push_back(std::move(info));
  }
  std::sort(entries.begin(), entries.end(), [](const AgentSessionCompletionEntry& a, const AgentSessionCompletionEntry& b){
    if(a.updatedAt != b.updatedAt) return a.updatedAt > b.updatedAt;
    return a.sessionId > b.sessionId;
  });
  return entries;
}

struct AgentCompletion {
  static Candidates complete(const std::string& buffer, const std::vector<std::string>& tokens){
    Candidates cand;
    if(tokens.empty() || tokens[0] != "agent") return cand;

    auto sw = splitLastWord(buffer);
    bool trailingSpace = (!buffer.empty() && std::isspace(static_cast<unsigned char>(buffer.back())));
    static const std::vector<std::string> subs{"run", "saferun", "tools", "monitor"};

    auto addSubcommand = [&](const std::string& sub){
      cand.items.push_back(sw.before + sub);
      cand.labels.push_back(sub);
      cand.matchPositions.push_back({});
      cand.annotations.push_back("");
      cand.exactMatches.push_back(false);
      cand.matchDetails.push_back({});
    };

    if(tokens.size() == 1){
      if(!trailingSpace) return cand;
      for(const auto& sub : subs){
        addSubcommand(sub);
      }
      return cand;
    }

    if(tokens.size() == 2 && !trailingSpace){
      for(const auto& sub : subs){
        MatchResult match = compute_match(sub, sw.word);
        if(!match.matched) continue;
        cand.items.push_back(sw.before + sub);
        cand.labels.push_back(sub);
        cand.matchPositions.push_back(match.positions);
        cand.annotations.push_back("");
        cand.exactMatches.push_back(match.exact);
        cand.matchDetails.push_back(match);
      }
      sortCandidatesByMatch(sw.word, cand);
      return cand;
    }

    if(tokens.size() >= 2 && tokens[1] == "monitor"){
      if(tokens.size() > 3) return cand;
      if(tokens.size() == 3 && trailingSpace) return cand;
      std::string query = sw.word;
      if(tokens.size() == 2 && trailingSpace) query.clear();
      auto entries = agent_session_completion_entries();
      for(const auto& entry : entries){
        if(query.empty()){
          cand.items.push_back(sw.before + entry.sessionId);
          cand.labels.push_back(entry.sessionId);
          cand.matchPositions.push_back({});
          cand.annotations.push_back(entry.summary);
          cand.exactMatches.push_back(false);
          cand.matchDetails.push_back({});
          continue;
        }
        MatchResult match = compute_match(entry.sessionId, query);
        if(!match.matched) continue;
        cand.items.push_back(sw.before + entry.sessionId);
        cand.labels.push_back(entry.sessionId);
        cand.matchPositions.push_back(match.positions);
        cand.annotations.push_back(entry.summary);
        cand.exactMatches.push_back(match.exact);
        cand.matchDetails.push_back(match);
      }
      if(!query.empty()) sortCandidatesByMatch(query, cand);
      return cand;
    }

    return cand;
  }
};

inline std::string summarize_transcript_payload(const std::string& eventKind, const sj::Value& data){
  if(!data.isObject()) return sj::dump(data);
  const auto& obj = data.asObject();
  auto findString = [&](const std::string& key) -> std::string {
    auto it = obj.find(key);
    if(it == obj.end()) return {};
    return it->second.asString();
  };
  auto findBool = [&](const std::string& key, bool def = false) -> bool {
    auto it = obj.find(key);
    if(it == obj.end()) return def;
    return it->second.asBool(def);
  };
  auto findInt = [&](const std::string& key) -> std::optional<long long> {
    auto it = obj.find(key);
    if(it == obj.end()) return std::nullopt;
    return it->second.asInteger();
  };
  if(eventKind == "send" || eventKind == "receive"){
    std::string type = findString("type");
    if(type.empty()) return sj::dump(data);
    if(type == "tool_call"){
      std::string name = findString("name");
      return name.empty()? type : type + " " + name;
    }
    if(type == "tool_result"){
      std::ostringstream oss;
      oss << type;
      if(auto id = findString("id"); !id.empty()) oss << " #" << id;
      oss << (findBool("ok", false) ? " ok" : " error");
      if(auto exitCode = findInt("exit_code")) oss << " exit=" << *exitCode;
      return oss.str();
    }
    if(type == "final"){
      std::string answer = findString("answer");
      if(!answer.empty()) answer = truncate_summary(answer, 80);
      return answer.empty()? type : type + " " + answer;
    }
    if(type == "log"){
      std::string message = findString("message");
      if(!message.empty()) message = truncate_summary(message, 80);
      return message.empty()? type : type + " " + message;
    }
    return type;
  }
  if(eventKind == "parse_error"){
    std::string raw = findString("raw");
    bool looksJson = findBool("looks_json", false);
    if(raw.empty()) return looksJson? std::string("parse_error") : std::string("non-json output");
    std::string prefix = looksJson? std::string("parse_error ") : std::string("stderr ");
    return prefix + truncate_summary(raw, 80);
  }
  if(eventKind == "artifact"){
    std::string name = findString("name");
    std::string path = findString("path");
    std::string detail = name.empty()? std::string("artifact") : std::string("artifact ") + name;
    if(!path.empty()) detail += " -> " + path;
    return detail;
  }
  if(eventKind == "guard_blocked"){
    std::string command = truncate_summary(findString("command"), 120);
    std::string reason = truncate_summary(findString("reason"), 120);
    std::string prompt = findString("prompt_id");
    std::ostringstream oss;
    oss << "guard blocked";
    if(!command.empty()) oss << ": " << command;
    if(!reason.empty()) oss << " (reason: " << reason << ")";
    if(!prompt.empty()) oss << " [prompt " << prompt << "]";
    return oss.str();
  }
  if(eventKind == "guard_decision"){
    bool approved = findBool("approved", false);
    std::string command = truncate_summary(findString("command"), 120);
    std::string prompt = findString("prompt_id");
    std::ostringstream oss;
    oss << "guard " << (approved ? "approved" : "rejected");
    if(!command.empty()) oss << ": " << command;
    if(!prompt.empty()) oss << " [prompt " << prompt << "]";
    return oss.str();
  }
  return sj::dump(data);
}

struct TranscriptSummaryInfo {
  std::string eventKind;
  std::string messageType;
  std::string toolName;
  std::string callId;
  bool toolOk = true;
};

inline std::string summarize_transcript_entry(const std::string& raw,
                                              TranscriptSummaryInfo* outInfo = nullptr){
  try{
    sj::Value value = sj::parse(raw);
    if(!value.isObject()) return raw;
    const auto& obj = value.asObject();
    std::string ts;
    if(auto it = obj.find("ts"); it != obj.end()) ts = it->second.asString();
    std::string eventKind;
    if(auto it = obj.find("event"); it != obj.end()) eventKind = it->second.asString();
    if(outInfo){
      outInfo->eventKind = eventKind;
      outInfo->messageType.clear();
      outInfo->toolName.clear();
      outInfo->callId.clear();
      outInfo->toolOk = true;
    }
    std::string detail;
    if(auto it = obj.find("data"); it != obj.end()) detail = summarize_transcript_payload(eventKind, it->second);
    if(outInfo && obj.find("data") != obj.end()){
      const auto& dataVal = obj.find("data")->second;
      if(dataVal.isObject()){
        const auto& dataObj = dataVal.asObject();
        if(auto typeIt = dataObj.find("type"); typeIt != dataObj.end()){
          outInfo->messageType = typeIt->second.asString();
          if(outInfo->messageType == "tool_call"){
            if(auto idIt = dataObj.find("id"); idIt != dataObj.end()){
              outInfo->callId = idIt->second.asString();
            }
            if(auto nameIt = dataObj.find("name"); nameIt != dataObj.end()){
              outInfo->toolName = nameIt->second.asString();
            }
          }else if(outInfo->messageType == "tool_result"){
            if(auto idIt = dataObj.find("id"); idIt != dataObj.end()){
              outInfo->callId = idIt->second.asString();
            }
            if(auto okIt = dataObj.find("ok"); okIt != dataObj.end()){
              outInfo->toolOk = okIt->second.asBool(true);
            }
          }
        }
      }
    }
    if(detail.empty() && !eventKind.empty()) detail = eventKind;
    std::string prefix;
    if(eventKind == "send") prefix = "->";
    else if(eventKind == "receive") prefix = "<-";
    else prefix = eventKind.empty()? std::string("event") : eventKind;
    std::ostringstream oss;
    if(!ts.empty()) oss << "[" << ts << "] ";
    if(!prefix.empty()) oss << prefix;
    if(!detail.empty()){
      if(!prefix.empty()) oss << ' ';
      oss << detail;
    }
    std::string out = oss.str();
    return truncate_summary(out, 240);
  }catch(...){
    return raw;
  }
}

inline bool resolve_monitor_target(const std::string& requestedSession,
                                   std::string& resolvedSession,
                                   std::filesystem::path& transcriptPath,
                                   std::string& error){
  if(!requestedSession.empty()){
    resolvedSession = requestedSession;
    transcriptPath = std::filesystem::current_path() / "artifacts" / resolvedSession / "transcript.jsonl";
    if(!std::filesystem::exists(transcriptPath)){
      error = std::string("agent monitor: transcript not found for session ") + resolvedSession;
      return false;
    }
    return true;
  }
  auto latest = load_latest_agent_session_marker();
  if(!latest){
    error = "agent monitor: no recorded session available";
    return false;
  }
  resolvedSession = latest->first;
  transcriptPath = latest->second;
  if(!std::filesystem::exists(transcriptPath)){
    error = std::string("agent monitor: transcript missing: ") + transcriptPath.string();
    return false;
  }
  return true;
}

inline ToolExecutionResult monitor_agent_session(const std::string& sessionId,
                                                 const std::filesystem::path& transcriptPath){
#ifndef _WIN32
  ToolExecutionResult result;
  struct MonitorAckGuard {
    bool active = true;
    ~MonitorAckGuard(){ if(active) agent_indicator_mark_acknowledged(); }
  } ackGuard;
  std::ifstream stream(transcriptPath);
  if(!stream.good()){
    g_parse_error_cmd = "agent";
    result.exitCode = 1;
    result.output = "agent monitor: unable to open transcript\n";
    result.display = result.output;
    return result;
  }
  struct MonitorActiveGuard {
    bool active = true;
    ~MonitorActiveGuard(){ if(active) agent_monitor_set_active(false); }
  } activeGuard;
  agent_monitor_set_active(true);
  std::cout << "[agent] monitoring session " << sessionId << " (press q to quit, y/n to respond to guard prompts)" << std::endl;
  std::unordered_map<std::string, std::string> toolByCallId;
  auto tool_color = [&](const std::string& toolName) -> const char* {
    if(toolName.rfind("fs.exec", 0) == 0) return ansi::RED;
    if(toolName.rfind("fs.write", 0) == 0 || toolName.rfind("fs.create", 0) == 0) return ansi::YELLOW;
    if(toolName.rfind("fs.todo", 0) == 0) return ansi::GREEN;
    if(toolName.rfind("fs.ctx", 0) == 0) return ansi::CYAN;
    if(toolName.rfind("fs.tree", 0) == 0) return ansi::CYAN;
    if(toolName.rfind("fs.read", 0) == 0) return ansi::CYAN;
    return ansi::WHITE;
  };
  auto emit = [&](const std::string& raw){
    TranscriptSummaryInfo summary;
    std::string line = summarize_transcript_entry(raw, &summary);
    const char* color = nullptr;
    if(summary.eventKind == "guard_blocked"){
      color = ansi::RED;
    }else if(summary.eventKind == "guard_decision"){
      if(line.find("rejected") != std::string::npos) color = ansi::RED;
      else color = ansi::GREEN;
    }else if(summary.messageType == "tool_call"){
      if(!summary.callId.empty() && !summary.toolName.empty()){
        toolByCallId[summary.callId] = summary.toolName;
      }
      if(!summary.toolName.empty()){
        color = tool_color(summary.toolName);
      }
    }else if(summary.messageType == "tool_result"){
      std::string toolName;
      if(!summary.callId.empty()){
        auto it = toolByCallId.find(summary.callId);
        if(it != toolByCallId.end()){
          toolName = it->second;
          toolByCallId.erase(it);
        }
      }
      if(!toolName.empty()){
        color = summary.toolOk ? tool_color(toolName) : ansi::RED;
      }else{
        color = summary.toolOk ? ansi::GREEN : ansi::RED;
      }
    }else if(summary.messageType == "final"){
      color = ansi::CYAN;
    }else if(summary.messageType == "log"){
      color = ansi::GRAY;
    }else if(summary.eventKind == "parse_error"){
      color = ansi::RED;
    }
    if(color){
      std::cout << color << line << ansi::RESET << std::endl;
    }else{
      std::cout << line << std::endl;
    }
  };
  std::shared_ptr<GuardPromptState> currentPrompt;
  std::string shownPromptId;
  auto show_prompt = [&](){
    if(!currentPrompt || currentPrompt->resolved.load(std::memory_order_acquire)) return;
    if(shownPromptId == currentPrompt->id) return;
    shownPromptId = currentPrompt->id;
    std::string command = truncate_summary(currentPrompt->command, 200);
    std::string reason = truncate_summary(currentPrompt->reason, 200);
    std::cout << ansi::RED << "[guard] Command blocked";
    if(!command.empty()) std::cout << ": " << command;
    std::cout << ansi::RESET << std::endl;
    if(!reason.empty()){
      std::cout << ansi::RED << "[guard] Reason: " << reason << ansi::RESET << std::endl;
    }
    std::cout << ansi::YELLOW << "Press y to approve or n to reject this command." << ansi::RESET << std::endl;
  };
  std::string line;
  std::streampos lastPos = stream.tellg();
  if(lastPos == std::streampos(-1)){
    lastPos = std::streampos(0);
  }
  auto drain_new_entries = [&](){
    stream.clear();
    stream.seekg(lastPos);
    if(!stream.good()){
      stream.clear();
      stream.seekg(0, std::ios::beg);
      auto resetPos = stream.tellg();
      if(resetPos == std::streampos(-1)){
        resetPos = std::streampos(0);
      }
      lastPos = resetPos;
    }
    while(std::getline(stream, line)){
      emit(line);
      auto pos = stream.tellg();
      if(pos == std::streampos(-1)){
        lastPos = std::streampos(0);
      }else{
        lastPos = pos;
      }
    }
    if(stream.bad()){
      stream.clear();
    }
    auto nextPrompt = next_guard_prompt_for_session(sessionId);
    if(nextPrompt && nextPrompt->resolved.load(std::memory_order_acquire)){
      nextPrompt.reset();
    }
    currentPrompt = nextPrompt;
    if(currentPrompt && currentPrompt->resolved.load(std::memory_order_acquire)){
      currentPrompt.reset();
    }
    if(!currentPrompt){
      shownPromptId.clear();
    }
    show_prompt();
  };
  drain_new_entries();
  bool running = true;
  while(running){
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    int ready = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv);
    if(ready > 0 && FD_ISSET(STDIN_FILENO, &readfds)){
      char ch = 0;
      ssize_t rc = ::read(STDIN_FILENO, &ch, 1);
      if(rc > 0){
        if(ch == 'q' || ch == 'Q'){
          running = false;
          break;
        }
        if(currentPrompt && !currentPrompt->resolved.load(std::memory_order_acquire)){
          if(ch == 'y' || ch == 'Y'){
            resolve_guard_prompt(currentPrompt, true);
            currentPrompt.reset();
            shownPromptId.clear();
            std::cout << ansi::YELLOW << "[guard] override approved" << ansi::RESET << std::endl;
            drain_new_entries();
          }else if(ch == 'n' || ch == 'N'){
            resolve_guard_prompt(currentPrompt, false);
            currentPrompt.reset();
            shownPromptId.clear();
            std::cout << ansi::YELLOW << "[guard] override rejected" << ansi::RESET << std::endl;
            drain_new_entries();
          }
        }
      }
    }else if(ready < 0){
      if(errno == EINTR) continue;
      std::cout << "[agent] monitor stopped (select failed)" << std::endl;
      running = false;
      break;
    }
    drain_new_entries();
  }
  std::cout << "[agent] monitor stopped" << std::endl;
  return result;
#else
  g_parse_error_cmd = "agent";
  return detail::text_result("agent monitor is not supported on this platform\n", 1);
#endif
}

inline void agent_session_thread_main(std::shared_ptr<AgentSession> session, std::string goal){
#ifndef _WIN32
  struct IndicatorGuard {
    bool active = true;
    void finish(){
      if(active){
        active = false;
        agent_indicator_set_finished();
      }
    }
    ~IndicatorGuard(){ finish(); }
  } indicatorGuard;

  bool summaryWritten = false;
  auto record_summary = [&](const std::string& summary){
    if(summaryWritten) return;
    summaryWritten = true;
    session->update_summary(summary);
    sj::Object data;
    data.emplace("text", sj::Value(summary));
    session->record_event("summary", sj::Value(std::move(data)));
  };

  auto finalize_summary = [&](){
    if(summaryWritten) return;
    if(session->finalReceived && !session->finalAnswer.empty()){
      record_summary(session->finalAnswer);
    }else if(session->finalReceived){
      record_summary("Agent session finished without an answer.");
    }else{
      record_summary("Agent session ended without a final message.");
    }
  };

  auto record_error = [&](const std::string& message){
    sj::Object err;
    err.emplace("message", sj::Value(message));
    session->record_event("error", sj::Value(std::move(err)));
    record_summary(message);
  };

  try{
    sj::Value catalog = build_tool_catalog();
    sj::Object hello;
    hello.emplace("type", sj::Value("hello"));
    hello.emplace("version", sj::Value("1.0"));
    hello.emplace("tool_catalog", catalog);
    sj::Object limits;
    limits.emplace("stdout_bytes", sj::Value(static_cast<long long>(session->stdoutLimit)));
    limits.emplace("tool_timeout_ms", sj::Value(static_cast<long long>(session->cfg.toolTimeoutMs)));
    hello.emplace("limits", sj::Value(std::move(limits)));
    sj::Object policy;
    sj::Array allowed;
    allowed.push_back(sj::Value("fs.read"));
    allowed.push_back(sj::Value("fs.write"));
    allowed.push_back(sj::Value("fs.create"));
    allowed.push_back(sj::Value("fs.tree"));
    allowed.push_back(sj::Value("fs.exec.shell"));
    policy.emplace("allowed_tools", sj::Value(std::move(allowed)));
    policy.emplace("sandbox_root", sj::Value(session->cfg.sandboxRoot.string()));
    auto reviewLabel = session->manual_review_policy_name();
    if(!reviewLabel.empty()){
      policy.emplace("manual_review", sj::Value(reviewLabel));
    }
    hello.emplace("policy", sj::Value(std::move(policy)));
    sj::Value helloVal(std::move(hello));
    session->record_event("send", helloVal);
    if(!session->send_message(helloVal)){
      record_error("Failed to send hello message to agent process.");
      indicatorGuard.finish();
      return;
    }

    sj::Object start;
    start.emplace("type", sj::Value("start"));
    start.emplace("goal", sj::Value(goal));
    sj::Object context;
    context.emplace("cwd", sj::Value(std::filesystem::current_path().string()));
    start.emplace("context", sj::Value(std::move(context)));
    sj::Value startVal(std::move(start));
    session->record_event("send", startVal);
    if(!session->send_message(startVal)){
      record_error("Failed to send start message to agent process.");
      indicatorGuard.finish();
      return;
    }

    std::string line;
    bool running = true;
    while(running && session->receive_message(line)){
      if(line.empty()) continue;
      sj::Value msg;
      bool parsed = false;
      try{
        msg = sj::parse(line);
        parsed = true;
      }catch(const std::exception&){
      }
      if(!parsed){
        bool looksJson = !line.empty() && (line.front() == '{' || line.front() == '[');
        sj::Object payload;
        payload.emplace("raw", sj::Value(line));
        payload.emplace("looks_json", sj::Value(looksJson));
        session->record_event("parse_error", sj::Value(std::move(payload)));
        if(looksJson){
          sj::Object err;
          err.emplace("type", sj::Value("error"));
          err.emplace("message", sj::Value("invalid json"));
          sj::Value errVal(std::move(err));
          session->record_event("send", errVal);
          session->send_message(errVal);
        }
        continue;
      }
      session->record_event("receive", msg);
      const auto* typeField = msg.find("type");
      if(!typeField) continue;
      std::string type = typeField->asString();
      if(type == "tool_call"){
        const auto* idField = msg.find("id");
        const auto* nameField = msg.find("name");
        const auto* argsField = msg.find("args");
        std::string callId = idField ? idField->asString() : "";
        std::string toolName = nameField ? nameField->asString() : "";
        sj::Value args = argsField ? *argsField : sj::Value();
        ToolExecutionResult res = session->invoke_tool(toolName, args);
        sj::Object reply;
        reply.emplace("type", sj::Value("tool_result"));
        reply.emplace("id", sj::Value(callId));
        bool truncated = false;
        std::string stdoutLimited = clamp_stdout(res.output, session->stdoutLimit, truncated);
        reply.emplace("ok", sj::Value(res.exitCode == 0));
        reply.emplace("exit_code", sj::Value(res.exitCode));
        reply.emplace("stdout", sj::Value(stdoutLimited));
        reply.emplace("stderr", sj::Value(res.stderrOutput.value_or("")));
        sj::Value meta = meta_from_result(res);
        if(meta.type() == sj::Value::Type::Object){
          sj::Object metaObj = meta.asObject();
          metaObj.emplace("stdout_truncated", sj::Value(truncated));
          reply.emplace("meta", sj::Value(std::move(metaObj)));
        }else{
          reply.emplace("meta", meta);
        }
        sj::Value replyVal(std::move(reply));
        session->record_event("send", replyVal);
        if(!session->send_message(replyVal)){
          record_error("Failed to send tool_result to agent process.");
          running = false;
        }
      }else if(type == "log"){
        // Logs are captured in the transcript; no realtime console output.
      }else if(type == "final"){
        const auto* ans = msg.find("answer");
        if(ans){
          session->finalAnswer = ans->asString();
        }
        const auto* artifactsField = msg.find("artifacts");
        if(artifactsField && artifactsField->isArray()){
          const auto& arr = artifactsField->asArray();
          for(const auto& item : arr){
            if(!item.isObject()) continue;
            const auto* nameField = item.find("name");
            const auto* contentField = item.find("content");
            if(!nameField || !contentField) continue;
            std::string name = nameField->asString();
            std::string safeName = name;
            for(char& ch : safeName){
              if(ch == '/' || ch == '\\') ch = '_';
            }
            if(safeName.empty()) safeName = "artifact";
            std::filesystem::path path = session->artifactDir / safeName;
            std::ofstream ofs(path, std::ios::binary);
            if(ofs){
              ofs << contentField->asString();
              ofs.close();
              sj::Object rec;
              rec.emplace("type", sj::Value("artifact"));
              rec.emplace("name", sj::Value(name));
              rec.emplace("path", sj::Value(path.string()));
              session->record_event("artifact", sj::Value(std::move(rec)));
            }
          }
        }
        session->finalReceived = true;
        if(!session->finalAnswer.empty()){
          record_summary(session->finalAnswer);
        }
        running = false;
      }
    }

    if(running){
      session->record_event("status", sj::make_object({{"state", sj::Value("helper_disconnected")}}));
    }
  }catch(const std::exception& ex){
    record_error(std::string("Agent worker exception: ") + ex.what());
    indicatorGuard.finish();
    return;
  }catch(...){
    record_error("Agent worker exception: unknown error");
    indicatorGuard.finish();
    return;
  }

  finalize_summary();
  indicatorGuard.finish();
#else
  (void)session;
  (void)goal;
#endif
}

inline ToolExecutionResult launch_agent_session(const std::string& goal,
                                                AgentManualReviewScope reviewScope,
                                                const std::string& modeLabel){
#ifndef _WIN32
  auto session = std::make_shared<AgentSession>();
  session->manualReviewScope = reviewScope;
  session->launchMode = modeLabel;
  if(!session->start()){
    g_parse_error_cmd = "agent";
    return detail::text_result("agent: failed to start Python helper\n", 1);
  }
  session->mark_latest_session();
  session->update_summary("Agent session is running.");
  session->record_event("status", sj::make_object({
    {"state", sj::Value("dispatched")},
    {"goal", sj::Value(goal)},
    {"mode", sj::Value(session->launchMode)}
  }));
  try{
    agent_indicator_set_running();
    std::thread([session, goal]{ agent_session_thread_main(session, goal); }).detach();
  }catch(const std::system_error& ex){
    agent_indicator_set_finished();
    agent_indicator_mark_acknowledged();
    session->update_summary(std::string("Agent dispatch failed: ") + ex.what());
    session->record_event("summary", sj::make_object({{"text", sj::Value(std::string("Agent dispatch failed: ") + ex.what())}}));
    session->record_event("error", sj::make_object({{"message", sj::Value(std::string("thread dispatch failed: ") + ex.what())}}));
    g_parse_error_cmd = "agent";
    return detail::text_result(std::string("agent: failed to dispatch worker thread: ") + ex.what() + "\n", 1);
  }

  ToolExecutionResult out;
  out.exitCode = 0;
  std::ostringstream oss;
  oss << "[agent] session " << session->sessionId << " started asynchronously." << "\n";
  oss << "use `agent monitor` to follow progress (latest session by default)." << "\n";
  oss << "transcript: " << session->transcript_path() << "\n";
  oss << "summary: " << session->summary_path() << "\n";
  out.output = oss.str();
  sj::Object meta;
  meta.emplace("session_id", sj::Value(session->sessionId));
  meta.emplace("transcript", sj::Value(session->transcript_path().string()));
  meta.emplace("summary", sj::Value(session->summary_path().string()));
  meta.emplace("duration_ms", sj::Value(0));
  out.metaJson = sj::dump(sj::Value(std::move(meta)));
  return out;
#else
  g_parse_error_cmd = "agent";
  return detail::text_result("agent: not supported on this platform\n", 1);
#endif
}

struct AgentTool {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "agent";
    spec.summary = "Run sandboxed automation agent";
    set_tool_summary_locale(spec, "en", "Run sandboxed automation agent");
    set_tool_summary_locale(spec, "zh", "运行沙盒内的自动化 Agent");
    spec.help = "agent run <goal...> | agent saferun [-a] <todo...> | agent tools --json | agent monitor [session_id]";
    set_tool_help_locale(spec, "en", "agent run <goal...> | agent saferun [-a] <todo...> | agent tools --json | agent monitor [session_id]");
    set_tool_help_locale(spec, "zh", "agent run <目标...> | agent saferun [-a] <待办...> | agent tools --json | agent monitor [session_id]");
    spec.subs = {
      SubcommandSpec{"run", {}, {positional("<goal...>")}, {}, nullptr},
      SubcommandSpec{"saferun",
        {OptionSpec{"-a", false}, OptionSpec{"--all", false}},
        {positional("<todo...>")},
        {}, nullptr},
      SubcommandSpec{"tools", {OptionSpec{"--json", false}}, {}, {}, nullptr},
      SubcommandSpec{"monitor", {}, {positional("[session_id]")}, {}, nullptr}
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& tokens = request.tokens;
    if(tokens.size() < 2){
      g_parse_error_cmd = "agent";
      return detail::text_result("usage: agent <run|tools> ...\n", 1);
    }
    if(tokens[1] == "tools"){
      bool jsonFlag = false;
      for(size_t i = 2; i < tokens.size(); ++i){
        if(tokens[i] == "--json") jsonFlag = true;
      }
      if(!jsonFlag){
        g_parse_error_cmd = "agent";
        return detail::text_result("usage: agent tools --json\n", 1);
      }
      sj::Value catalog = build_tool_catalog();
      ToolExecutionResult out;
      out.exitCode = 0;
      out.output = sj::dump(catalog, 2) + "\n";
      out.metaJson = sj::dump(sj::make_object({{"duration_ms", sj::Value(0)}}));
      return out;
    }
    if(tokens[1] == "monitor"){
#ifndef _WIN32
      if(tokens.size() > 3){
        g_parse_error_cmd = "agent";
        return detail::text_result("usage: agent monitor [session_id]\n", 1);
      }
      std::string requestedId;
      if(tokens.size() == 3) requestedId = tokens[2];
      std::string resolvedId;
      std::filesystem::path transcriptPath;
      std::string error;
      if(!resolve_monitor_target(requestedId, resolvedId, transcriptPath, error)){
        g_parse_error_cmd = "agent";
        return detail::text_result(error + "\n", 1);
      }
      return monitor_agent_session(resolvedId, transcriptPath);
#else
      g_parse_error_cmd = "agent";
      return detail::text_result("agent monitor is not supported on this platform\n", 1);
#endif
    }
    if(tokens[1] == "saferun"){
      bool auditAll = false;
      std::vector<std::string> todoTokens;
      for(size_t i = 2; i < tokens.size(); ++i){
        if(tokens[i] == "-a" || tokens[i] == "--all"){
          auditAll = true;
          continue;
        }
        todoTokens.push_back(tokens[i]);
      }
      if(todoTokens.empty()){
        g_parse_error_cmd = "agent";
        return detail::text_result("usage: agent saferun [-a] <todo...>\n", 1);
      }
      std::string todo;
      for(size_t i = 0; i < todoTokens.size(); ++i){
        if(i) todo.push_back(' ');
        todo += todoTokens[i];
      }
      return launch_agent_session(todo,
                                  auditAll ? AgentManualReviewScope::AllTools : AgentManualReviewScope::FsShellAndNonFs,
                                  auditAll ? "saferun_all" : "saferun");
    }
    if(tokens[1] != "run"){
      g_parse_error_cmd = "agent";
      return detail::text_result("usage: agent <run|tools> ...\n", 1);
    }
    if(tokens.size() < 3){
      g_parse_error_cmd = "agent";
      return detail::text_result("usage: agent run <goal...>\n", 1);
    }
    std::string goal;
    for(size_t i = 2; i < tokens.size(); ++i){
      if(i > 2) goal.push_back(' ');
      goal += tokens[i];
    }
    return launch_agent_session(goal, AgentManualReviewScope::None, "run");
  }
};

inline ToolDefinition make_agent_tool(){
  ToolDefinition def;
  def.ui = AgentTool::ui();
  def.executor = AgentTool::run;
  def.completion = AgentCompletion::complete;
  return def;
}

} // namespace tool

