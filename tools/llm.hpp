#pragma once

#include "tool_common.hpp"

namespace tool {

struct Llm {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "llm";
    spec.summary = "Call the Python LLM helper";
    set_tool_summary_locale(spec, "en", "Call the Python LLM helper");
    set_tool_summary_locale(spec, "zh", "调用 Python LLM 助手");
    set_tool_help_locale(spec, "en", "llm call <message...> | llm recall | llm new | llm switch <conversation> | llm rename <name>");
    set_tool_help_locale(spec, "zh", "llm call <消息...> | llm recall | llm new | llm switch <对话> | llm rename <名称>");
    spec.subs = {
      SubcommandSpec{"call", {}, {positional("<message...>")}, {}, nullptr},
      SubcommandSpec{"recall", {}, {}, {}, nullptr},
      SubcommandSpec{"new", {}, {}, {}, nullptr},
      SubcommandSpec{"switch", {}, {positional("<conversation>")}, {}, nullptr},
      SubcommandSpec{"rename", {}, {positional("<name>")}, {}, nullptr}
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 2){
      g_parse_error_cmd = "llm";
      return detail::text_result("usage: llm <call|recall|new|switch|rename>\n", 1);
    }
    const std::string sub = args[1];
    if(sub == "call"){
      if(args.size() < 3){
        g_parse_error_cmd = "llm";
        return detail::text_result("usage: llm call <message>\n", 1);
      }
      std::string cmd = "python3 tools/llm.py call";
      for(size_t i = 2; i < args.size(); ++i){
        cmd += " ";
        cmd += shellEscape(args[i]);
      }
      cmd = "MYCLI_LLM_SILENT=1 " + cmd + " > /dev/null 2>&1";
      auto dispatch = [command = cmd]{
        platform::RawModeScope raw_guard;
        std::system(command.c_str());
      };
      try{
        std::thread(dispatch).detach();
      }catch(const std::system_error&){
        dispatch();
      }
      llm_set_pending(true);
      return detail::text_result("[llm] request dispatched asynchronously. Use `llm recall` to view replies.\n");
    }
    if(sub == "recall"){
      auto result = detail::execute_shell(request, "python3 tools/llm.py recall");
      llm_poll();
      if(result.exitCode != 0){
        g_parse_error_cmd = "llm";
      }else{
        llm_mark_seen();
      }
      return result;
    }
    if(sub == "new"){
      if(args.size() > 2){
        g_parse_error_cmd = "llm";
        return detail::text_result("usage: llm new\n", 1);
      }
      auto result = detail::execute_shell(request, "python3 tools/llm.py new");
      if(result.exitCode != 0){
        g_parse_error_cmd = "llm";
      }else{
        llm_poll();
        llm_mark_seen();
      }
      return result;
    }
    if(sub == "switch"){
      if(args.size() < 3){
        g_parse_error_cmd = "llm";
        return detail::text_result("usage: llm switch <conversation>\n", 1);
      }
      std::string target;
      for(size_t i = 2; i < args.size(); ++i){
        if(i > 2) target += " ";
        target += args[i];
      }
      std::string cmd = "python3 tools/llm.py switch " + shellEscape(target);
      auto result = detail::execute_shell(request, cmd);
      if(result.exitCode != 0){
        g_parse_error_cmd = "llm";
      }else{
        llm_poll();
        llm_mark_seen();
      }
      return result;
    }
    if(sub == "rename"){
      if(args.size() < 3){
        g_parse_error_cmd = "llm";
        return detail::text_result("usage: llm rename <name>\n", 1);
      }
      std::string name;
      for(size_t i = 2; i < args.size(); ++i){
        if(i > 2) name += " ";
        name += args[i];
      }
      std::string cmd = "python3 tools/llm.py rename " + shellEscape(name);
      auto result = detail::execute_shell(request, cmd);
      if(result.exitCode != 0){
        g_parse_error_cmd = "llm";
      }else{
        llm_poll();
        llm_mark_seen();
      }
      return result;
    }
    g_parse_error_cmd = "llm";
    return detail::text_result("usage: llm <call|recall|new|switch|rename>\n", 1);
  }
  
  static std::vector<std::string> conversation_names(){
    std::vector<std::string> names;
    auto [code, output] = detail::run_command_capture("python3 tools/llm.py list-names");
    if(code != 0) return names;
    std::istringstream iss(output);
    std::string line;
    while(std::getline(iss, line)){
      if(!line.empty() && line.back() == '\r') line.pop_back();
      if(line.empty()) continue;
      names.push_back(line);
    }
    return names;
  }

  static Candidates complete(const std::string& buffer, const std::vector<std::string>& tokens){
    Candidates cand;
    if(tokens.empty() || tokens[0] != "llm") return cand;
    bool trailingSpace = (!buffer.empty() && std::isspace(static_cast<unsigned char>(buffer.back())));
    static const std::vector<std::string> subs{"call", "recall", "new", "switch", "rename"};
    if(tokens.size() == 1){
      if(trailingSpace){
        for(const auto& sub : subs){
          cand.items.push_back(sub);
          cand.labels.push_back(sub);
          cand.matchPositions.push_back({});
          cand.annotations.push_back("");
          cand.exactMatches.push_back(false);
          cand.matchDetails.push_back({});
        }
      }
      return cand;
    }
    auto sw = splitLastWord(buffer);
    if(tokens.size() == 2 && !trailingSpace){
      for(const auto& sub : subs){
        MatchResult match = compute_match(sub, sw.word);
        if(!match.matched) continue;
        cand.items.push_back(sub);
        cand.labels.push_back(sub);
        cand.matchPositions.push_back(match.positions);
        cand.annotations.push_back("");
        cand.exactMatches.push_back(match.exact);
        cand.matchDetails.push_back(match);
      }
      sortCandidatesByMatch(sw.word, cand);
      return cand;
    }
    if(tokens.size() >= 2 && tokens[1] == "switch"){
      bool expectingConversation = false;
      if(tokens.size() == 2){
        expectingConversation = trailingSpace;
      }else{
        expectingConversation = !trailingSpace;
      }
      if(!expectingConversation){
        return cand;
      }
      std::string query = sw.word;
      if(tokens.size() == 2 && trailingSpace) query.clear();
      auto names = conversation_names();
      for(const auto& name : names){
        if(query.empty()){
          cand.items.push_back(name);
          cand.labels.push_back(name);
          cand.matchPositions.push_back({});
          cand.annotations.push_back("");
          cand.exactMatches.push_back(false);
          cand.matchDetails.push_back({});
          continue;
        }
        MatchResult match = compute_match(name, query);
        if(!match.matched) continue;
        cand.items.push_back(name);
        cand.labels.push_back(name);
        cand.matchPositions.push_back(match.positions);
        cand.annotations.push_back("");
        cand.exactMatches.push_back(match.exact);
        cand.matchDetails.push_back(match);
      }
      if(!query.empty()) sortCandidatesByMatch(query, cand);
      return cand;
    }
    return cand;
  }
};

inline ToolDefinition make_llm_tool(){
  ToolDefinition def;
  def.ui = Llm::ui();
  def.executor = Llm::run;
  def.completion = Llm::complete;
  return def;
}

} // namespace tool
