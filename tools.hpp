#pragma once
#include "globals.hpp"

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
    bool accept = (kind==PathKind::Any) || (kind==PathKind::Dir && isDir) || (kind==PathKind::File && isFile);
    if(!accept) continue;
    if(isDir) cand += "/";
    std::vector<int> positions;
    for(size_t i=0;i<dir.size();++i) positions.push_back(static_cast<int>(i));
    for(int pos : match.positions) positions.push_back(static_cast<int>(dir.size() + pos));
    if(isDir){ /* no change */ }
    out.items.push_back(sw.before + cand);
    out.labels.push_back(cand);
    std::sort(positions.begin(), positions.end());
    out.matchPositions.push_back(std::move(positions));
  }
  ::closedir(d);
  std::vector<size_t> idx(out.labels.size()); for(size_t i=0;i<idx.size();++i) idx[i]=i;
  std::sort(idx.begin(),idx.end(),[&](size_t a,size_t b){ return out.labels[a] < out.labels[b]; });
  Candidates s; s.items.reserve(idx.size()); s.labels.reserve(idx.size());
  for(size_t k: idx){
    s.items.push_back(out.items[k]);
    s.labels.push_back(out.labels[k]);
    s.matchPositions.push_back(out.matchPositions[k]);
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

// =================== Built-in Tools ===================
inline ToolSpec make_show(){
  ToolSpec t; t.name="show"; t.summary="Show system information (config|logs)";
  set_tool_summary_locale(t, "en", "Show system information (config|logs)");
  set_tool_summary_locale(t, "zh", "显示系统信息（config|logs）");
  t.subs = {
    SubcommandSpec{"config", {}, {}, {}, [](const std::vector<std::string>&){
      std::cout<<tr("show_config_output");
    }},
    SubcommandSpec{"logs",   {}, {}, {}, [](const std::vector<std::string>&){
      std::cout<<tr("show_logs_output");
    }}
  };
  t.handler = [](const std::vector<std::string>&){
    std::cout<<tr("show_usage")<<"\n";
  };
  return t;
}
inline ToolSpec make_config(){
  ToolSpec t; t.name="config"; t.summary="Manage CLI configuration";
  set_tool_summary_locale(t, "en", "Manage CLI configuration");
  set_tool_summary_locale(t, "zh", "管理 CLI 配置");
  t.subs = {
    SubcommandSpec{"get", {}, {"<key>"}, {}, [](const std::vector<std::string>& a){
      if(a.size()<3){
        std::cout<<tr("config_get_usage")<<"\n";
        g_parse_error_cmd="config";
        return;
      }
      std::string value;
      const std::string& key = a[2];
      if(!config_get_value(key, value)){
        std::cout<<trFmt("config_unknown_key", {{"key", key}})<<"\n";
        g_parse_error_cmd="config";
        return;
      }
      std::cout<<trFmt("config_get_value", {{"key", key}, {"value", value}})<<"\n";
    }},
    SubcommandSpec{"set", {}, {"<key>","<value>"}, {}, [](const std::vector<std::string>& a){
      if(a.size()<4){
        std::cout<<tr("config_set_usage")<<"\n";
        g_parse_error_cmd="config";
        return;
      }
      std::string error;
      const std::string& key = a[2];
      const std::string& value = a[3];
      if(!config_set_value(key, value, error)){
        if(error=="unknown_key"){
          std::cout<<trFmt("config_unknown_key", {{"key", key}})<<"\n";
        }else{
          std::cout<<trFmt("config_invalid_value", {{"key", key}, {"value", value}})<<"\n";
        }
        g_parse_error_cmd="config";
        return;
      }
      save_config(config_file_path());
      std::cout<<trFmt("config_set_success", {{"key", key}, {"value", value}})<<"\n";
    }},
    SubcommandSpec{"list", {}, {}, {}, [](const std::vector<std::string>&){
      std::cout<<tr("config_list_header")<<"\n";
      auto keys = config_list_keys();
      for(auto &k : keys){
        std::string value; config_get_value(k, value);
        std::cout<<"  "<<k<<" = "<<value<<"\n";
      }
    }}
  };
  t.handler = [](const std::vector<std::string>&){
    std::cout<<tr("config_usage")<<"\n";
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
      if(config_set_value("prompt.cwd", mode, error)){
        save_config(config_file_path());
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
  REG.registerTool(make_config());
  REG.registerTool(make_run());
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