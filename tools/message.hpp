#pragma once

#include "tool_common.hpp"

namespace tool {

struct Message {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "message";
    spec.summary = "Show unread markdown notifications";
    set_tool_summary_locale(spec, "en", "Show unread markdown notifications");
    set_tool_summary_locale(spec, "zh", "查看未读的 Markdown 通知");
    set_tool_help_locale(spec, "en",
                         "message list | message last | message detail <file>\n"
                         "List unread markdown files, show the latest one, or show a specific file.");
    set_tool_help_locale(spec, "zh",
                         "message list | message last | message detail <文件>\n"
                         "列出未读 Markdown 文件、查看最近一条，或查看指定文件内容。");
    spec.subs = {
      SubcommandSpec{"list", {}, {}, {}, nullptr},
      SubcommandSpec{"last", {}, {}, {}, nullptr},
      SubcommandSpec{"detail", {}, {positional("<file>")}, {}, nullptr}
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 2){
      g_parse_error_cmd = "message";
      return detail::text_result("usage: message <list|last|detail>\n", 1);
    }
    const std::string sub = args[1];
    auto ensureFolderConfigured = []() -> std::optional<std::string>{
      const std::string& folder = message_watch_folder();
      if(folder.empty()){
        return std::nullopt;
      }
      return folder;
    };
    auto folderOpt = ensureFolderConfigured();
    if(!folderOpt){
      return detail::text_result("message folder not configured. Use `setting set message.folder <path>` first.\n", 1);
    }
    message_poll();
    if(sub == "list"){
      auto pending = message_pending_files();
      std::ostringstream oss;
      if(pending.empty()){
        oss << "No modified markdown files detected in " << *folderOpt << ".\n";
        return detail::text_result(oss.str());
      }
      oss << "Modified markdown files in " << *folderOpt << ":\n";
      for(const auto& info : pending){
        std::string tag = info.isNew ? "[NEW]" : "[UPDATED]";
        std::time_t ts = info.modifiedAt;
        char buf[64];
        if(std::tm* lt = std::localtime(&ts);
           lt && std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", lt)){
          oss << "  " << tag << " " << basenameOf(info.path) << "  (" << buf << ")\n";
        }else{
          oss << "  " << tag << " " << basenameOf(info.path) << "\n";
        }
      }
      return detail::text_result(oss.str());
    }
    if(sub == "last"){
      auto pending = message_pending_files();
      if(pending.empty()){
        std::ostringstream oss;
        oss << "No modified markdown files detected in " << *folderOpt << ".\n";
        return detail::text_result(oss.str());
      }
      const auto& info = pending.front();
      std::ostringstream oss;
      oss << "--- " << info.path << " ---\n";
      std::ifstream in(info.path);
      if(!in.good()){
        g_parse_error_cmd = "message";
        return detail::text_result("[message] unable to open file\n", 1);
      }
      std::string line;
      while(std::getline(in, line)){
        oss << line << "\n";
      }
      if(in.fail() && !in.eof()){
        oss << "[message] error reading file\n";
      }
      message_mark_read(info.path);
      return detail::text_result(oss.str());
    }
    if(sub == "detail"){
      if(args.size() < 3){
        g_parse_error_cmd = "message";
        return detail::text_result("usage: message detail <file>\n", 1);
      }
      auto resolved = message_resolve_label(args[2]);
      if(!resolved){
        g_parse_error_cmd = "message";
        return detail::text_result("message file not found: " + args[2] + "\n", 1);
      }
      std::ifstream in(*resolved);
      if(!in.good()){
        g_parse_error_cmd = "message";
        return detail::text_result("[message] unable to open file\n", 1);
      }
      std::ostringstream oss;
      oss << "--- " << *resolved << " ---\n";
      std::string line;
      while(std::getline(in, line)){
        oss << line << "\n";
      }
      if(in.fail() && !in.eof()){
        oss << "[message] error reading file\n";
      }
      message_mark_read(*resolved);
      return detail::text_result(oss.str());
    }
    g_parse_error_cmd = "message";
    return detail::text_result("usage: message <list|last|detail>\n", 1);
  }
};

inline ToolDefinition make_message_tool(){
  ToolDefinition def;
  def.ui = Message::ui();
  def.executor = Message::run;
  return def;
}

} // namespace tool
