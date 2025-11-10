#pragma once

#include "tool_common.hpp"

namespace tool {

struct Ls {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "ls";
    spec.summary = "List directory (simple)";
    set_tool_summary_locale(spec, "en", "List directory (simple)");
    set_tool_summary_locale(spec, "zh", "列出目录（简化版）");
    spec.options = {
      {"-a", false, {}, nullptr, false, "", false},
      {"-l", false, {}, nullptr, false, "", false}
    };
    spec.positional = {"[<dir>]"};
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    bool showDot = false;
    bool longFmt = false;
    std::string path = ".";
    for(size_t i = 1; i < args.size(); ++i){
      if(args[i] == "-a") showDot = true;
      else if(args[i] == "-l") longFmt = true;
      else if(!args[i].empty() && args[i][0] == '-'){
        g_parse_error_cmd = "ls";
        return detail::text_result("unknown option: " + args[i] + "\n", 1);
      }else{
        path = args[i];
      }
    }
    DIR* dir = ::opendir(path.c_str());
    if(!dir){
      std::ostringstream oss;
      oss << "ls: " << path << ": " << std::strerror(errno) << "\n";
      g_parse_error_cmd = "ls";
      return detail::text_result(oss.str(), 1);
    }
    std::vector<std::string> names;
    while(dirent* entry = ::readdir(dir)){
      std::string name = entry->d_name;
      if(!showDot && !name.empty() && name[0] == '.') continue;
      bool isDir = isDirFS(path + (path.back() == '/' ? "" : "/") + name);
      if(isDir) name.push_back('/');
      names.push_back(std::move(name));
    }
    ::closedir(dir);
    std::sort(names.begin(), names.end());
    std::ostringstream oss;
    if(longFmt){
      for(const auto& name : names){
        oss << name << "\n";
      }
    }else{
      for(const auto& name : names){
        oss << name << "\n";
      }
    }
    return detail::text_result(oss.str());
  }
};

inline ToolDefinition make_ls_tool(){
  ToolDefinition def;
  def.ui = Ls::ui();
  def.executor = Ls::run;
  return def;
}

} // namespace tool

