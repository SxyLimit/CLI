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
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

#include "globals.hpp"
#include "tools.hpp"

// ===== Global state definitions =====
ToolRegistry REG;
CwdMode      g_cwd_mode = CwdMode::Full;
bool         g_should_exit = false;
std::string  g_parse_error_cmd;

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
    if(startsWith(s, sw.word)){
      out.items.push_back(sw.before + s);
      out.labels.push_back(s);
    }
  }
  return out;
}

static Candidates candidatesForTool(const ToolSpec& spec, const std::string& buf){
  Candidates out; auto sw=splitLastWord(buf); auto toks=splitTokens(buf);
  if(toks.empty() || toks[0]!=spec.name) return out;

  // 子命令名补全
  if (inSubcommandSlot(spec, toks)){
    for(auto &sub: spec.subs){
      if(sw.word.empty() || startsWith(sub.name, sw.word)){
        out.items.push_back(sw.before + sub.name);
        out.labels.push_back(sub.name);
      }
    }
    if(!out.items.empty()) return out;
  }

  // 是否子命令上下文
  auto findSub=[&]()->const SubcommandSpec*{
    if(toks.size()>=2){ for(auto &s: spec.subs) if(s.name==toks[1]) return &s; }
    return nullptr;
  };
  const SubcommandSpec* sub=findSub();

  // 值补全（包含路径型选项）
  if(toks.size()>=2){
    std::string prev = (toks.back()==sw.word && toks.size()>=2) ? toks[toks.size()-2] : (!toks.empty()? toks.back():"");
    auto tryValue = [&](const OptionSpec& o)->bool{
      if(o.name==prev && o.takesValue){
        if(o.isPath) { out = pathCandidatesForWord(buf, sw.word); return true; }
        std::vector<std::string> vals = o.valueSuggestions;
        if(o.dynamicValues){ auto more=o.dynamicValues(toks); vals.insert(vals.end(), more.begin(), more.end()); }
        for(auto &v: vals) if(startsWith(v, sw.word)){ out.items.push_back(sw.before+v); out.labels.push_back(v); }
        return true;
      } return false;
    };
    if(sub){ for(auto &o: sub->options) if(tryValue(o)) return out; }
    for(auto &o: spec.options) if(tryValue(o)) return out;
  }

  // 如果“下一个位置参数占位符”为路径型 → 直接进入路径补全（无需 ./ 或 /）
  auto nextPathPosTrigger = [&](const std::vector<std::string>& posDefs, size_t startIdx, const std::vector<OptionSpec>& opts)->bool{
    // 计算下一个待填的位置参数索引
    size_t i = startIdx, posFilled = 0;
    while(i < toks.size()){
      const std::string& tk = toks[i];
      if(!tk.empty() && tk[0]=='-'){
        bool takes=false; for(auto &o: opts) if(o.name==tk){ takes=o.takesValue; break; }
        i += takes && (i+1<toks.size()) ? 2 : 1;
      } else { posFilled += 1; i += 1; }
    }
    if(posFilled < posDefs.size() && isPathLikePlaceholder(posDefs[posFilled])){
      // 处于输入该位置参数阶段时，触发路径补全（即使 sw.word 为空或不以 ./ 开头）
      return true;
    }
    return false;
  };

  if(sub){
    if(nextPathPosTrigger(sub->positional, /*startIdx*/2, sub->options)){
      return pathCandidatesForWord(buf, sw.word);
    }
  }else{
    if(nextPathPosTrigger(spec.positional, /*startIdx*/1, spec.options)){
      return pathCandidatesForWord(buf, sw.word);
    }
  }

  // 选项名补全
  if(startsWith(sw.word,"-")){
    std::set<std::string> used;
    for(size_t i=1;i<toks.size();++i) if(startsWith(toks[i],"--")||startsWith(toks[i],"-")) used.insert(toks[i]);
    auto addOpts = [&](const std::vector<OptionSpec>& opts){
      for(auto &o: opts){
        if(!used.count(o.name) && startsWith(o.name, sw.word)){
          out.items.push_back(sw.before + o.name);
          out.labels.push_back(o.name);
        }
      }
    };
    if(sub) addOpts(sub->options);
    addOpts(spec.options);
    if(!out.items.empty()) return out;
  }

  // 路径模式（兜底）
  if(startsWith(sw.word,"/")||startsWith(sw.word,"./")||startsWith(sw.word,"../")){
    return pathCandidatesForWord(buf, sw.word);
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
      for (auto &n : names) if (startsWith(n, sw.word)) {
        out.items.push_back(sw.before + n);
        out.labels.push_back(n);
      }
    }
    return out;
  }

  if(toks.empty()) return firstWordCandidates(buf);
  if(const ToolSpec* spec = REG.find(toks[0])) return candidatesForTool(*spec, buf);
  return firstWordCandidates(buf);
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
                             const std::string& buf,
                             const std::vector<std::string>& labels,
                             int sel, int &lastShown){
  int total=(int)labels.size();
  int toShow = std::min(3, std::max(0, total-1));
  auto sw = splitLastWord(buf);
  int indent = status_len + promptDisplayWidth() + displayWidth(sw.before);
  for(int i=1;i<=toShow;++i){
    const std::string& label = labels[(sel+i)%total];
    std::string prefixWord = sw.word;
    std::string suffix = label;
    if(suffix.size()>=prefixWord.size() && startsWith(suffix,prefixWord)) suffix = suffix.substr(prefixWord.size());
    std::cout << "\n" << "\x1b[2K";
    for(int s=0;s<indent;++s) std::cout << ' ';
    std::cout << ansi::WHITE << prefixWord << ansi::RESET
              << ansi::GRAY  << suffix << ansi::RESET;
  }
  for(int pad=toShow; pad<lastShown; ++pad){ std::cout << "\n" << "\x1b[2K"; }
  int up = toShow + ((lastShown>toShow)? (lastShown-toShow):0);
  if(up>0) std::cout << ansi::CUU << up << "A";
  int col = status_len + promptDisplayWidth() + displayWidth(buf) + 1;
  std::cout << ansi::CHA << col << "G" << std::flush;
  lastShown = toShow;
}

// ===== Exec & help =====
static void execToolLine(const std::string& line){
  auto toks = splitTokens(line); if(toks.empty()) return;
  const ToolSpec* spec = REG.find(toks[0]); if(!spec){ std::cout<<"unknown command: "<<toks[0]<<"\n"; return; }
  if(!spec->subs.empty() && toks.size()>=2){
    for(auto &sub: spec->subs){ if(sub.name == toks[1]){ if(sub.handler){ sub.handler(toks); return; } } }
  }
  if(spec->handler){ spec->handler(toks); return; }
  std::cout<<"no handler\n";
}
static void printHelpAll(){
  auto names = REG.listNames();
  std::cout<<"Available commands:\n";
  std::cout<<"  help  - Show command help\n";
  for(auto &n:names){
    const ToolSpec* t=REG.find(n);
    std::cout<<"  "<<n;
    if(t && !t->summary.empty()) std::cout<<"  - "<<t->summary;
    std::cout<<"\n";
  }
  std::cout<<"Use: help <command> to see details.\n";
}
static void printHelpOne(const std::string& name){
  const ToolSpec* t = REG.find(name);
  if(!t){ std::cout<<"No such command: "<<name<<"\n"; return; }
  std::cout<<name<<(t->summary.empty()? "":(" - "+t->summary))<<"\n";
  if(!t->subs.empty()){
    std::cout<<"  subcommands:\n";
    for(auto &s:t->subs){
      std::cout<<"    "<<s.name;
      if(!s.positional.empty()) std::cout<<" "<<join(s.positional);
      if(!s.options.empty())    std::cout<<"  [options]";
      std::cout<<"\n";
      if(!s.options.empty()){
        for(auto &o:s.options){
          std::cout<<"      "<<o.name;
          if(o.takesValue) std::cout<<" "<<(o.placeholder.empty()? "<val>":o.placeholder);
          if(o.required)   std::cout<<" (required)";
          if(o.isPath)     std::cout<<" (path)";
          if(!o.valueSuggestions.empty()){
            std::cout<<"  {"; for(size_t i=0;i<o.valueSuggestions.size();++i){ if(i) std::cout<<","; std::cout<<o.valueSuggestions[i]; } std::cout<<"}";
          }
          std::cout<<"\n";
        }
      }
    }
  }
  if(!t->options.empty()){
    std::cout<<"  options:\n";
    for(auto &o:t->options){
      std::cout<<"    "<<o.name;
      if(o.takesValue) std::cout<<" "<<(o.placeholder.empty()? "<val>":o.placeholder);
      if(o.required)   std::cout<<" (required)";
      if(o.isPath)     std::cout<<" (path)";
      if(!o.valueSuggestions.empty()){
        std::cout<<"  {"; for(size_t i=0;i<o.valueSuggestions.size();++i){ if(i) std::cout<<","; std::cout<<o.valueSuggestions[i]; } std::cout<<"}";
      }
      std::cout<<"\n";
    }
  }
  if(!t->positional.empty()){
    std::cout<<"  positional: "<<join(t->positional)<<"\n";
  }
}

// ===== Main =====
int main(){
  std::setlocale(LC_CTYPE, "");
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

    std::string ghost="";
    if(haveCand){
      if(sel>=total) sel=0;
      std::string lab=cand.labels[sel], pre=sw.word;
      if(lab.size()>=pre.size() && startsWith(lab,pre)) ghost=lab.substr(pre.size());
    }
    if(ghost.empty()) ghost = contextGhostFor(buf);

    std::cout << ansi::CLR
              << ansi::WHITE << status << ansi::RESET
              << ansi::CYAN << ansi::BOLD << PLAIN_PROMPT << ansi::RESET
              << ansi::WHITE << buf << ansi::RESET
              << (ghost.empty()? "" : std::string(ansi::GRAY)+ghost+ansi::RESET);
    std::cout.flush();

    if(haveCand){
      int toShow = std::min(3, std::max(0, total-1));
      int indent = status_len + promptDisplayWidth() + displayWidth(sw.before);
      for(int i=1;i<=toShow;++i){
        const std::string& label = cand.labels[(sel+i)%total];
        std::string prefixWord = sw.word;
        std::string suffix = label;
        if(suffix.size()>=prefixWord.size() && startsWith(suffix,prefixWord)) suffix = suffix.substr(prefixWord.size());
        std::cout << "\n" << "\x1b[2K";
        for(int s=0;s<indent;++s) std::cout << ' ';
        std::cout << ansi::WHITE << prefixWord << ansi::RESET
                  << ansi::GRAY  << suffix << ansi::RESET;
      }
      for(int pad=toShow; pad<lastShown; ++pad){ std::cout << "\n" << "\x1b[2K"; }
      int up = toShow + ((lastShown>toShow)? (lastShown-toShow):0);
      if(up>0) std::cout << ansi::CUU << up << "A";
      int col = status_len + promptDisplayWidth() + displayWidth(buf) + 1;
      std::cout << ansi::CHA << col << "G" << std::flush;
      lastShown = toShow;
    }else{
      for(int i=0;i<lastShown;i++){ std::cout<<"\n"<<"\x1b[2K"; }
      if(lastShown>0){ std::cout<<ansi::CUU<<lastShown<<"A"<<ansi::CHA<<1<<"G"; }
      int col = status_len + promptDisplayWidth() + displayWidth(buf) + 1;
      std::cout<<ansi::CHA<<col<<"G"<<std::flush;
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