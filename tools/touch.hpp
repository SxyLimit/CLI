#pragma once

#include "tool_common.hpp"

namespace tool {

struct Touch {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "touch";
    spec.summary = "Update timestamps or create files (like Linux touch)";
    set_tool_summary_locale(spec, "en", spec.summary);
    set_tool_summary_locale(spec, "zh", "更新时间戳或创建文件（同 Linux touch）");
    spec.positional = {
      positional("<path>", true, PathKind::Any, {}, true),
      positional("[more paths...]", true, PathKind::Any, {}, true)
    };
    set_tool_help_locale(spec, "en",
                         "touch <path> [more paths...]\n"
                         "Create files when absent; otherwise update their modification time (mirrors Linux touch).\n"
                         "Directories are allowed.");
    set_tool_help_locale(spec, "zh",
                         "touch <路径> [更多路径…]\n"
                         "文件不存在时创建，存在时更新修改时间（与 Linux touch 一致），可作用于目录。");
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 2){
      g_parse_error_cmd = "touch";
      return detail::text_result("usage: touch <path> [more paths...]\n", 1);
    }

    std::vector<std::string> paths(args.begin() + 1, args.end());
    std::ostringstream oss;
    int exitCode = 0;

    for(const auto& pathStr : paths){
      std::filesystem::path target(pathStr);
      std::error_code ec;
      bool exists = std::filesystem::exists(target, ec);
      if(ec){
        exitCode = 1;
        oss << "touch: " << pathStr << ": " << ec.message() << "\n";
        continue;
      }

      if(exists){
        auto now = std::filesystem::file_time_type::clock::now();
        std::filesystem::last_write_time(target, now, ec);
        if(ec){
          exitCode = 1;
          oss << "touch: " << pathStr << ": " << ec.message() << "\n";
          continue;
        }
        oss << "touch: updated " << pathStr << "\n";
        continue;
      }

      if(target.has_parent_path() && !target.parent_path().empty()){
        std::filesystem::path parent = target.parent_path();
        bool parentExists = std::filesystem::exists(parent, ec);
        if(ec){
          exitCode = 1;
          oss << "touch: " << pathStr << ": " << ec.message() << "\n";
          continue;
        }
        if(!parentExists){
          exitCode = 1;
          oss << "touch: " << pathStr << ": No such file or directory\n";
          continue;
        }
      }

      std::ofstream out(target, std::ios::app | std::ios::binary);
      if(!out.good()){
        exitCode = 1;
        oss << "touch: " << pathStr << ": unable to open for writing\n";
        continue;
      }

      auto now = std::filesystem::file_time_type::clock::now();
      std::filesystem::last_write_time(target, now, ec);
      if(ec){
        exitCode = 1;
        oss << "touch: " << pathStr << ": " << ec.message() << "\n";
        continue;
      }

      oss << "touch: created " << pathStr << "\n";
    }

    return detail::text_result(oss.str(), exitCode);
  }
};

inline ToolDefinition make_touch_tool(){
  ToolDefinition def;
  def.ui = Touch::ui();
  def.executor = Touch::run;
  return def;
}

} // namespace tool
