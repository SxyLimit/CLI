#pragma once
#include "globals.hpp"
#include <system_error>
#include <thread>
#include <cctype>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <cstdint>

// ===== Path candidates (inline) =====
inline Candidates pathCandidatesForWord(const std::string& fullBuf, const std::string& word, PathKind kind){
  Candidates out;
  std::string dir, base;
  if (!word.empty() && word.back()=='/'){ dir=word; base=""; }
  else { size_t p = word.find_last_of('/'); if(p==std::string::npos){ dir=""; base=word; } else { dir=word.substr(0,p+1); base=word.substr(p+1); } }
  std::string root = dir.empty()? std::string("./") : dir;
  DIR* d = ::opendir(root.c_str()); if(!d) return out;
  auto sw = splitLastWord(fullBuf);
  for(dirent* e = ::readdir(d); e; e = ::readdir(d)){
    std::string name = e->d_name; if(name=="."||name=="..") continue;
    MatchResult match = compute_match(name, base);
    if(!base.empty() && !match.matched) continue;
    if(base.empty() && !match.matched) match.matched = true;
    std::string cand = dir + name;
    struct stat st{}; std::string pth = root + (root.back()=='/'? "" : "/") + name;
    bool isDir=false, isFile=false;
    if(::stat(pth.c_str(), &st)==0){
      isDir = S_ISDIR(st.st_mode);
      isFile = S_ISREG(st.st_mode);
    }
    bool include = false;
    bool dirAsHint = false;
    if(kind == PathKind::Any){
      include = isDir || isFile;
    }else if(kind == PathKind::Dir){
      include = isDir;
    }else if(kind == PathKind::File){
      if(isFile) include = true;
      else if(isDir){ include = true; dirAsHint = true; }
    }
    if(!include) continue;
    if(isDir) cand += "/";
    std::vector<int> positions;
    for(size_t i=0;i<dir.size();++i) positions.push_back(static_cast<int>(i));
    for(int pos : match.positions) positions.push_back(static_cast<int>(dir.size() + pos));
    if(isDir){ /* no change */ }
    out.items.push_back(sw.before + cand);
    out.labels.push_back(cand);
    std::sort(positions.begin(), positions.end());
    out.matchPositions.push_back(std::move(positions));
    out.annotations.push_back(dirAsHint? "[dir]" : "");
    out.exactMatches.push_back(match.exact);
  }
  ::closedir(d);
  std::vector<size_t> idx(out.labels.size()); for(size_t i=0;i<idx.size();++i) idx[i]=i;
  std::sort(idx.begin(),idx.end(),[&](size_t a,size_t b){
    bool ea = (a < out.exactMatches.size()) ? out.exactMatches[a] : false;
    bool eb = (b < out.exactMatches.size()) ? out.exactMatches[b] : false;
    if(ea != eb) return ea > eb;
    return out.labels[a] < out.labels[b];
  });
  Candidates s; s.items.reserve(idx.size()); s.labels.reserve(idx.size()); s.annotations.reserve(idx.size());
  for(size_t k: idx){
    s.items.push_back(out.items[k]);
    s.labels.push_back(out.labels[k]);
    s.matchPositions.push_back(out.matchPositions[k]);
    s.annotations.push_back(out.annotations[k]);
    if(k < out.exactMatches.size()) s.exactMatches.push_back(out.exactMatches[k]);
    else s.exactMatches.push_back(false);
  }
  return s;
}

// ===== Ghost generators =====
inline std::string renderCommandGhost(const ToolSpec& spec, const std::vector<std::string>& toks){
  if (toks.empty() || toks[0] != spec.name) return "";

  if (!spec.subs.empty()){
    if (toks.size()==1 || (toks.size()>=2 &&
        std::none_of(spec.subs.begin(), spec.subs.end(),
                     [&](const SubcommandSpec& s){ return s.name==toks[1]; }))){
      return " <subcommand>";
    }
  }

  // --- 只提示尚未填写的“位置参数占位符” ---
  // 统计已填写的位置参数个数（跳过选项及其值）
  std::set<std::string> usedOpts;
  size_t i = 1, posCount = 0;
  while (i < toks.size()){
    const std::string& tk = toks[i];
    if (!tk.empty() && tk[0] == '-') {
      usedOpts.insert(tk);
      const OptionSpec* def = nullptr;
      for (auto &o : spec.options) if (o.name == tk) { def = &o; break; }
      if (def && def->takesValue && i + 1 < toks.size()){ i += 2; } else { i += 1; }
    } else { posCount += 1; i += 1; }
  }

  std::string out;
  if (posCount < spec.positional.size()){
    for (size_t k = posCount; k < spec.positional.size(); ++k){
      out += " " + spec.positional[k];
    }
  }

  // 选项 ghost：仅提示未使用
  for (auto &opt : spec.options){
    if (usedOpts.count(opt.name)) continue;
    std::string piece = " " + opt.name +
                        (opt.takesValue ? " " + (opt.placeholder.empty()? "<val>" : opt.placeholder) : "");
    out += (opt.required ? piece : " [" + piece.substr(1) + "]");
  }
  return out;
}

inline std::string renderSubGhost(const ToolSpec& parent, const SubcommandSpec& sub,
                           const std::vector<std::string>& /*toks*/, size_t /*subIdx*/,
                           const std::set<std::string>& used){
  auto suppressedByMutex = [&](const std::string& optName){
    for (auto& kv : sub.mutexGroups){
      const auto& group = kv.second;
      bool usedInGroup=false;
      for (auto& g : group) if(used.count(g)){ usedInGroup=true; break; }
      if (usedInGroup && std::find(group.begin(), group.end(), optName)!=group.end() && !used.count(optName)) return true;
    }
    return false;
  };
  std::string out;
  for (auto &ph : sub.positional) out += " " + ph;
  for (auto &o : sub.options){
    if (used.count(o.name) || suppressedByMutex(o.name)) continue;
    std::string piece = " " + o.name + (o.takesValue? " " + (o.placeholder.empty()? "<val>":o.placeholder) : "");
    out += (o.required? piece : " [" + piece.substr(1) + "]");
  }
  (void)parent;
  return out;
}

inline std::string shellEscape(const std::string& arg){
  std::string out;
  out.reserve(arg.size()+2);
  out.push_back('\'');
  for(char c : arg){
    if(c=='\'') out += "'\"'\"'";
    else out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

inline ToolSpec make_llm(){
  ToolSpec t; t.name="llm"; t.summary="Call the Python LLM helper";
  set_tool_summary_locale(t, "en", "Call the Python LLM helper");
  set_tool_summary_locale(t, "zh", "调用 Python LLM 助手");
  SubcommandSpec call{"call", {}, {"<message...>"}, {}, [](const std::vector<std::string>& args){
    if(args.size()<3){
      std::cout<<"usage: llm call <message>\n";
      g_parse_error_cmd = "llm";
      return;
    }
    std::string cmd = "python3 tools/llm.py call";
    for(size_t i=2;i<args.size();++i){
      cmd += " ";
      cmd += shellEscape(args[i]);
    }
    cmd = "MYCLI_LLM_SILENT=1 " + cmd + " > /dev/null 2>&1";
    try{
      std::thread([cmd]{ std::system(cmd.c_str()); }).detach();
      std::cout << "[llm] request dispatched asynchronously. Use `llm recall` to view replies." << "\n";
    }catch(const std::system_error&){
      int rc = std::system(cmd.c_str());
      if(rc!=0) g_parse_error_cmd = "llm";
    }
  }};
  SubcommandSpec recall{"recall", {}, {}, {}, [](const std::vector<std::string>& args){
    if(args.size()>2){
      std::cout<<"usage: llm recall\n";
      g_parse_error_cmd = "llm";
      return;
    }
    std::string cmd = "python3 tools/llm.py recall";
    int rc = std::system(cmd.c_str());
    llm_poll();
    if(rc!=0){
      g_parse_error_cmd = "llm";
    }else{
      llm_mark_seen();
    }
  }};
  t.subs = {call, recall};
  t.handler = [](const std::vector<std::string>&){
    std::cout<<"usage: llm <call|recall>\n";
  };
  return t;
}

inline ToolSpec make_message(){
  ToolSpec t; t.name="message"; t.summary="Show unread markdown notifications";
  set_tool_summary_locale(t, "en", "Show unread markdown notifications");
  set_tool_summary_locale(t, "zh", "查看未读的 Markdown 通知");

  auto ensureFolderConfigured = []()->bool{
    const std::string& folder = message_watch_folder();
    if(folder.empty()){
      std::cout<<"message folder not configured. Use `setting set message.folder <path>` first.\n";
      return false;
    }
    return true;
  };

  auto formatTime = [](std::time_t ts){
    char buf[64];
    if(std::tm* lt = std::localtime(&ts)){
      if(std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", lt)){
        return std::string(buf);
      }
    }
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(ts));
    return std::string(buf);
  };

  auto printFile = [](const std::string& path){
    std::ifstream in(path);
    if(!in.good()){
      std::cout<<"[message] unable to open file\n";
      return false;
    }
    std::string line;
    while(std::getline(in, line)){
      std::cout<<line<<"\n";
    }
    if(in.fail() && !in.eof()){
      std::cout<<"[message] error reading file\n";
    }
    return true;
  };

  t.subs = {
    SubcommandSpec{"list", {}, {}, {}, [ensureFolderConfigured, formatTime](const std::vector<std::string>&){
      message_poll();
      if(!ensureFolderConfigured()) return;
      auto pending = message_pending_files();
      const std::string& folder = message_watch_folder();
      if(pending.empty()){
        std::cout<<"No modified markdown files detected in "<<folder<<".\n";
        return;
      }
      std::cout<<"Modified markdown files in "<<folder<<":\n";
      for(const auto& info : pending){
        std::string tag = info.isNew? "[NEW]" : "[UPDATED]";
        std::cout<<"  "<<tag<<" "<<basenameOf(info.path)
                 <<"  ("<<formatTime(info.modifiedAt)<<")\n";
      }
    }},
    SubcommandSpec{"last", {}, {}, {}, [ensureFolderConfigured, printFile](const std::vector<std::string>&){
      message_poll();
      if(!ensureFolderConfigured()) return;
      auto pending = message_pending_files();
      const std::string& folder = message_watch_folder();
      if(pending.empty()){
        std::cout<<"No modified markdown files detected in "<<folder<<".\n";
        return;
      }
      const auto& info = pending.front();
      std::cout<<"--- "<<info.path<<" ---\n";
      if(printFile(info.path)){
        message_mark_read(info.path);
      }
    }},
    SubcommandSpec{"detail", {}, {"<file>"}, {}, [ensureFolderConfigured, printFile](const std::vector<std::string>& args){
      if(args.size()<3){
        std::cout<<"usage: message detail <file>\n";
        g_parse_error_cmd = "message";
        return;
      }
      message_poll();
      if(!ensureFolderConfigured()) return;
      auto resolved = message_resolve_label(args[2]);
      if(!resolved){
        std::cout<<"message file not found: "<<args[2]<<"\n";
        g_parse_error_cmd = "message";
        return;
      }
      std::cout<<"--- "<<*resolved<<" ---\n";
      if(printFile(*resolved)){
        message_mark_read(*resolved);
      }
    }}
  };

  t.handler = [](const std::vector<std::string>&){
    std::cout<<"usage: message <list|last|detail>\n";
  };
  return t;
}

// =================== Built-in Tools ===================
struct TerminalArtImage {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;
};

inline std::optional<TerminalArtImage> load_terminal_art_image(const std::filesystem::path& path){
  std::ifstream in(path);
  if(!in.good()) return std::nullopt;
  TerminalArtImage img;
  if(!(in >> img.width >> img.height)) return std::nullopt;
  if(img.width <= 0 || img.height <= 0) return std::nullopt;
  const std::size_t pixelCount = static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height);
  img.pixels.resize(pixelCount * 3);
  for(std::size_t idx = 0; idx < pixelCount; ++idx){
    int r = 0, g = 0, b = 0;
    if(!(in >> r >> g >> b)){
      return std::nullopt;
    }
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);
    img.pixels[idx * 3 + 0] = static_cast<std::uint8_t>(r);
    img.pixels[idx * 3 + 1] = static_cast<std::uint8_t>(g);
    img.pixels[idx * 3 + 2] = static_cast<std::uint8_t>(b);
  }
  return img;
}

inline const TerminalArtImage* load_cached_terminal_art(const std::string& theme){
  struct Cache {
    std::string resolvedPath;
    std::filesystem::file_time_type mtime{};
    bool hasMtime = false;
    bool valid = false;
    TerminalArtImage image;
  };
  static std::map<std::string, Cache> caches;

  auto pathIt = g_settings.promptThemeArtPaths.find(theme);
  std::string configured = (pathIt == g_settings.promptThemeArtPaths.end()) ? std::string() : pathIt->second;

  Cache& cache = caches[theme];
  if(configured.empty()){
    cache.valid = false;
    cache.hasMtime = false;
    cache.resolvedPath.clear();
    return nullptr;
  }

  std::filesystem::path p(configured);
  if(p.is_relative()){
    p = std::filesystem::path(config_home()) / p;
  }
  p = std::filesystem::absolute(p);

  if(cache.resolvedPath != p.string()){
    cache.valid = false;
    cache.hasMtime = false;
  }

  std::error_code ec;
  auto currentMtime = std::filesystem::last_write_time(p, ec);
  if(ec){
    cache.valid = false;
    cache.hasMtime = false;
    cache.resolvedPath = p.string();
    return nullptr;
  }

  if(!cache.valid || !cache.hasMtime || cache.mtime != currentMtime){
    auto loaded = load_terminal_art_image(p);
    if(!loaded){
      cache.valid = false;
      cache.hasMtime = true;
      cache.mtime = currentMtime;
      cache.resolvedPath = p.string();
      return nullptr;
    }
    cache.image = std::move(*loaded);
    cache.valid = true;
    cache.hasMtime = true;
    cache.mtime = currentMtime;
    cache.resolvedPath = p.string();
  }
  return cache.valid ? &cache.image : nullptr;
}

inline std::vector<std::string> render_terminal_art_lines(const TerminalArtImage& img){
  const int horizontalScale = 2;
  std::vector<std::string> lines;
  lines.reserve(static_cast<std::size_t>(std::max(0, img.height)));
  for(int y = 0; y < img.height; ++y){
    std::string line;
    line.reserve(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(horizontalScale) * 10 + 8);
    for(int x = 0; x < img.width; ++x){
      std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)) * 3;
      unsigned int r = img.pixels[idx + 0];
      unsigned int g = img.pixels[idx + 1];
      unsigned int b = img.pixels[idx + 2];
      char buf[32];
      std::snprintf(buf, sizeof(buf), "\x1b[48;2;%u;%u;%um", r, g, b);
      line += buf;
      for(int i = 0; i < horizontalScale; ++i){
        line.push_back(' ');
      }
    }
    line += ansi::RESET;
    lines.push_back(std::move(line));
  }
  return lines;
}

inline const std::vector<std::string>& mycli_ascii_art_template(){
  static const std::vector<std::string> lines = {
    R"( __  __       ____ _     ___ )",
    R"(|  \/  | ___ / ___| |   |_ _|)",
    R"(| |\/| |/ _ \ |   | |    | | )",
    R"(| |  | |  __/ |___| |___ | | )",
    R"(|_|  |_|\___|\____|_____|___|)"
  };
  return lines;
}

inline std::string gradient_line_with_theme(const std::string& line,
                                            int startR, int startG, int startB,
                                            int endR, int endG, int endB){
  int total = 0;
  for(unsigned char ch : line){
    if(!std::isspace(static_cast<unsigned char>(ch))) ++total;
  }
  if(total == 0){
    return std::string(ansi::BOLD) + line + ansi::RESET;
  }
  std::string out;
  out.reserve(line.size() * 10);
  out += ansi::BOLD;
  int idx = 0;
  for(char c : line){
    unsigned char uc = static_cast<unsigned char>(c);
    if(std::isspace(static_cast<unsigned char>(uc))){
      out.push_back(c);
      continue;
    }
    double t = (total == 1) ? 0.0 : static_cast<double>(idx) / static_cast<double>(total - 1);
    int r = static_cast<int>(startR + (endR - startR) * t + 0.5);
    int g = static_cast<int>(startG + (endG - startG) * t + 0.5);
    int b = static_cast<int>(startB + (endB - startB) * t + 0.5);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm", r, g, b);
    out += buf;
    out.push_back(c);
    ++idx;
  }
  out += ansi::RESET;
  return out;
}

inline std::vector<std::string> render_mycli_ascii_art(){
  const auto& base = mycli_ascii_art_template();
  std::vector<std::string> colored;
  colored.reserve(base.size());
  const std::string theme = g_settings.promptTheme;
  if(const TerminalArtImage* art = load_cached_terminal_art(theme); art){
    return render_terminal_art_lines(*art);
  }
  if(auto gradient = theme_gradient_colors(theme); gradient.has_value()){
    const auto& colors = *gradient;
    for(const auto& line : base){
      colored.push_back(gradient_line_with_theme(line,
                                                colors[0], colors[1], colors[2],
                                                colors[3], colors[4], colors[5]));
    }
  }else{
    for(const auto& line : base){
      colored.push_back(std::string(ansi::CYAN) + ansi::BOLD + line + ansi::RESET);
    }
  }
  return colored;
}

inline ToolSpec make_show(){
  ToolSpec t; t.name="show"; t.summary="Show system information";
  set_tool_summary_locale(t, "en", "Show system information");
  set_tool_summary_locale(t, "zh", "显示系统信息");
  t.subs = {
    SubcommandSpec{"LICENSE", {}, {}, {}, [](const std::vector<std::string>&){
      std::ifstream in("LICENSE");
      if(!in.good()){
        std::cout<<tr("show_license_error")<<"\n";
        return;
      }
      std::ostringstream oss;
      oss << in.rdbuf();
      if(in.bad()){
        std::cout<<tr("show_license_error")<<"\n";
        return;
      }
      std::string content = oss.str();
      if(content.empty()){
        std::cout<<"\n";
      }else{
        std::cout<<content;
        if(content.back()!='\n') std::cout<<"\n";
      }
    }},
    SubcommandSpec{"MyCLI", {}, {}, {}, [](const std::vector<std::string>&){
      std::cout<<tr("show_mycli_version")<<"\n\n";
      const auto art = render_mycli_ascii_art();
      for(const auto& line : art){
        std::cout<<line<<"\n";
      }
      std::cout<<std::flush;
    }}
  };
  t.handler = [](const std::vector<std::string>&){
    std::cout<<tr("show_usage")<<"\n";
  };
  return t;
}
inline ToolSpec make_clear(){
  ToolSpec t; t.name="clear"; t.summary="Clear the terminal screen";
  set_tool_summary_locale(t, "en", "Clear the terminal screen");
  set_tool_summary_locale(t, "zh", "清空终端屏幕");
  t.handler = [](const std::vector<std::string>&){
    std::cout << "\x1b[2J\x1b[3J\x1b[Hmycli>" << std::flush;
  };
  return t;
}
inline ToolSpec make_setting(){
  ToolSpec t; t.name="setting"; t.summary="Manage CLI settings";
  set_tool_summary_locale(t, "en", "Manage CLI settings");
  set_tool_summary_locale(t, "zh", "管理 CLI 设置");
  t.subs = {
    SubcommandSpec{"get", {}, {"<key>"}, {}, [](const std::vector<std::string>& a){
      if(a.size()<3){
        std::cout<<tr("setting_get_usage")<<"\n";
        g_parse_error_cmd="setting";
        return;
      }
      std::string value;
      const std::string& key = a[2];
      if(!settings_get_value(key, value)){
        std::cout<<trFmt("setting_unknown_key", {{"key", key}})<<"\n";
        g_parse_error_cmd="setting";
        return;
      }
      std::cout<<trFmt("setting_get_value", {{"key", key}, {"value", value}})<<"\n";
    }},
    SubcommandSpec{"set", {}, {"<key>","<value>"}, {}, [](const std::vector<std::string>& a){
      if(a.size()<4){
        std::cout<<tr("setting_set_usage")<<"\n";
        g_parse_error_cmd="setting";
        return;
      }
      std::string error;
      const std::string& key = a[2];
      const std::string& value = a[3];
      if(!settings_set_value(key, value, error)){
        if(error=="unknown_key"){
          std::cout<<trFmt("setting_unknown_key", {{"key", key}})<<"\n";
        }else{
          std::cout<<trFmt("setting_invalid_value", {{"key", key}, {"value", value}})<<"\n";
        }
        g_parse_error_cmd="setting";
        return;
      }
      save_settings(settings_file_path());
      std::cout<<trFmt("setting_set_success", {{"key", key}, {"value", value}})<<"\n";
    }},
    SubcommandSpec{"list", {}, {}, {}, [](const std::vector<std::string>&){
      std::cout<<tr("setting_list_header")<<"\n";
      auto keys = settings_list_keys();
      for(auto &k : keys){
        std::string value; settings_get_value(k, value);
        std::cout<<"  "<<k<<" = "<<value<<"\n";
      }
    }}
  };
  t.handler = [](const std::vector<std::string>&){
    std::cout<<tr("setting_usage")<<"\n";
  };
  return t;
}
inline ToolSpec make_run(){
  ToolSpec t; t.name="run"; t.summary="Run a demo job with options";
  set_tool_summary_locale(t, "en", "Run a demo job with options");
  set_tool_summary_locale(t, "zh", "运行示例任务（带参数）");
  t.options = {
    {"--model", true, {"alpha","bravo","charlie","delta"}, nullptr, false, "<name>", false},
    {"--topk",  true, {"1","3","5","8","10"},             nullptr, false, "<k>",    false},
    {"--temp",  true, {"0.0","0.2","0.5","0.8","1.0"},    nullptr, false, "<t>",    false},
    {"--input", true, {},                                 nullptr, false, "<path>", true }
  };
  t.handler = [](const std::vector<std::string>& a){
    std::string model="alpha", topk="5", temp="0.2", inputPath="";
    for(size_t i=1;i+1<a.size();++i){
      if(a[i]=="--model") model=a[i+1];
      else if(a[i]=="--topk")  topk=a[i+1];
      else if(a[i]=="--temp")  temp=a[i+1];
      else if(a[i]=="--input") inputPath=a[i+1];
      else if(a[i].rfind("--",0)==0){ std::cout<<"unknown option: "<<a[i]<<"\n"; g_parse_error_cmd="run"; return; }
    }
    std::cout<<"(run) model="<<model<<" topk="<<topk<<" temp="<<temp
             <<(inputPath.empty()? "":(" input="+inputPath))<<"\n";
  };
  return t;
}
inline ToolSpec make_cd(){
  // -o: Omit；-o -a: Hidden；-o -c: Full；否则 cd <path>
  ToolSpec t; t.name="cd"; t.summary="Change directory";
  set_tool_summary_locale(t, "en", "Change directory");
  set_tool_summary_locale(t, "zh", "切换目录");
  t.positional={"<dir>"}; // 关键：占位符含 <dir> → 自动路径补全并限制目录
  t.handler=[](const std::vector<std::string>& a){
    bool flag_o=false, flag_a=false, flag_c=false;
    std::vector<std::string> rest;
    for(size_t i=1;i<a.size();++i){
      if(a[i]=="-o") flag_o=true;
      else if(a[i]=="-a") flag_a=true;
      else if(a[i]=="-c") flag_c=true;
      else rest.push_back(a[i]);
    }
    if(flag_o){
      std::string mode = "omit";
      if(flag_a) mode = "hidden";
      else if(flag_c) mode = "full";
      std::string error;
      if(settings_set_value("prompt.cwd", mode, error)){
        save_settings(settings_file_path());
        std::string modeLabel = tr(std::string("mode.")+mode);
        std::cout<<trFmt("cd_mode_updated", {{"mode", modeLabel}})<<"\n";
        return;
      }
      std::cout<<tr("cd_mode_error")<<"\n";
      g_parse_error_cmd="cd";
      return;
    }
    if(flag_a || flag_c){
      std::cout<<tr("cd_usage")<<"\n";
      g_parse_error_cmd="cd";
      return;
    }
    if(rest.empty()){ std::cout<<tr("cd_usage")<<"\n"; g_parse_error_cmd="cd"; return; }
    const std::string& path = rest[0];
    if(::chdir(path.c_str())==0){ char buf[4096]; if(getcwd(buf,sizeof(buf))) std::cout<<buf<<"\n"; }
    else { std::perror("cd"); }
  };
  return t;
}
inline ToolSpec make_ls(){
  ToolSpec t; t.name="ls"; t.summary="List directory (simple)";
  set_tool_summary_locale(t, "en", "List directory (simple)");
  set_tool_summary_locale(t, "zh", "列出目录（简化版）");
  t.options={{"-a",false,{},nullptr,false,"",false},{"-l",false,{},nullptr,false,"",false}};
  t.positional={"[<dir>]"}; // 若作为位置参，也会自动路径补全
  t.handler=[](const std::vector<std::string>& a){
    bool showDot=false; bool longFmt=false; std::string path=".";
    for(size_t i=1;i<a.size();++i){
      if(a[i]=="-a") showDot=true;
      else if(a[i]=="-l") longFmt=true;
      else if(!a[i].empty() && a[i][0]=='-'){ std::cout<<"unknown option: "<<a[i]<<"\n"; g_parse_error_cmd="ls"; return; }
      else path=a[i];
    }
    DIR* d = ::opendir(path.c_str()); if(!d){ std::perror("ls"); return; }
    std::vector<std::string> names;
    for(dirent* e=readdir(d); e; e=readdir(d)){
      std::string n=e->d_name; if(!showDot && n.size() && n[0]=='.') continue;
      bool dir=isDirFS(path + (path.back()=='/'?"":"/") + n);
      names.push_back(n + (dir?"/":""));
    } closedir(d);
    std::sort(names.begin(),names.end());
    if(longFmt){ for(auto&s:names) std::cout<<s<<"\n"; }
    else{ for(size_t i=0;i<names.size();++i){ std::cout<<names[i]<<(((i+1)%8)?"  ":"\n"); } if(names.size()%8) std::cout<<"\n"; }
  };
  return t;
}
inline ToolSpec make_cat(){
  ToolSpec t; t.name="cat"; t.summary="Print file content (<=1MB, UTF-8)";
  set_tool_summary_locale(t, "en", "Print file content (<=1MB, UTF-8)");
  set_tool_summary_locale(t, "zh", "输出文件内容（<=1MB，UTF-8）");
  t.positional={"<file>"}; // 关键：<file> → 自动路径补全
  t.handler=[](const std::vector<std::string>& a){
    if(a.size()<2){ std::cout<<"usage: cat <file>\n"; g_parse_error_cmd="cat"; return; }
    std::ifstream in(a[1], std::ios::binary);
    if(!in){ std::perror("cat"); return; }
    in.seekg(0,std::ios::end); auto sz=in.tellg(); in.seekg(0);
    if(sz>1024*1024){ std::cout<<"[cat] file too large (>1MB)\n"; return; }
    std::string data; data.resize((size_t)sz);
    if(sz>0) in.read(&data[0], sz);
    std::cout<<data<<(data.size()&&data.back()!='\n'? "\n": "");
  };
  return t;
}
inline ToolSpec make_exit_tool(const std::string& name){
  ToolSpec t; t.name = name; t.summary="Exit the shell";
  t.handler = [](const std::vector<std::string>&){ g_should_exit = true; };
  return t;
}
inline StatusProvider make_cwd_status(){
  return StatusProvider{"cwd", [](){
    if(g_cwd_mode==CwdMode::Hidden) return std::string();
    char buf[4096]; if(!getcwd(buf,sizeof(buf))) return std::string();
    std::string full(buf);
    if(g_cwd_mode==CwdMode::Omit) return std::string("[")+basenameOf(full)+"] ";
    return std::string("[")+full+"] ";
  }};
}

// =================== Dynamic Tools Loader ===================
// 支持 [tool] 顶层 + [tool.sub] 子命令；支持 type=system/python
inline std::string trim(const std::string& s){
  size_t a=0,b=s.size(); while(a<b && (s[a]==' '||s[a]=='\t')) ++a; while(b>a && (s[b-1]==' '||s[b-1]=='\t'||s[b-1]=='\r')) --b; return s.substr(a,b-a);
}
inline std::vector<std::string> splitCSV(const std::string& s){
  std::vector<std::string> out; std::string cur; for(char c: s){ if(c==','){ auto t=trim(cur); if(!t.empty()) out.push_back(t); cur.clear(); } else cur.push_back(c); }
  auto t=trim(cur); if(!t.empty()) out.push_back(t); return out;
}

inline void register_tools_from_config(const std::string& path){
  std::ifstream in(path);
  if(!in.good()) return;

  struct TmpTool {
    std::string summary, type="system", exec, script, options, positional, optionPaths;
    std::map<std::string, std::string> summaryLocales;
    std::map<std::string, std::vector<std::string>> optvalues;
    struct TmpSub {
      std::string name, options, positional, optionPaths;
      std::map<std::string, std::vector<std::string>> optvalues;
      std::map<std::string, std::vector<std::string>> mutexGroups;
    };
    std::map<std::string, TmpSub> subs;
  };

  std::map<std::string, TmpTool> all;

  std::string line, section;
  while(std::getline(in, line)){
    auto s = trim(line);
    if(s.empty() || s[0]=='#' || s[0]==';') continue;
    if(s.front()=='[' && s.back()==']'){ section = trim(s.substr(1, s.size()-2)); continue; }
    auto eq = s.find('='); if(eq==std::string::npos) continue;
    auto k = trim(s.substr(0,eq)); auto v = trim(s.substr(eq+1));

    auto dot = section.find('.');
    std::string tool = section, sub;
    if(dot!=std::string::npos){ tool = section.substr(0,dot); sub = section.substr(dot+1); }

    TmpTool& T = all[tool];
    if(sub.empty()){
      if(k=="summary") T.summary = v;
      else if(k.rfind("summary.",0)==0){
        auto lang = k.substr(8);
        if(!lang.empty()) T.summaryLocales[lang] = v;
      }
      else if(k=="type") T.type = v;
      else if(k=="exec") T.exec = v;
      else if(k=="script") T.script = v;
      else if(k=="options") T.options = v;
      else if(k=="positional") T.positional = v;
      else if(k=="optionPaths") T.optionPaths = v;
      else if(k.rfind("optvalues.",0)==0){
        auto on = k.substr(10); T.optvalues[on] = splitCSV(v);
      }
    }else{
      auto& S = T.subs[sub]; S.name=sub;
      if(k=="options") S.options = v;
      else if(k=="positional") S.positional = v;
      else if(k=="optionPaths") S.optionPaths = v;
      else if(k.rfind("optvalues.",0)==0){
        auto on=k.substr(10); S.optvalues[on] = splitCSV(v);
      }else if(k=="mutex"){ // mutex=group:opt1|opt2,group2:optA|optB
        for(auto& grpKV : splitCSV(v)){
          auto colon = grpKV.find(':'); if(colon==std::string::npos) continue;
          auto gname = trim(grpKV.substr(0,colon));
          auto gvals = trim(grpKV.substr(colon+1));
          std::vector<std::string> opts; std::string cur;
          for(char c: gvals){ if(c=='|'){ auto t=trim(cur); if(!t.empty()) opts.push_back(t); cur.clear(); } else cur.push_back(c); }
          auto t=trim(cur); if(!t.empty()) opts.push_back(t);
          if(!gname.empty() && !opts.empty()) S.mutexGroups[gname]=opts;
        }
      }
    }
  }

  // 构建并注册
  for(auto& kv : all){
    const std::string& name = kv.first;
    TmpTool& T = kv.second;

    ToolSpec tool; tool.name = name; tool.summary = T.summary;
    tool.summaryLocales = T.summaryLocales;

    std::set<std::string> topPathOpts;
    for(auto& s : splitCSV(T.optionPaths)) topPathOpts.insert(s);
    for(auto& oname : splitCSV(T.options)){
      OptionSpec o; o.name = oname;
      if(T.optvalues.count(oname)){ o.takesValue = true; o.valueSuggestions = T.optvalues[oname]; }
      if(topPathOpts.count(oname)){ o.takesValue = true; o.isPath = true; if(o.placeholder.empty()) o.placeholder = "<path>"; }
      tool.options.push_back(o);
    }
    if(!T.positional.empty()){
      tool.positional = splitTokens(T.positional);
    }

    if(!T.subs.empty()){
      for(auto& skv : T.subs){
        const auto& S = skv.second;
        SubcommandSpec sub; sub.name = S.name;

        std::set<std::string> subPathOpts;
        for(auto& s : splitCSV(S.optionPaths)) subPathOpts.insert(s);
        for(auto& oname : splitCSV(S.options)){
          OptionSpec o; o.name = oname;
          if(S.optvalues.count(oname)){ o.takesValue = true; o.valueSuggestions = S.optvalues.at(oname); }
          if(subPathOpts.count(oname)){ o.takesValue = true; o.isPath = true; if(o.placeholder.empty()) o.placeholder="<path>"; }
          sub.options.push_back(o);
        }
        sub.positional = splitTokens(S.positional);
        sub.mutexGroups = S.mutexGroups;

        if(T.type=="python"){
          sub.handler = [exe=T.exec, script=T.script, subcmd=sub.name](const std::vector<std::string>& args){
            if(exe.empty() || script.empty()){ std::cout<<"python tool not configured\n"; g_parse_error_cmd=args.empty()?"":args[0]; return; }
            std::string cmd = exe + " " + script + " " + subcmd;
            for(size_t i=2;i<args.size();++i){ cmd += " "; cmd += args[i]; }
            int rc = std::system(cmd.c_str()); if(rc!=0){ g_parse_error_cmd = args[0]; }
          };
        }else{
          sub.handler = [exe=T.exec.empty()? name : T.exec, subcmd=sub.name](const std::vector<std::string>& args){
            std::string cmd = exe; if(!subcmd.empty()){ cmd += " "; cmd += subcmd; }
            for(size_t i=2;i<args.size();++i){ cmd += " "; cmd += args[i]; }
            int rc = std::system(cmd.c_str()); if(rc!=0){ g_parse_error_cmd = args[0]; }
          };
        }
        tool.subs.push_back(std::move(sub));
      }
      tool.handler = [](const std::vector<std::string>&){ std::cout<<"usage: <tool> <subcommand> [options]\n"; };
    }else{
      if(T.type=="python"){
        tool.handler = [exe=T.exec, script=T.script](const std::vector<std::string>& args){
          if(exe.empty() || script.empty()){ std::cout<<"python tool not configured\n"; g_parse_error_cmd=args.empty()?"":args[0]; return; }
          std::string cmd = exe + std::string(" ") + script;
          for(size_t i=1;i<args.size();++i){ cmd += " "; cmd += args[i]; }
          int rc = std::system(cmd.c_str()); if(rc!=0){ g_parse_error_cmd = args[0]; }
        };
      }else{
        tool.handler = [exe=T.exec.empty()? name : T.exec](const std::vector<std::string>& args){
          std::string cmd = exe; for(size_t i=1;i<args.size();++i){ cmd += " "; cmd += args[i]; }
          int rc = std::system(cmd.c_str()); if(rc!=0){ g_parse_error_cmd = args[0]; }
        };
      }
    }

    REG.registerTool(tool);
  }
}

// =================== Register All ===================
inline void register_all_tools(){
  REG.registerTool(make_show());
  REG.registerTool(make_clear());
  REG.registerTool(make_setting());
  REG.registerTool(make_run());
  REG.registerTool(make_llm());
  REG.registerTool(make_message());
  // git 从配置加载
  REG.registerTool(make_cd());
  REG.registerTool(make_ls());
  REG.registerTool(make_cat());
  REG.registerTool(make_exit_tool("exit"));
  REG.registerTool(make_exit_tool("quit"));
}
inline void register_status_providers(){
  REG.registerStatusProvider(make_cwd_status());
}