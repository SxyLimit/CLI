#pragma once

#include "tool_common.hpp"

namespace tool {

struct Mkdir {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "mkdir";
    spec.summary = "Create directories (like Linux mkdir)";
    set_tool_summary_locale(spec, "en", spec.summary);
    set_tool_summary_locale(spec, "zh", "创建目录（同 Linux mkdir）");
    spec.options = {
      OptionSpec{"--parents", false},
      OptionSpec{"-p", false}
    };
    spec.positional = {
      positional("<path>", true, PathKind::Dir, {}, true),
      positional("[more paths...]", true, PathKind::Dir, {}, true)
    };
    set_tool_help_locale(spec, "en",
                         "mkdir [--parents|-p] <path> [more paths...]\n"
                         "Create directories; with -p, existing directories are accepted and parents are created.");
    set_tool_help_locale(spec, "zh",
                         "mkdir [--parents|-p] <路径> [更多路径…]\n"
                         "创建目录；使用 -p 可自动创建父目录，并允许目录已存在。");
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 2){
      g_parse_error_cmd = "mkdir";
      return detail::text_result("usage: mkdir [--parents|-p] <path> [more paths...]\n", 1);
    }

    bool parents = false;
    std::vector<std::string> paths;
    for(size_t i = 1; i < args.size(); ++i){
      const auto& tok = args[i];
      if(tok == "--parents" || tok == "-p"){
        parents = true;
      }else{
        paths.push_back(tok);
      }
    }

    if(paths.empty()){
      g_parse_error_cmd = "mkdir";
      return detail::text_result("usage: mkdir [--parents|-p] <path> [more paths...]\n", 1);
    }

    std::ostringstream oss;
    int exitCode = 0;
    for(const auto& pathStr : paths){
      std::error_code ec;
      if(parents){
        bool created = std::filesystem::create_directories(pathStr, ec);
        if(ec){
          exitCode = 1;
          oss << "mkdir: " << pathStr << ": " << ec.message() << "\n";
          continue;
        }
        if(created){
          oss << "mkdir: " << pathStr << " created\n";
        }else{
          oss << "mkdir: " << pathStr << " already exists\n";
        }
      }else{
        std::filesystem::create_directory(pathStr, ec);
        if(ec){
          exitCode = 1;
          oss << "mkdir: " << pathStr << ": " << ec.message() << "\n";
          continue;
        }
        oss << "mkdir: " << pathStr << " created\n";
      }
    }

    return detail::text_result(oss.str(), exitCode);
  }
};

inline ToolDefinition make_mkdir_tool(){
  ToolDefinition def;
  def.ui = Mkdir::ui();
  def.executor = Mkdir::run;
  return def;
}

} // namespace tool
