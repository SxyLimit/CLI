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
      {"-l", false, {}, nullptr, false, "", false},
      {"-t", false, {}, nullptr, false, "", false},
      {"-S", false, {}, nullptr, false, "", false},
      {"-X", false, {}, nullptr, false, "", false},
      {"-v", false, {}, nullptr, false, "", false},
      {"-r", false, {}, nullptr, false, "", false}
    };
    spec.positional = {positional("[<dir>]")};
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    bool showDot = false;
    bool longFmt = false;
    bool reverse = false;
    enum class SortMode { Name, Time, Size, Extension, Version };
    SortMode sortMode = SortMode::Name;
    std::string path = ".";
    for(size_t i = 1; i < args.size(); ++i){
      const std::string& token = args[i];
      if(token == "--"){
        if(i + 1 < args.size()){
          path = args[++i];
        }
        continue;
      }
      if(!token.empty() && token[0] == '-' && token.size() > 1){
        for(size_t j = 1; j < token.size(); ++j){
          switch(token[j]){
            case 'a': showDot = true; break;
            case 'l': longFmt = true; break;
            case 't': sortMode = SortMode::Time; break;
            case 'S': sortMode = SortMode::Size; break;
            case 'X': sortMode = SortMode::Extension; break;
            case 'v': sortMode = SortMode::Version; break;
            case 'r': reverse = true; break;
            default:
              g_parse_error_cmd = "ls";
              return detail::text_result("unknown option: -" + std::string(1, token[j]) + "\n", 1);
          }
        }
      }else{
        path = token;
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

    auto natural_compare = [](const std::string& a, const std::string& b) -> int {
      size_t ia = 0, ib = 0;
      while(ia < a.size() && ib < b.size()){
        if(std::isdigit(static_cast<unsigned char>(a[ia])) &&
           std::isdigit(static_cast<unsigned char>(b[ib]))){
          size_t ja = ia;
          size_t jb = ib;
          while(ja < a.size() && std::isdigit(static_cast<unsigned char>(a[ja]))) ++ja;
          while(jb < b.size() && std::isdigit(static_cast<unsigned char>(b[jb]))) ++jb;
          std::string numA = a.substr(ia, ja - ia);
          std::string numB = b.substr(ib, jb - ib);
          auto trim_leading_zero = [](const std::string& s){
            size_t pos = 0;
            while(pos + 1 < s.size() && s[pos] == '0') ++pos;
            return s.substr(pos);
          };
          numA = trim_leading_zero(numA);
          numB = trim_leading_zero(numB);
          if(numA.size() != numB.size()) return numA.size() < numB.size() ? -1 : 1;
          int cmp = numA.compare(numB);
          if(cmp != 0) return cmp < 0 ? -1 : 1;
          ia = ja;
          ib = jb;
          continue;
        }
        unsigned char ca = static_cast<unsigned char>(a[ia]);
        unsigned char cb = static_cast<unsigned char>(b[ib]);
        if(ca != cb) return ca < cb ? -1 : 1;
        ++ia;
        ++ib;
      }
      if(ia == a.size() && ib == b.size()) return 0;
      return ia == a.size() ? -1 : 1;
    };

    auto ext_of = [](const std::string& name){
      std::string base = name;
      if(!base.empty() && base.back() == '/') base.pop_back();
      auto pos = base.find_last_of('.');
      if(pos == std::string::npos || pos == 0) return std::string();
      return base.substr(pos + 1);
    };

    std::sort(entries.begin(), entries.end(), [&](const EntryInfo& a, const EntryInfo& b){
      switch(sortMode){
        case SortMode::Time:
          if(a.modifiedAt != b.modifiedAt) return a.modifiedAt > b.modifiedAt;
          break;
        case SortMode::Size:
          if(a.size != b.size) return a.size > b.size;
          break;
        case SortMode::Extension: {
          std::string ea = ext_of(a.name);
          std::string eb = ext_of(b.name);
          if(ea != eb) return ea < eb;
          break;
        }
        case SortMode::Version: {
          int cmp = natural_compare(a.name, b.name);
          if(cmp != 0) return cmp < 0;
          break;
        }
        case SortMode::Name:
        default:
          break;
      }
      return a.displayName < b.displayName;
    });
    if(reverse){
      std::reverse(entries.begin(), entries.end());
    }

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

