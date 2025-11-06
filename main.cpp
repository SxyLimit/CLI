#include <termios.h>
#include <unistd.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <clocale>
#include <codecvt>
#include <cwchar>
#include <locale>
#include <stdexcept>
#include <cctype>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <optional>
#include <unordered_map>

#include "globals.hpp"
#include "tools.hpp"
#include "config.hpp"

// ===== Global state definitions =====
ToolRegistry REG;
CwdMode      g_cwd_mode = CwdMode::Full;
bool         g_should_exit = false;
std::string  g_parse_error_cmd;

static const std::string kConfigPath = "./mycli_config.json";

const std::string& config_file_path(){ return kConfigPath; }

static std::unordered_map<std::string, std::map<std::string, std::string>> g_i18n = {
  {"show_config_output", {{"en", "theme = default\nmodel = alpha\n"}, {"zh", "主题 = default\n模型 = alpha\n"}}},
  {"show_logs_output",   {{"en", "3 fake log lines...\n2 fake log lines...\n1 fake log line...\n"}, {"zh", "3 行模拟日志...\n2 行模拟日志...\n1 行模拟日志...\n"}}},
  {"show_usage",         {{"en", "usage: show [config|logs]"}, {"zh", "用法：show [config|logs]"}}},
  {"config_get_usage",   {{"en", "usage: config get <key>"}, {"zh", "用法：config get <key>"}}},
  {"config_unknown_key", {{"en", "unknown config key: {key}"}, {"zh", "未知配置项：{key}"}}},
  {"config_get_value",   {{"en", "config {key} = {value}"}, {"zh", "配置 {key} = {value}"}}},
  {"config_set_usage",   {{"en", "usage: config set <key> <value>"}, {"zh", "用法：config set <key> <value>"}}},
  {"config_invalid_value", {{"en", "invalid value for {key}: {value}"}, {"zh", "配置 {key} 的值无效：{value}"}}},
  {"config_set_success", {{"en", "updated {key} -> {value}"}, {"zh", "已更新 {key} -> {value}"}}},
  {"config_list_header", {{"en", "Available config keys:"}, {"zh", "可用配置项："}}},
  {"config_usage",       {{"en", "usage: config <get|set|list>"}, {"zh", "用法：config <get|set|list>"}}},
  {"cd_mode_updated",    {{"en", "prompt cwd mode set to {mode}"}, {"zh", "提示符目录模式已设为 {mode}"}}},
  {"cd_mode_error",      {{"en", "failed to update prompt mode"}, {"zh", "更新提示符模式失败"}}},
  {"cd_usage",           {{"en", "usage: cd <path> | cd -o [-a|-c]"}, {"zh", "用法：cd <path> | cd -o [-a|-c]"}}},
  {"mode.full",          {{"en", "full"}, {"zh", "完整"}}},
  {"mode.omit",          {{"en", "omit"}, {"zh", "仅名称"}}},
  {"mode.hidden",        {{"en", "hidden"}, {"zh", "隐藏"}}},
  {"help_available_commands", {{"en", "Available commands:"}, {"zh", "可用命令："}}},
  {"help_command_summary",   {{"en", "  help  - Show command help"}, {"zh", "  help  - 显示命令帮助"}}},
  {"help_use_command",       {{"en", "Use: help <command> to see details."}, {"zh", "使用：help <command> 查看详情。"}}},
  {"help_no_such_command",   {{"en", "No such command: {name}"}, {"zh", "没有名为 {name} 的命令"}}},
  {"help_subcommands",       {{"en", "  subcommands:"}, {"zh", "  子命令："}}},
  {"help_options",           {{"en", "  options:"}, {"zh", "  选项："}}},
  {"help_positional",        {{"en", "  positional: {value}"}, {"zh", "  位置参数：{value}"}}},
  {"help_required_tag",      {{"en", " (required)"}, {"zh", "（必填）"}}},
  {"help_path_tag",          {{"en", " (path)"}, {"zh", "（路径）"}}},
  {"help_usage_line",        {{"en", "usage: {value}"}, {"zh", "用法：{value}"}}},
  {"unknown_command",        {{"en", "unknown command: {name}"}, {"zh", "未知命令：{name}"}}},
  {"path_error_missing",     {{"en", "missing"}, {"zh", "不存在"}}},
  {"path_error_need_dir",    {{"en", "needs directory"}, {"zh", "需要目录"}}},
  {"path_error_need_file",   {{"en", "needs file"}, {"zh", "需要文件"}}}
};

void set_tool_summary_locale(ToolSpec& spec, const std::string& lang, const std::string& value){
  spec.summaryLocales[lang] = value;
  if(lang=="en" && spec.summary.empty()) spec.summary = value;
}

std::string localized_tool_summary(const ToolSpec& spec){
  auto it = spec.summaryLocales.find(g_config.language);
  if(it!=spec.summaryLocales.end() && !it->second.empty()) return it->second;
  auto en = spec.summaryLocales.find("en");
  if(en!=spec.summaryLocales.end() && !en->second.empty()) return en->second;
  return spec.summary;
}

std::string tr(const std::string& key){
  auto it = g_i18n.find(key);
  if(it!=g_i18n.end()){
    auto jt = it->second.find(g_config.language);
    if(jt!=it->second.end()) return jt->second;
    jt = it->second.find("en");
    if(jt!=it->second.end()) return jt->second;
  }
  return key;
}

std::string trFmt(const std::string& key, const std::map<std::string, std::string>& values){
  std::string base = tr(key);
  std::string out;
  out.reserve(base.size()+32);
  for(size_t i=0;i<base.size();){
    if(base[i]=='{' ){
      size_t end = base.find('}', i+1);
      if(end!=std::string::npos){
        std::string var = base.substr(i+1, end-i-1);
        auto it = values.find(var);
        if(it!=values.end()) out += it->second;
        else out += base.substr(i, end-i+1);
        i = end+1;
        continue;
      }
    }
    out.push_back(base[i]);
    ++i;
  }
  return out;
}

MatchResult compute_match(const std::string& candidate, const std::string& pattern){
  MatchResult res;
  if(pattern.empty()){
    res.matched = true;
    return res;
  }
  bool ignoreCase = g_config.completionIgnoreCase;
  bool subseq = g_config.completionSubsequence;
  if(subseq){
    size_t p = 0;
    for(size_t i=0;i<candidate.size() && p<pattern.size();++i){
      char c = candidate[i];
      char pc = pattern[p];
      if(ignoreCase){
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        pc = static_cast<char>(std::tolower(static_cast<unsigned char>(pc)));
      }
      if(c==pc){
        res.positions.push_back(static_cast<int>(i));
        ++p;
      }
    }
    if(p==pattern.size()){
      res.matched = true;
      return res;
    }
    res.positions.clear();
  }
  if(pattern.size()>candidate.size()) return res;
  for(size_t i=0;i<pattern.size();++i){
    char c = candidate[i];
    char pc = pattern[i];
    if(ignoreCase){
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      pc = static_cast<char>(std::tolower(static_cast<unsigned char>(pc)));
    }
    if(c!=pc) return res;
    res.positions.push_back(static_cast<int>(i));
  }
  res.matched = true;
  return res;
}

// ===== Prompt params =====
static const std::string PLAIN_PROMPT = "mycli> ";
static const int extraLines = 3;

static int displayWidth(const std::string& text){
  static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
  std::wstring ws;
  try {
    ws = conv.from_bytes(text);
  } catch (const std::range_error&) {
    return static_cast<int>(text.size());
  }
  int width = 0;
  for (wchar_t ch : ws) {
    int w = ::wcwidth(ch);
    if (w < 0) w = 1;
    width += w;
  }
  return width;
}

static std::string renderHighlightedLabel(const std::string& label, const std::vector<int>& positions){
  std::string out;
  out.reserve(label.size()*4);
  size_t idx = 0;
  int state = 0; // 0 none, 1 white, 2 gray
  auto flush = [&](int next){
    if(state==next) return;
    if(state!=0) out += ansi::RESET;
    if(next==1) out += ansi::WHITE;
    else if(next==2) out += ansi::GRAY;
    state = next;
  };
  for(size_t i=0;i<label.size();++i){
    bool matched = (idx < positions.size() && positions[idx]==static_cast<int>(i));
    flush(matched?1:2);
    out.push_back(label[i]);
    if(matched) ++idx;
  }
  if(state!=0) out += ansi::RESET;
  return out;
}

static int highlightCursorOffset(const std::string& label, const std::vector<int>& positions){
  if(positions.empty()) return 0;
  int last = positions.back();
  if(last < 0) return 0;
  std::string prefix = label.substr(0, static_cast<size_t>(last)+1);
  return displayWidth(prefix);
}

static int promptDisplayWidth(){
  static int width = displayWidth(PLAIN_PROMPT);
  return width;
}

// helper: 是否“路径型占位符”
static bool isPathLikePlaceholder(const std::string& ph){
  std::string t = ph; std::transform(t.begin(), t.end(), t.begin(), ::tolower);
  // 支持 <path> / <file> / <dir> 及其变体 [<path>]、<path...> 等
  return t.find("<path")!=std::string::npos || t.find("<file")!=std::string::npos || t.find("<dir")!=std::string::npos;
}

static PathKind placeholderPathKind(const std::string& ph){
  std::string t = ph; std::transform(t.begin(), t.end(), t.begin(), ::tolower);
  if(t.find("<file") != std::string::npos) return PathKind::File;
  if(t.find("<dir")  != std::string::npos) return PathKind::Dir;
  return PathKind::Any;
}

struct PathCompletionContext {
  bool active = false;
  bool appliesToCurrentWord = false;
  PathKind kind = PathKind::Any;
};

static PathCompletionContext analyzePositionalPathContext(const std::vector<std::string>& posDefs,
                                                         size_t startIdx,
                                                         const std::vector<OptionSpec>& opts,
                                                         const std::vector<std::string>& toks,
                                                         const SplitWord& sw,
                                                         const std::string& buf){
  PathCompletionContext ctx;
  if(posDefs.empty()) return ctx;

  bool trailingSpace = (!buf.empty() && std::isspace(static_cast<unsigned char>(buf.back())));
  if(!trailingSpace && startIdx >= toks.size()) return ctx;

  size_t i = startIdx;
  size_t posFilled = 0;
  bool currentWordIsPositional = false;

  while(i < toks.size()){
    const std::string& tk = toks[i];
    if(!tk.empty() && tk[0]=='-'){
      bool takes=false;
      size_t advance = 1;
      for(auto &o : opts){
        if(o.name == tk){
          takes = o.takesValue;
          if(takes && i + 1 < toks.size()){
            bool valueIsCurrent = (!trailingSpace && i + 1 == toks.size() - 1 && toks.back() == sw.word);
            if(valueIsCurrent){
              currentWordIsPositional = false;
            }
            advance = 2;
          }
          break;
        }
      }
      i += advance;
      continue;
    }

    bool isCurrentWord = (!trailingSpace && i == toks.size() - 1 && toks[i] == sw.word);
    if(isCurrentWord){
      currentWordIsPositional = true;
      break;
    }
    posFilled += 1;
    i += 1;
  }

  if(!(trailingSpace || currentWordIsPositional)) return ctx;

  if(posFilled < posDefs.size() && isPathLikePlaceholder(posDefs[posFilled])){
    ctx.active = true;
    ctx.appliesToCurrentWord = currentWordIsPositional;
    PathKind kind = placeholderPathKind(posDefs[posFilled]);
    ctx.kind = kind;
  }
  return ctx;
}

static bool inSubcommandSlot(const ToolSpec& spec, const std::vector<std::string>& toks){
  if (spec.subs.empty()) return false;
  if (toks.size()==1) return true;
  if (toks.size()>=2){
    for (auto &s : spec.subs) if (s.name==toks[1]) return false;
    return true;
  }
  return false;
}

static Candidates firstWordCandidates(const std::string& buf){
  Candidates out; auto sw=splitLastWord(buf);
  if(!sw.before.empty()) return out;

  auto names = REG.listNames();
  names.push_back("help");
  std::sort(names.begin(),names.end());
  names.erase(std::unique(names.begin(),names.end()), names.end());

  for(auto&s:names){
    MatchResult match = compute_match(s, sw.word);
    if(!match.matched) continue;
    out.items.push_back(sw.before + s);
    out.labels.push_back(s);
    out.matchPositions.push_back(match.positions);
  }
  return out;
}

static Candidates candidatesForTool(const ToolSpec& spec, const std::string& buf){
  Candidates out; auto sw=splitLastWord(buf); auto toks=splitTokens(buf);
  if(toks.empty() || toks[0]!=spec.name) return out;

  // 子命令名补全
  if (inSubcommandSlot(spec, toks)){
    for(auto &sub: spec.subs){
      MatchResult match = compute_match(sub.name, sw.word);
      if(!match.matched) continue;
      out.items.push_back(sw.before + sub.name);
      out.labels.push_back(sub.name);
      out.matchPositions.push_back(match.positions);
    }
    if(!out.items.empty()) return out;
  }

  // 是否子命令上下文
  auto findSub=[&]()->const SubcommandSpec*{
    if(toks.size()>=2){ for(auto &s: spec.subs) if(s.name==toks[1]) return &s; }
    return nullptr;
  };
  const SubcommandSpec* sub=findSub();

  if(spec.name == "config" && sub){
    auto positionalIndex = [&]()->std::optional<size_t>{
      bool trailingSpace = (!buf.empty() && std::isspace(static_cast<unsigned char>(buf.back())));
      size_t start = 2; // command + subcommand
      size_t count = 0;
      for(size_t i=start; i<toks.size(); ++i){
        bool isCurrent = (!trailingSpace && i + 1 == toks.size() && toks[i] == sw.word);
        if(isCurrent) return count;
        count++;
      }
      if(trailingSpace) return count;
      return std::nullopt;
    }();

    if(positionalIndex){
      size_t idx = *positionalIndex;
      if((sub->name=="get" || sub->name=="set") && idx==0){
        auto keys = config_list_keys();
        for(const auto& key : keys){
          MatchResult match = compute_match(key, sw.word);
          if(!match.matched) continue;
          out.items.push_back(sw.before + key);
          out.labels.push_back(key);
          out.matchPositions.push_back(match.positions);
        }
        if(!out.items.empty()) return out;
      }
      if(sub->name=="set" && idx==1){
        std::string keyName = (toks.size()>=3? toks[2] : "");
        auto values = config_value_suggestions_for(keyName);
        for(const auto& val : values){
          MatchResult match = compute_match(val, sw.word);
          if(!match.matched) continue;
          out.items.push_back(sw.before + val);
          out.labels.push_back(val);
          out.matchPositions.push_back(match.positions);
        }
        if(!out.items.empty()) return out;
      }
    }
  }

  // 值补全（包含路径型选项）
  if(toks.size()>=2){
    std::string prev = (toks.back()==sw.word && toks.size()>=2) ? toks[toks.size()-2] : (!toks.empty()? toks.back():"");
    auto tryValue = [&](const OptionSpec& o)->bool{
      if(o.name==prev && o.takesValue){
        if(o.isPath) {
          PathKind kind = (o.pathKind != PathKind::Any) ? o.pathKind : placeholderPathKind(o.placeholder);
          out = pathCandidatesForWord(buf, sw.word, kind);
          return true;
        }
        std::vector<std::string> vals = o.valueSuggestions;
        if(o.dynamicValues){ auto more=o.dynamicValues(toks); vals.insert(vals.end(), more.begin(), more.end()); }
        for(auto &v: vals){
          MatchResult match = compute_match(v, sw.word);
          if(!match.matched) continue;
          out.items.push_back(sw.before+v);
          out.labels.push_back(v);
          out.matchPositions.push_back(match.positions);
        }
        return true;
      } return false;
    };
    if(sub){ for(auto &o: sub->options) if(tryValue(o)) return out; }
    for(auto &o: spec.options) if(tryValue(o)) return out;
  }

  // 如果“下一个位置参数占位符”为路径型 → 直接进入路径补全（无需 ./ 或 /）
  auto positionalContext = [&](const std::vector<std::string>& posDefs, size_t startIdx, const std::vector<OptionSpec>& opts){
    return analyzePositionalPathContext(posDefs, startIdx, opts, toks, sw, buf);
  };

  if(sub){
    std::vector<OptionSpec> combinedOpts = spec.options;
    combinedOpts.insert(combinedOpts.end(), sub->options.begin(), sub->options.end());
    auto ctx = positionalContext(sub->positional, /*startIdx*/2, combinedOpts);
    if(ctx.active){
      return pathCandidatesForWord(buf, sw.word, ctx.kind);
    }
  }else{
    auto ctx = positionalContext(spec.positional, /*startIdx*/1, spec.options);
    if(ctx.active){
      return pathCandidatesForWord(buf, sw.word, ctx.kind);
    }
  }

  // 选项名补全
  if(startsWith(sw.word,"-")){
    std::set<std::string> used;
    for(size_t i=1;i<toks.size();++i) if(startsWith(toks[i],"--")||startsWith(toks[i],"-")) used.insert(toks[i]);
    auto addOpts = [&](const std::vector<OptionSpec>& opts){
      for(auto &o: opts){
        if(!used.count(o.name)){
          MatchResult match = compute_match(o.name, sw.word);
          if(!match.matched) continue;
          out.items.push_back(sw.before + o.name);
          out.labels.push_back(o.name);
          out.matchPositions.push_back(match.positions);
        }
      }
    };
    if(sub) addOpts(sub->options);
    addOpts(spec.options);
    if(!out.items.empty()) return out;
  }

  // 路径模式（兜底）
  if(startsWith(sw.word,"/")||startsWith(sw.word,"./")||startsWith(sw.word,"../")){
    return pathCandidatesForWord(buf, sw.word, PathKind::Any);
  }

  return out;
}

static Candidates computeCandidates(const std::string& buf){
  auto toks=splitTokens(buf);
  auto sw  = splitLastWord(buf);

  // help 二参补全
  if (!toks.empty() && toks[0] == "help") {
    Candidates out;
    if (toks.size()==1 || (toks.size()==2 && toks.back()==sw.word)) {
      auto names = REG.listNames();
      std::sort(names.begin(), names.end());
      names.erase(std::unique(names.begin(),names.end()), names.end());
      for (auto &n : names) {
        MatchResult match = compute_match(n, sw.word);
        if(!match.matched) continue;
        out.items.push_back(sw.before + n);
        out.labels.push_back(n);
        out.matchPositions.push_back(match.positions);
      }
    }
    return out;
  }

  if(toks.empty()) return firstWordCandidates(buf);
  if(const ToolSpec* spec = REG.find(toks[0])) return candidatesForTool(*spec, buf);
  return firstWordCandidates(buf);
}

static std::optional<std::string> detectPathErrorMessage(const std::string& buf, const Candidates& cand){
  auto toks = splitTokens(buf);
  auto sw   = splitLastWord(buf);
  if(toks.empty() || sw.word.empty()) return std::nullopt;
  if(!buf.empty() && std::isspace(static_cast<unsigned char>(buf.back()))) return std::nullopt;
  if(!g_config.showPathErrorHint) return std::nullopt;

  if(toks[0] == "help") return std::nullopt;
  const ToolSpec* spec = REG.find(toks[0]);
  if(!spec) return std::nullopt;

  const SubcommandSpec* sub = nullptr;
  if(!spec->subs.empty() && toks.size()>=2){
    for(auto &s : spec->subs){
      if(s.name == toks[1]){ sub = &s; break; }
    }
  }

  PathKind expected = PathKind::Any;
  bool hasExpectation = false;

  auto findPathOpt = [&](const std::vector<OptionSpec>& opts)->const OptionSpec*{
    if(toks.size()<2 || toks.back()!=sw.word) return nullptr;
    std::string prev = toks[toks.size()-2];
    for(auto &o : opts){
      if(o.name==prev && o.takesValue && o.isPath) return &o;
    }
    return nullptr;
  };

  const OptionSpec* opt = nullptr;
  if(sub){ opt = findPathOpt(sub->options); }
  if(!opt) opt = findPathOpt(spec->options);
  if(opt){
    expected = (opt->pathKind != PathKind::Any) ? opt->pathKind : placeholderPathKind(opt->placeholder);
    hasExpectation = true;
  } else {
    if(sub){
      std::vector<OptionSpec> combinedOpts = spec->options;
      combinedOpts.insert(combinedOpts.end(), sub->options.begin(), sub->options.end());
      auto ctx = analyzePositionalPathContext(sub->positional, /*startIdx*/2, combinedOpts, toks, sw, buf);
      if(ctx.appliesToCurrentWord){
        expected = ctx.kind;
        hasExpectation = true;
      }
    } else {
      auto ctx = analyzePositionalPathContext(spec->positional, /*startIdx*/1, spec->options, toks, sw, buf);
      if(ctx.appliesToCurrentWord){
        expected = ctx.kind;
        hasExpectation = true;
      }
    }
  }

  if(!hasExpectation) return std::nullopt;

  struct stat st{};
  if(::stat(sw.word.c_str(), &st)!=0){
    bool hasCand = std::any_of(cand.labels.begin(), cand.labels.end(), [&](const std::string& lab){
      MatchResult match = compute_match(lab, sw.word);
      return match.matched;
    });
    if(!hasCand) return tr("path_error_missing");
    return std::nullopt;
  }
  bool isDir = S_ISDIR(st.st_mode);
  bool isFile = S_ISREG(st.st_mode);
  if(expected == PathKind::Dir && !isDir) return tr("path_error_need_dir");
  if(expected == PathKind::File && !isFile) return tr("path_error_need_file");
  return std::nullopt;
}

static std::string contextGhostFor(const std::string& buf){
  auto toks=splitTokens(buf); auto sw=splitLastWord(buf);
  if(toks.empty()) return "";
  if (toks[0] == "help") return " <command>";
  const ToolSpec* spec = REG.find(toks[0]); if(!spec) return "";
  if(inSubcommandSlot(*spec, toks)) return " <subcommand>";
  if(!spec->subs.empty() && toks.size()>=2){
    for(auto &sub: spec->subs){
      if(sub.name==toks[1]){
        std::set<std::string> used;
        for(size_t i=2;i<toks.size();++i)
          if(startsWith(toks[i],"--")||startsWith(toks[i],"-")) used.insert(toks[i]);
        return renderSubGhost(*spec, sub, toks, 1, used);
      }
    }
  }
  return renderCommandGhost(*spec, toks);
}

// ===== Rendering =====
static void renderInputWithGhost(const std::string& status, int status_len,
                                 const std::string& buf, const std::string& ghost){
  (void)status_len;
  std::cout << ansi::CLR
            << ansi::WHITE << status << ansi::RESET
            << ansi::CYAN << ansi::BOLD << PLAIN_PROMPT << ansi::RESET
            << ansi::WHITE << buf << ansi::RESET;
  if(!ghost.empty()) std::cout << ansi::GRAY << ghost << ansi::RESET;
  std::cout.flush();
}
static void renderBelowThree(const std::string& status, int status_len,
                             int cursorCol,
                             const std::string& buf,
                             const Candidates& cand,
                             int sel, int &lastShown){
  (void)status;
  int total=(int)cand.labels.size();
  int toShow = std::min(3, std::max(0, total-1));
  auto sw = splitLastWord(buf);
  int indent = status_len + promptDisplayWidth() + displayWidth(sw.before);
  for(int i=1;i<=toShow;++i){
    size_t idx = (sel+i)%total;
    const std::string& label = cand.labels[idx];
    const std::vector<int>& matches = cand.matchPositions[idx];
    std::string line = renderHighlightedLabel(label, matches);
    std::cout << "\n" << "\x1b[2K";
    for(int s=0;s<indent;++s) std::cout << ' ';
    std::cout << line;
  }
  for(int pad=toShow; pad<lastShown; ++pad){ std::cout << "\n" << "\x1b[2K"; }
  int up = toShow + ((lastShown>toShow)? (lastShown-toShow):0);
  if(up>0) std::cout << ansi::CUU << up << "A";
  std::cout << ansi::CHA << cursorCol << "G" << std::flush;
  lastShown = toShow;
}

// ===== Exec & help =====
static void execToolLine(const std::string& line){
  auto toks = splitTokens(line); if(toks.empty()) return;
  const ToolSpec* spec = REG.find(toks[0]); if(!spec){ std::cout<<trFmt("unknown_command", {{"name", toks[0]}})<<"\n"; return; }
  if(!spec->subs.empty() && toks.size()>=2){
    for(auto &sub: spec->subs){ if(sub.name == toks[1]){ if(sub.handler){ sub.handler(toks); return; } } }
  }
  if(spec->handler){ spec->handler(toks); return; }
  std::cout<<"no handler\n";
}
static void printHelpAll(){
  auto names = REG.listNames();
  std::cout<<tr("help_available_commands")<<"\n";
  std::cout<<tr("help_command_summary")<<"\n";
  for(auto &n:names){
    const ToolSpec* t=REG.find(n);
    std::cout<<"  "<<n;
    if(t){
      std::string summary = localized_tool_summary(*t);
      if(!summary.empty()) std::cout<<"  - "<<summary;
    }
    std::cout<<"\n";
  }
  std::cout<<tr("help_use_command")<<"\n";
}
static void printHelpOne(const std::string& name){
  const ToolSpec* t = REG.find(name);
  if(!t){ std::cout<<trFmt("help_no_such_command", {{"name", name}})<<"\n"; return; }
  std::string summary = localized_tool_summary(*t);
  if(summary.empty()) std::cout<<name<<"\n";
  else std::cout<<name<<" - "<<summary<<"\n";
  if(!t->subs.empty()){
    std::cout<<tr("help_subcommands")<<"\n";
    for(auto &s:t->subs){
      std::cout<<"    "<<s.name;
      if(!s.positional.empty()) std::cout<<" "<<join(s.positional);
      if(!s.options.empty())    std::cout<<"  [options]";
      std::cout<<"\n";
      if(!s.options.empty()){
        for(auto &o:s.options){
          std::cout<<"      "<<o.name;
          if(o.takesValue) std::cout<<" "<<(o.placeholder.empty()? "<val>":o.placeholder);
          if(o.required)   std::cout<<tr("help_required_tag");
          if(o.isPath)     std::cout<<tr("help_path_tag");
          if(!o.valueSuggestions.empty()){
            std::cout<<"  {"; for(size_t i=0;i<o.valueSuggestions.size();++i){ if(i) std::cout<<","; std::cout<<o.valueSuggestions[i]; } std::cout<<"}";
          }
          std::cout<<"\n";
        }
      }
    }
  }
  if(!t->options.empty()){
    std::cout<<tr("help_options")<<"\n";
    for(auto &o:t->options){
      std::cout<<"    "<<o.name;
      if(o.takesValue) std::cout<<" "<<(o.placeholder.empty()? "<val>":o.placeholder);
      if(o.required)   std::cout<<tr("help_required_tag");
      if(o.isPath)     std::cout<<tr("help_path_tag");
      if(!o.valueSuggestions.empty()){
        std::cout<<"  {"; for(size_t i=0;i<o.valueSuggestions.size();++i){ if(i) std::cout<<","; std::cout<<o.valueSuggestions[i]; } std::cout<<"}";
      }
      std::cout<<"\n";
    }
  }
  if(!t->positional.empty()){
    std::cout<<trFmt("help_positional", {{"value", join(t->positional)}})<<"\n";
  }
}

// ===== Main =====
int main(){
  std::setlocale(LC_CTYPE, "");
  load_config(config_file_path());
  apply_config_to_runtime();

  // 1) 注册内置工具与状态
  register_all_tools();
  register_status_providers();

  // 2) 从当前目录加载动态工具（如 git/pytool）
  const std::string conf = "./mycli_tools.conf";
  register_tools_from_config(conf);

  // 3) 退出时回车复位
  std::atexit([](){ ::write(STDOUT_FILENO, "\r\n", 2); ::fsync(STDOUT_FILENO); });
  std::signal(SIGINT,  [](int){ ::write(STDOUT_FILENO, "\r\n", 2); ::_exit(128); });
  std::signal(SIGTERM, [](int){ ::write(STDOUT_FILENO, "\r\n", 2); ::_exit(128); });
  std::signal(SIGHUP,  [](int){ ::write(STDOUT_FILENO, "\r\n", 2); ::_exit(128); });
  std::signal(SIGQUIT, [](int){ ::write(STDOUT_FILENO, "\r\n", 2); ::_exit(128); });

  // 4) 原始模式（最小化）
  struct TermRaw { termios orig{}; bool active=false;
    void enable(){ if(tcgetattr(STDIN_FILENO,&orig)==-1) std::exit(1);
      termios raw=orig; raw.c_lflag&=~(ECHO|ICANON); raw.c_cc[VMIN]=1; raw.c_cc[VTIME]=0;
      if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw)==-1) std::exit(1); active=true; }
    void disable(){ if(active){ tcsetattr(STDIN_FILENO,TCSANOW,&orig); active=false; } }
    ~TermRaw(){ disable(); }
  } term; term.enable();

  std::string buf; int sel=0; int lastShown=0;

  while(true){
    std::string status = REG.renderStatusPrefix();
    int status_len = displayWidth(status);

    Candidates cand = computeCandidates(buf);
    auto sw = splitLastWord(buf);
    int total = (int)cand.labels.size();
    bool haveCand = total>0;

    if(haveCand && sel>=total) sel=0;
    std::string contextGhost = haveCand? "" : contextGhostFor(buf);

    auto pathError = detectPathErrorMessage(buf, cand);

    std::cout << ansi::CLR
              << ansi::WHITE << status << ansi::RESET
              << ansi::CYAN << ansi::BOLD << PLAIN_PROMPT << ansi::RESET
              << ansi::WHITE << sw.before << ansi::RESET;
    if(pathError){
      std::cout << ansi::RED << sw.word << ansi::RESET
                << "  " << ansi::YELLOW << "+" << *pathError << ansi::RESET;
    }else if(haveCand){
      const std::string& label = cand.labels[sel];
      const std::vector<int>& matches = cand.matchPositions[sel];
      std::cout << renderHighlightedLabel(label, matches);
    }else{
      std::cout << ansi::WHITE << sw.word << ansi::RESET;
    }
    if(!contextGhost.empty()) std::cout << ansi::GRAY << contextGhost << ansi::RESET;
    std::cout.flush();

    int baseIndent = status_len + promptDisplayWidth() + displayWidth(sw.before);
    int cursorCol = baseIndent;
    if(pathError){
      cursorCol += displayWidth(sw.word);
    }else if(haveCand){
      cursorCol += highlightCursorOffset(cand.labels[sel], cand.matchPositions[sel]);
    }else{
      cursorCol += displayWidth(sw.word);
    }
    cursorCol += 1;

    if(haveCand){
      renderBelowThree(status, status_len, cursorCol, buf, cand, sel, lastShown);
    }else{
      for(int i=0;i<lastShown;i++){ std::cout<<"\n"<<"\x1b[2K"; }
      if(lastShown>0){ std::cout<<ansi::CUU<<lastShown<<"A"<<ansi::CHA<<1<<"G"; }
      std::cout<<ansi::CHA<<cursorCol<<"G"<<std::flush;
      lastShown=0; sel=0;
    }

    char ch; if(read(STDIN_FILENO,&ch,1)<=0) break;

    if(ch=='\n' || ch=='\r'){
      std::cout << "\n";
      if(!buf.empty()){
        auto tks = splitTokens(buf);
        if(tks[0]=="help"){
          if(tks.size()==1) printHelpAll();
          else printHelpOne(tks[1]);
        }else{
          execToolLine(buf);
          if(!g_parse_error_cmd.empty()){ printHelpOne(g_parse_error_cmd); g_parse_error_cmd.clear(); }
          if(g_should_exit){ std::cout<<ansi::DIM<<"bye"<<ansi::RESET<<"\n"; break; }
        }
      }
      buf.clear(); sel=0; lastShown=0;
      continue;
    }
    if(ch==0x7f){ if(!buf.empty()) buf.pop_back(); sel=0; continue; }
    if(ch=='\t'){ if(haveCand){ auto head=splitLastWord(buf).before; buf=head+cand.labels[sel]; sel=0; } continue; }
    if(ch=='\x1b'){ char seq[2]; if(read(STDIN_FILENO,&seq[0],1)<=0) continue; if(read(STDIN_FILENO,&seq[1],1)<=0) continue;
      if(seq[0]=='['){
        if(seq[1]=='A'){ if(haveCand){ sel=(sel-1+total)%total; } }
        else if(seq[1]=='B'){ if(haveCand){ sel=(sel+1)%total; } }
      }
      continue;
    }
    if(std::isprint((unsigned char)ch)){ buf.push_back(ch); sel=0; continue; }
  }

  ::write(STDOUT_FILENO, "\r\n", 2); ::fsync(STDOUT_FILENO);
  return 0;
}