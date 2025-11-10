#pragma once

#include "../globals.hpp"
#include "../settings.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <dirent.h>
#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#define getcwd _getcwd
#endif

std::string shellEscape(const std::string& arg);

namespace tool {
namespace detail {

inline ToolExecutionResult text_result(const std::string& text, int exitCode = 0){
  ToolExecutionResult result;
  result.exitCode = exitCode;
  result.output = text;
  result.display = text;
  return result;
}

inline std::pair<int, std::string> run_command_capture(const std::string& command){
#ifdef _WIN32
  FILE* pipe = _popen(command.c_str(), "rt");
  if(!pipe) return {-1, std::string()};
  std::string output;
  char buffer[512];
  while(fgets(buffer, sizeof(buffer), pipe)){
    output.append(buffer);
  }
  int rc = _pclose(pipe);
  return {rc, output};
#else
  FILE* pipe = popen(command.c_str(), "r");
  if(!pipe) return {-1, std::string()};
  std::string output;
  char buffer[512];
  while(fgets(buffer, sizeof(buffer), pipe)){
    output.append(buffer);
  }
  int rc = pclose(pipe);
  return {rc, output};
#endif
}

inline ToolExecutionResult execute_shell(const ToolExecutionRequest& request,
                                         const std::string& command,
                                         bool captureWhenSilent = true){
  ToolExecutionResult result;
  if(request.silent && captureWhenSilent){
    auto [code, output] = run_command_capture(command);
    result.exitCode = code;
    result.output = output;
    return result;
  }
  int rc = std::system(command.c_str());
  result.exitCode = rc;
  if(request.silent && !captureWhenSilent){
    result.output.clear();
  }
  return result;
}

} // namespace detail
} // namespace tool

