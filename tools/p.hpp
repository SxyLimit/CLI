#pragma once

#include "tool_common.hpp"

namespace tool {

struct HistoryReplay {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "p";
    spec.summary = "Browse recent commands";
    set_tool_summary_locale(spec, "en", "Browse recent commands");
    set_tool_summary_locale(spec, "zh", "查看最近使用的命令");
    spec.help = "Displays the recent command history. Type `p` followed by a space to trigger history completions and press Tab to insert a previous command.";
    set_tool_help_locale(spec, "en", spec.help);
    set_tool_help_locale(spec, "zh", "显示最近输入的命令。输入 `p` 再加空格即可触发历史补全，按 Tab 可将选中的旧指令直接放回输入行。");
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    (void)request;
    ToolExecutionResult result;
    const auto& history = history_recent_commands();
    std::ostringstream oss;
    if(history.empty()){
      oss << "No recent commands." << '\n';
    }else{
      oss << "Recent commands (most recent first):" << '\n';
      int index = 1;
      for(const auto& cmd : history){
        oss << index++ << ". " << cmd << '\n';
      }
    }
    std::string output = oss.str();
    result.output = output;
    result.display = output;
    return result;
  }

  static Candidates complete(const std::string& buffer, const std::vector<std::string>& tokens){
    Candidates cand;
    if(tokens.empty() || tokens[0] != "p") return cand;
    bool trailingSpace = (!buffer.empty() && std::isspace(static_cast<unsigned char>(buffer.back())));
    if(tokens.size() > 1 && trailingSpace) return cand;
    if(tokens.size() <= 1 && !trailingSpace) return cand;

    auto sw = splitLastWord(buffer);
    const auto& history = history_recent_commands();
    for(const auto& cmd : history){
      MatchResult match = compute_match(cmd, sw.word);
      if(!match.matched) continue;
      cand.items.push_back(cmd);
      cand.labels.push_back(cmd);
      cand.matchPositions.push_back(match.positions);
      cand.annotations.push_back("");
      cand.exactMatches.push_back(match.exact);
      cand.matchDetails.push_back(match);
    }
    return cand;
  }
};

inline ToolDefinition make_p_tool(){
  ToolDefinition def;
  def.ui = HistoryReplay::ui();
  def.executor = HistoryReplay::run;
  def.completion = HistoryReplay::complete;
  return def;
}

} // namespace tool
