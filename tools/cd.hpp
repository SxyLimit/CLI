#pragma once

#include "tool_common.hpp"

namespace tool {

struct Cd {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "cd";
    spec.summary = "Change directory";
    set_tool_summary_locale(spec, "en", "Change directory");
    set_tool_summary_locale(spec, "zh", "切换目录");
    spec.positional = {positional("<dir>", true, PathKind::Dir, {}, true, false)};
    set_tool_help_locale(spec, "en", "cd <path> | cd -o [-a|-c]");
    set_tool_help_locale(spec, "zh", "cd <路径> | cd -o [-a|-c]");
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    bool flagO = false;
    bool flagA = false;
    bool flagC = false;
    std::vector<std::string> rest;
    for(size_t i = 1; i < args.size(); ++i){
      if(args[i] == "-o") flagO = true;
      else if(args[i] == "-a") flagA = true;
      else if(args[i] == "-c") flagC = true;
      else rest.push_back(args[i]);
    }
    if(flagO){
      std::string mode = "omit";
      if(flagA) mode = "hidden";
      else if(flagC) mode = "full";
      std::string error;
      if(settings_set_value("prompt.cwd", mode, error)){
        save_settings(settings_file_path());
        std::string modeLabel = tr(std::string("mode.") + mode);
        return detail::text_result(trFmt("cd_mode_updated", {{"mode", modeLabel}}) + "\n");
      }
      g_parse_error_cmd = "cd";
      return detail::text_result(tr("cd_mode_error") + "\n", 1);
    }
    if(flagA || flagC){
      g_parse_error_cmd = "cd";
      return detail::text_result(tr("cd_usage") + "\n", 1);
    }
    if(rest.empty()){
      g_parse_error_cmd = "cd";
      return detail::text_result(tr("cd_usage") + "\n", 1);
    }
    const std::string& path = rest.front();
    if(chdir(path.c_str()) == 0){
      char buf[4096];
      if(getcwd(buf, sizeof(buf))){
        return detail::text_result(std::string(buf) + "\n");
      }
      return detail::text_result("\n");
    }
    std::ostringstream oss;
    oss << "cd: " << path << ": " << std::strerror(errno) << "\n";
    g_parse_error_cmd = "cd";
    return detail::text_result(oss.str(), 1);
  }
};

inline ToolDefinition make_cd_tool(){
  ToolDefinition def;
  def.ui = Cd::ui();
  def.executor = Cd::run;
  return def;
}

} // namespace tool
