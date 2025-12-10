#pragma once

#include "tool_common.hpp"

#include <iomanip>

namespace tool {

struct Ls {
  struct EntryInfo {
    std::string name;
    std::string displayName;
    bool isDir = false;
    off_t size = 0;
    std::time_t modifiedAt = 0;
  };

  static int displayWidthColumns(){
    int terminalWidth = platform::terminal_columns();
    if(terminalWidth <= 0) terminalWidth = 80;
    if(!g_settings.promptInputEllipsisEnabled){
      return terminalWidth;
    }
    if(g_settings.promptInputEllipsisRightWidthAuto){
      return terminalWidth;
    }
    if(g_settings.promptInputEllipsisRightWidth > 0){
      return g_settings.promptInputEllipsisRightWidth;
    }
    return terminalWidth;
  }

  static std::string formatTimestamp(std::time_t ts){
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &ts);
#else
    localtime_r(&ts, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return oss.str();
  }

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
    spec.positional = {positional("[<dir>]")};
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
    std::vector<EntryInfo> entries;
    std::string pathPrefix = path;
    if(!pathPrefix.empty() && pathPrefix.back() != '/') pathPrefix.push_back('/');
    while(dirent* entry = ::readdir(dir)){
      std::string name = entry->d_name;
      if(!showDot && !name.empty() && name[0] == '.') continue;
      EntryInfo info;
      info.name = name;
      struct stat st{};
      if(::stat((pathPrefix + name).c_str(), &st) == 0){
        info.isDir = S_ISDIR(st.st_mode);
        info.modifiedAt = st.st_mtime;
        if(!info.isDir){
          info.size = st.st_size;
        }
      }else{
        info.isDir = false;
        info.modifiedAt = 0;
        info.size = 0;
      }
      info.displayName = info.isDir ? name + "/" : name;
      entries.push_back(std::move(info));
    }
    ::closedir(dir);
    std::sort(entries.begin(), entries.end(), [](const EntryInfo& a, const EntryInfo& b){
      return a.displayName < b.displayName;
    });

    std::ostringstream oss;
    if(longFmt){
      size_t maxSizeWidth = 0;
      for(const auto& e : entries){
        if(!e.isDir){
          maxSizeWidth = std::max(maxSizeWidth, std::to_string(e.size).size());
        }
      }
      for(const auto& e : entries){
        oss << (e.isDir ? 'd' : '-') << ' ';
        if(maxSizeWidth > 0){
          if(e.isDir){
            oss << std::string(maxSizeWidth, ' ');
          }else{
            oss << std::setw(static_cast<int>(maxSizeWidth)) << e.size;
          }
          oss << ' ';
        }
        oss << formatTimestamp(e.modifiedAt) << ' ' << e.displayName << "\n";
      }
    }else{
      size_t maxLen = 0;
      for(const auto& e : entries){
        maxLen = std::max(maxLen, e.displayName.size());
      }
      int widthLimit = displayWidthColumns();
      int colWidth = static_cast<int>(maxLen) + 3;
      if(colWidth <= 0) colWidth = 1;
      int cols = std::max(1, widthLimit / colWidth);
      for(size_t i = 0; i < entries.size(); ++i){
        if(i > 0 && static_cast<int>(i % cols) == 0) oss << "\n";
        oss << std::left << std::setw(colWidth) << entries[i].displayName;
      }
      if(!entries.empty()) oss << "\n";
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

