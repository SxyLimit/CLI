#pragma once
#include "globals.hpp"
#include <system_error>
#include <thread>
#include <cctype>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <map>
#include <set>

#include "tools/tool_common.hpp"
#include "tools/clear.hpp"
#include "tools/run.hpp"
#include "tools/llm.hpp"
#include "tools/message.hpp"
#include "tools/cd.hpp"
#include "tools/ls.hpp"
#include "tools/cat.hpp"
#include "tools/mv.hpp"
#include "tools/rm.hpp"

inline std::vector<std::string> render_mycli_ascii_art(){
  return {
    " __  __        _____ _     ___ ",
    "|  \\/  |__ _ _|_   _| |__ |_ _|",
    "| |\\/| / _` / -_)| | | '_ \\ | | ",
    "|_|  |_|\\__,_\\___||_|_|_.__/|___|"
  };
}

// ===== Path candidates (inline) =====
inline Candidates pathCandidatesForWord(const std::string& fullBuf,
                                        const std::string& word,
                                        PathKind kind,
                                        const std::vector<std::string>* extensions,
                                        bool allowDirectories){
  Candidates out;

  auto isSep = [](char ch){ return ch == '/' || ch == '\\'; };

  std::string dir;
  std::string base;
  if(!word.empty() && isSep(word.back())){
    dir = word;
    base.clear();
  }else{
    size_t p = word.find_last_of("/\\");
    if(p == std::string::npos){
      base = word;
    }else{
      dir = word.substr(0, p + 1);
      base = word.substr(p + 1);
    }
  }

  std::filesystem::path rootPath = dir.empty() ? std::filesystem::path(".") : std::filesystem::path(dir);
  std::error_code ec;
  std::filesystem::directory_iterator it(rootPath, ec);
  if(ec) return out;

  auto sw = splitLastWord(fullBuf);
  auto pickSep = [&](char fallback){
    if(!dir.empty() && isSep(dir.back())) return dir.back();
    for(char ch : dir){
      if(isSep(ch)) return ch;
    }
    return fallback;
  };
  char preferredSep = pickSep(std::filesystem::path::preferred_separator);

  const std::vector<std::string>* extList = (extensions && !extensions->empty()) ? extensions : nullptr;
  std::vector<std::string> normalizedExts;
  std::string extensionHint;
  if(extList){
    normalizedExts.reserve(extList->size());
    for(const auto& rawExt : *extList){
      if(rawExt.empty()) continue;
      std::string norm = rawExt;
      if(norm.front() != '.') norm.insert(norm.begin(), '.');
      std::transform(norm.begin(), norm.end(), norm.begin(), [](unsigned char c){
        return static_cast<char>(std::tolower(c));
      });
      if(!norm.empty()) normalizedExts.push_back(norm);
    }
    std::sort(normalizedExts.begin(), normalizedExts.end());
    normalizedExts.erase(std::unique(normalizedExts.begin(), normalizedExts.end()), normalizedExts.end());
    if(!normalizedExts.empty()){
      extensionHint = "[" + join(normalizedExts, "|") + "]";
    }else{
      extList = nullptr;
    }
  }

  auto matchesExtension = [&](const std::string& candidateName){
    if(!extList) return true;
    auto pos = candidateName.find_last_of('.');
    if(pos == std::string::npos) return false;
    std::string ext = candidateName.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){
      return static_cast<char>(std::tolower(c));
    });
    return std::find(normalizedExts.begin(), normalizedExts.end(), ext) != normalizedExts.end();
  };

  for(const auto& entry : it){
    std::string name = entry.path().filename().string();
    if(name == "." || name == "..") continue;

    MatchResult match = compute_match(name, base);
    if(!base.empty() && !match.matched) continue;
    if(base.empty() && !match.matched) match.matched = true;

    std::error_code statusEc;
    bool isDir = entry.is_directory(statusEc);
    if(statusEc){ statusEc.clear(); isDir = false; }
    bool isFile = entry.is_regular_file(statusEc);
    if(statusEc){ statusEc.clear(); isFile = false; }

    bool include = false;
    bool dirAsHint = false;
    if(isDir){
      if(!allowDirectories) continue;
      if(kind == PathKind::Dir){
        include = true;
      }else if(kind == PathKind::File){
        include = true;
        dirAsHint = true;
      }else{ // PathKind::Any
        include = true;
      }
    }else if(isFile){
      if(kind == PathKind::Dir) continue;
      if(!matchesExtension(name)) continue;
      include = true;
    }
    if(!include) continue;

    std::string cand = dir + name;
    if(isDir){
      if(cand.empty() || !isSep(cand.back())) cand.push_back(preferredSep);
    }

    std::vector<int> positions;
    for(size_t i=0;i<dir.size();++i) positions.push_back(static_cast<int>(i));
    for(int pos : match.positions) positions.push_back(static_cast<int>(dir.size() + pos));
    std::sort(positions.begin(), positions.end());

    out.items.push_back(sw.before + cand);
    out.labels.push_back(cand);
    out.matchPositions.push_back(std::move(positions));
    std::string annotation;
    if(dirAsHint) annotation = "[dir]";
    if(!extensionHint.empty()){
      if(!annotation.empty()) annotation += " ";
      annotation += extensionHint;
    }
    out.annotations.push_back(std::move(annotation));
    out.exactMatches.push_back(match.exact);
    out.matchDetails.push_back(match);
  }

  std::vector<size_t> idx(out.labels.size());
  for(size_t i=0;i<idx.size();++i) idx[i]=i;
  std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b){
    bool ea = (a < out.exactMatches.size()) ? out.exactMatches[a] : false;
    bool eb = (b < out.exactMatches.size()) ? out.exactMatches[b] : false;
    if(ea != eb) return ea > eb;
    return out.labels[a] < out.labels[b];
  });
  Candidates s;
  s.items.reserve(idx.size());
  s.labels.reserve(idx.size());
  s.annotations.reserve(idx.size());
  for(size_t k : idx){
    s.items.push_back(out.items[k]);
    s.labels.push_back(out.labels[k]);
    s.matchPositions.push_back(out.matchPositions[k]);
    s.annotations.push_back(out.annotations[k]);
    if(k < out.exactMatches.size()) s.exactMatches.push_back(out.exactMatches[k]);
    else s.exactMatches.push_back(false);
    if(k < out.matchDetails.size()) s.matchDetails.push_back(out.matchDetails[k]);
    else s.matchDetails.emplace_back();
  }
  sortCandidatesByMatch(base, s);
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
      out += " " + spec.positional[k].placeholder;
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
  for (auto &ph : sub.positional) out += " " + ph.placeholder;
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

namespace tool {

struct Show {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "show";
    spec.summary = "Show system information";
    set_tool_summary_locale(spec, "en", "Show system information");
    set_tool_summary_locale(spec, "zh", "显示系统信息");
    set_tool_help_locale(spec, "en", "Use `show LICENSE` or `show MyCLI` to inspect bundled information.");
    set_tool_help_locale(spec, "zh", "使用 `show LICENSE` 或 `show MyCLI` 查看内置信息。");
    spec.subs = {
      SubcommandSpec{"LICENSE", {}, {}, {}, nullptr},
      SubcommandSpec{"MyCLI", {}, {}, {}, nullptr}
    };
    spec.handler = nullptr;
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    ToolExecutionResult result;
    const auto& args = request.tokens;
    if(args.size() < 2){
      g_parse_error_cmd = "show";
      result.exitCode = 1;
      result.output = tr("show_usage") + "\n";
      result.display = result.output;
      return result;
    }
    const std::string sub = args[1];
    if(sub == "LICENSE"){
      std::ifstream in("LICENSE");
      if(!in.good()){
        result.exitCode = 1;
        result.output = tr("show_license_error") + "\n";
        result.display = result.output;
        return result;
      }
      std::ostringstream oss;
      oss << in.rdbuf();
      if(in.bad()){
        result.exitCode = 1;
        result.output = tr("show_license_error") + "\n";
        result.display = result.output;
        return result;
      }
      std::string content = oss.str();
      if(!content.empty() && content.back() != '\n') content.push_back('\n');
      result.output = content;
      result.display = content;
      return result;
    }
    if(sub == "MyCLI"){
      std::ostringstream oss;
      oss << tr("show_mycli_version") << "\n\n";
      for(const auto& line : render_mycli_ascii_art()){
        oss << line << "\n";
      }
      result.output = oss.str();
      result.display = result.output;
      return result;
    }
    g_parse_error_cmd = "show";
    result.exitCode = 1;
    result.output = tr("show_usage") + "\n";
    result.display = result.output;
    return result;
  }
};

inline std::vector<std::string> split_setting_key(const std::string& key){
  std::vector<std::string> parts;
  std::string current;
  for(char ch : key){
    if(ch == '.'){
      if(!current.empty()) parts.push_back(current);
      current.clear();
    }else{
      current.push_back(ch);
    }
  }
  if(!current.empty()) parts.push_back(current);
  return parts;
}

inline std::string join_setting_segments(const std::vector<std::string>& segs){
  std::string key;
  for(size_t i = 0; i < segs.size(); ++i){
    if(i) key.push_back('.');
    key += segs[i];
  }
  return key;
}

inline std::set<std::string> next_setting_segments(const std::vector<std::string>& prefix){
  std::set<std::string> result;
  auto keys = settings_list_keys();
  for(const auto& key : keys){
    auto parts = split_setting_key(key);
    if(parts.size() < prefix.size()) continue;
    bool match = true;
    for(size_t i = 0; i < prefix.size(); ++i){
      if(parts[i] != prefix[i]){
        match = false;
        break;
      }
    }
    if(!match) continue;
    if(parts.size() == prefix.size()) continue;
    result.insert(parts[prefix.size()]);
  }
  return result;
}

struct Setting {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "setting";
    spec.summary = "Manage CLI settings";
    set_tool_summary_locale(spec, "en", "Manage CLI settings");
    set_tool_summary_locale(spec, "zh", "管理 CLI 设置");
    set_tool_help_locale(spec, "en", "setting <segments...> [list|get|set <value>]");
    set_tool_help_locale(spec, "zh", "setting <分段...> [list|get|set <值>]");
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 2){
      g_parse_error_cmd = "setting";
      return detail::text_result("usage: setting <path...> <list|get|set> [value]\n", 1);
    }
    std::vector<std::string> segments;
    std::string action;
    size_t actionIndex = 0;
    for(size_t i = 1; i < args.size(); ++i){
      if(action.empty() && (args[i] == "list" || args[i] == "get" || args[i] == "set")){
        action = args[i];
        actionIndex = i;
        break;
      }
      segments.push_back(args[i]);
    }
    if(action.empty()){
      action = "list";
    }
    std::string key = join_setting_segments(segments);
    if(action == "list"){
      auto keys = settings_list_keys();
      std::ostringstream oss;
      oss << tr("setting_list_header") << "\n";
      for(const auto& item : keys){
        if(!segments.empty()){
          if(item.rfind(key, 0) != 0) continue;
        }
        std::string value;
        settings_get_value(item, value);
        oss << "  " << item << " = " << value << "\n";
      }
      return detail::text_result(oss.str());
    }
    if(segments.empty()){
      g_parse_error_cmd = "setting";
      return detail::text_result("setting: key required\n", 1);
    }
    if(action == "get"){
      std::string value;
      if(!settings_get_value(key, value)){
        g_parse_error_cmd = "setting";
        return detail::text_result(trFmt("setting_unknown_key", {{"key", key}}) + "\n", 1);
      }
      return detail::text_result(trFmt("setting_get_value", {{"key", key}, {"value", value}}) + "\n");
    }
    if(action == "set"){
      if(actionIndex + 1 >= args.size()){
        g_parse_error_cmd = "setting";
        return detail::text_result("usage: setting <path...> set <value>\n", 1);
      }
      std::string value;
      for(size_t i = actionIndex + 1; i < args.size(); ++i){
        if(i > actionIndex + 1) value.push_back(' ');
        value += args[i];
      }
      std::string error;
      if(!settings_set_value(key, value, error)){
        g_parse_error_cmd = "setting";
        if(error == "unknown_key"){
          return detail::text_result(trFmt("setting_unknown_key", {{"key", key}}) + "\n", 1);
        }
        return detail::text_result(trFmt("setting_invalid_value", {{"key", key}, {"value", value}}) + "\n", 1);
      }
      save_settings(settings_file_path());
      return detail::text_result(trFmt("setting_set_success", {{"key", key}, {"value", value}}) + "\n");
    }
    g_parse_error_cmd = "setting";
    return detail::text_result("usage: setting <path...> <list|get|set> [value]\n", 1);
  }

  static Candidates complete(const std::string& buffer, const std::vector<std::string>& tokens){
    Candidates cand;
    if(tokens.empty()) return cand;
    SplitWord sw = splitLastWord(buffer);
    bool endsWithSpace = !buffer.empty() && std::isspace(static_cast<unsigned char>(buffer.back()));
    std::string current = sw.word;
    std::vector<std::string> rest(tokens.begin() + 1, tokens.end());
    std::string action;
    size_t actionIndex = rest.size();
    const std::set<std::string> actions = {"list", "get", "set"};
    for(size_t i = 0; i < rest.size(); ++i){
      if(actions.count(rest[i])){
        action = rest[i];
        actionIndex = i;
        break;
      }
    }
    std::vector<std::string> prefix;
    if(action.empty()){
      prefix = rest;
      if(!endsWithSpace && !prefix.empty()) prefix.pop_back();
    }else{
      prefix.assign(rest.begin(), rest.begin() + actionIndex);
    }
    auto addCandidate = [&](const std::string& label, bool appendSpace){
      std::string item = sw.before + label;
      if(appendSpace) item += ' ';
      MatchResult match = compute_match(label, current);
      if(!endsWithSpace && match.exact && label == current) return;
      cand.items.push_back(item);
      cand.labels.push_back(label);
      cand.matchDetails.push_back(match);
      cand.matchPositions.push_back(match.positions);
      cand.annotations.push_back("");
      cand.exactMatches.push_back(match.exact);
    };

    if(action.empty()){
      auto segments = next_setting_segments(prefix);
      for(const auto& seg : segments){
        addCandidate(seg, true);
      }
      if(!prefix.empty()){
        for(const auto& act : actions){
          addCandidate(act, true);
        }
      }
    }else if(!endsWithSpace && actionIndex == rest.size() - 1){
      for(const auto& act : actions){
        addCandidate(act, true);
      }
    }else if(action == "set"){
      std::string key = join_setting_segments(prefix);
      auto suggestions = settings_value_suggestions_for(key);
      for(const auto& suggestion : suggestions){
        addCandidate(suggestion, false);
      }
    }
    sortCandidatesByMatch(current, cand);
    return cand;
  }

};

struct Exit {
  static ToolSpec ui(const std::string& name){
    ToolSpec spec;
    spec.name = name;
    spec.summary = "Exit the shell";
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    (void)request;
    g_should_exit = true;
    ToolExecutionResult result;
    result.exitCode = 0;
    return result;
  }
};

inline ToolDefinition make_show_tool(){
  ToolDefinition def;
  def.ui = Show::ui();
  def.executor = Show::run;
  return def;
}

inline ToolDefinition make_setting_tool(){
  ToolDefinition def;
  def.ui = Setting::ui();
  def.executor = Setting::run;
  def.completion = Setting::complete;
  return def;
}

inline ToolDefinition make_exit_tool(const std::string& name){
  ToolDefinition def;
  def.ui = Exit::ui(name);
  def.executor = Exit::run;
  return def;
}

} // namespace tool

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
    std::string summary, help, type="system", exec, script, options, positional, optionPaths, positionalPaths;
    std::map<std::string, std::string> summaryLocales;
    std::map<std::string, std::string> helpLocales;
    std::map<std::string, std::vector<std::string>> optvalues;
    struct TmpSub {
      std::string name, options, positional, optionPaths, positionalPaths;
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
      else if(k=="help") T.help = v;
      else if(k.rfind("summary.",0)==0){
        auto lang = k.substr(8);
        if(!lang.empty()) T.summaryLocales[lang] = v;
      }
      else if(k.rfind("help.",0)==0){
        auto lang = k.substr(5);
        if(!lang.empty()) T.helpLocales[lang] = v;
      }
      else if(k=="type") T.type = v;
      else if(k=="exec") T.exec = v;
      else if(k=="script") T.script = v;
      else if(k=="options") T.options = v;
      else if(k=="positional") T.positional = v;
      else if(k=="optionPaths") T.optionPaths = v;
      else if(k=="positionalPaths") T.positionalPaths = v;
      else if(k.rfind("optvalues.",0)==0){
        auto on = k.substr(10); T.optvalues[on] = splitCSV(v);
      }
    }else{
      auto& S = T.subs[sub]; S.name=sub;
      if(k=="options") S.options = v;
      else if(k=="positional") S.positional = v;
      else if(k=="optionPaths") S.optionPaths = v;
      else if(k=="positionalPaths") S.positionalPaths = v;
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
  auto splitByChar = [&](const std::string& text, char delim){
    std::vector<std::string> parts;
    std::string cur;
    for(char c : text){
      if(c == delim){
        auto token = trim(cur);
        if(!token.empty()) parts.push_back(token);
        cur.clear();
      }else{
        cur.push_back(c);
      }
    }
    auto token = trim(cur);
    if(!token.empty()) parts.push_back(token);
    return parts;
  };

  auto toLowerCopy = [](std::string value){
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c){
      return static_cast<char>(std::tolower(c));
    });
    return value;
  };

  auto parseKind = [&](const std::string& token){
    std::string lower = toLowerCopy(token);
    if(lower == "dir" || lower == "directory" || lower == "d") return PathKind::Dir;
    if(lower == "file" || lower == "f") return PathKind::File;
    if(lower == "any" || lower == "path" || lower == "a") return PathKind::Any;
    return PathKind::Any;
  };

  auto applyPositionalHints = [&](std::vector<PositionalArgSpec>& dest, const std::string& specText){
    if(specText.empty()) return;
    for(const auto& entry : splitCSV(specText)){
      if(entry.empty()) continue;
      auto parts = splitByChar(entry, ':');
      if(parts.empty()) continue;
      size_t idx = 0;
      try{
        idx = static_cast<size_t>(std::stoul(parts[0]));
      }catch(...){
        continue;
      }
      if(idx == 0 || idx > dest.size()) continue;
      PositionalArgSpec& arg = dest[idx - 1];
      arg.isPath = true;
      if(parts.size() >= 2 && !parts[1].empty()){
        arg.pathKind = parseKind(parts[1]);
      }
      if(parts.size() >= 3){
        arg.allowedExtensions = splitByChar(parts[2], '|');
        if(!arg.allowedExtensions.empty() && arg.pathKind == PathKind::Any){
          arg.pathKind = PathKind::File;
        }
      }
    }
  };

  struct OptionPathHint {
    PathKind kind = PathKind::Any;
    std::vector<std::string> extensions;
  };

  auto parseOptionPathMap = [&](const std::string& text){
    std::map<std::string, OptionPathHint> hints;
    for(const auto& entry : splitCSV(text)){
      if(entry.empty()) continue;
      auto parts = splitByChar(entry, ':');
      if(parts.empty()) continue;
      OptionPathHint hint;
      if(parts.size() >= 2 && !parts[1].empty()){
        hint.kind = parseKind(parts[1]);
      }
      if(parts.size() >= 3){
        hint.extensions = splitByChar(parts[2], '|');
        if(!hint.extensions.empty() && hint.kind == PathKind::Any){
          hint.kind = PathKind::File;
        }
      }
      hints[parts[0]] = std::move(hint);
    }
    return hints;
  };

  for(auto& kv : all){
    const std::string& name = kv.first;
    TmpTool& T = kv.second;

    ToolSpec tool; tool.name = name; tool.summary = T.summary;
    tool.summaryLocales = T.summaryLocales;
    tool.help = T.help;
    tool.helpLocales = T.helpLocales;

    auto optionPathHints = parseOptionPathMap(T.optionPaths);
    for(auto& oname : splitCSV(T.options)){
      OptionSpec o; o.name = oname;
      if(T.optvalues.count(oname)){ o.takesValue = true; o.valueSuggestions = T.optvalues[oname]; }
      if(optionPathHints.count(oname)){
        o.takesValue = true;
        o.isPath = true;
        const auto& hint = optionPathHints[oname];
        o.pathKind = hint.kind;
        o.allowedExtensions = hint.extensions;
        if(o.placeholder.empty()) o.placeholder = "<path>";
      }
      tool.options.push_back(o);
    }
    if(!T.positional.empty()){
      for(const auto& token : splitTokens(T.positional)){
        tool.positional.push_back(tool::positional(token));
      }
      applyPositionalHints(tool.positional, T.positionalPaths);
    }

    if(!T.subs.empty()){
      for(const auto& skv : T.subs){
        const auto& S = skv.second;
        SubcommandSpec sub;
        sub.name = S.name;
        auto subPathHints = parseOptionPathMap(S.optionPaths);
        for(auto& oname : splitCSV(S.options)){
          OptionSpec o; o.name = oname;
          if(S.optvalues.count(oname)){ o.takesValue = true; o.valueSuggestions = S.optvalues.at(oname); }
          if(subPathHints.count(oname)){
            o.takesValue = true;
            o.isPath = true;
            const auto& hint = subPathHints[oname];
            o.pathKind = hint.kind;
            o.allowedExtensions = hint.extensions;
            if(o.placeholder.empty()) o.placeholder="<path>";
          }
          sub.options.push_back(o);
        }
        for(const auto& token : splitTokens(S.positional)){
          sub.positional.push_back(tool::positional(token));
        }
        applyPositionalHints(sub.positional, S.positionalPaths);
        sub.mutexGroups = S.mutexGroups;
        tool.subs.push_back(std::move(sub));
      }
    }

    ToolDefinition def;
    def.ui = tool;
    if(!T.subs.empty()){
      auto subs = T.subs;
      std::string type = T.type;
      std::string exec = T.exec;
      std::string script = T.script;
      def.executor = [name, type, exec, script, subs](const ToolExecutionRequest& req){
        ToolExecutionResult res;
        if(req.tokens.size() < 2){
          res.exitCode = 1;
          res.output = "usage: " + name + " <subcommand> [options]\n";
          res.display = res.output;
          g_parse_error_cmd = name;
          return res;
        }
        const std::string& subName = req.tokens[1];
        auto it = subs.find(subName);
        if(it == subs.end()){
          res.exitCode = 1;
          res.output = "unknown subcommand: " + subName + "\n";
          res.display = res.output;
          g_parse_error_cmd = name;
          return res;
        }
        std::string cmd;
        if(type == "python"){
          if(exec.empty() || script.empty()){
            res.exitCode = 1;
            res.output = "python tool not configured\n";
            res.display = res.output;
            g_parse_error_cmd = name;
            return res;
          }
          cmd = exec + " " + script + " " + subName;
        }else{
          cmd = exec.empty()? name : exec;
          if(!subName.empty()) cmd += " " + subName;
        }
        for(size_t i = 2; i < req.tokens.size(); ++i){
          cmd += " ";
          cmd += req.tokens[i];
        }
        auto execResult = tool::detail::execute_shell(req, cmd);
        if(execResult.exitCode != 0){
          g_parse_error_cmd = name;
        }
        return execResult;
      };
    }else{
      std::string type = T.type;
      std::string exec = T.exec;
      std::string script = T.script;
      def.executor = [name, type, exec, script](const ToolExecutionRequest& req){
        std::string cmd;
        ToolExecutionResult res;
        if(type == "python"){
          if(exec.empty() || script.empty()){
            res.exitCode = 1;
            res.output = "python tool not configured\n";
            res.display = res.output;
            g_parse_error_cmd = name;
            return res;
          }
          cmd = exec + " " + script;
        }else{
          cmd = exec.empty()? name : exec;
        }
        for(size_t i = 1; i < req.tokens.size(); ++i){
          cmd += " ";
          cmd += req.tokens[i];
        }
        auto execResult = tool::detail::execute_shell(req, cmd);
        if(execResult.exitCode != 0){
          g_parse_error_cmd = name;
        }
        return execResult;
      };
    }

    REG.registerTool(def);
  }
}

// =================== Register All ===================
inline void register_all_tools(){
  REG.registerTool(tool::make_show_tool());
  REG.registerTool(tool::make_clear_tool());
  REG.registerTool(tool::make_setting_tool());
  REG.registerTool(tool::make_run_tool());
  REG.registerTool(tool::make_llm_tool());
  REG.registerTool(tool::make_message_tool());
  REG.registerTool(tool::make_cd_tool());
  REG.registerTool(tool::make_ls_tool());
  REG.registerTool(tool::make_cat_tool());
  REG.registerTool(tool::make_mv_tool());
  REG.registerTool(tool::make_rm_tool());
  REG.registerTool(tool::make_exit_tool("exit"));
  REG.registerTool(tool::make_exit_tool("quit"));
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

inline void register_status_providers(){
  REG.registerStatusProvider(make_cwd_status());
}
