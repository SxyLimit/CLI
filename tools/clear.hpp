#pragma once

#include "tool_common.hpp"

namespace tool {

struct Clear {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "clear";
    spec.summary = "Clear the terminal screen";
    set_tool_summary_locale(spec, "en", "Clear the terminal screen");
    set_tool_summary_locale(spec, "zh", "清空终端屏幕");
    set_tool_help_locale(spec, "en", "Clears the screen and resets the cursor to the top-left corner.");
    set_tool_help_locale(spec, "zh", "清空屏幕并将光标移动到左上角。");
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    (void)request;
    ToolExecutionResult result;
    std::string seq = "\x1b[2J\x1b[3J\x1b[H";
    result.display = seq;
    result.output = seq;
    return result;
  }
};

inline ToolDefinition make_clear_tool(){
  ToolDefinition def;
  def.ui = Clear::ui();
  def.executor = Clear::run;
  return def;
}

} // namespace tool

