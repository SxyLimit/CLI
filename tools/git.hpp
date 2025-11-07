#pragma once

#include "../globals.hpp"
#include <array>
#include <cctype>
#include <set>
#include <utility>

inline std::string git_trim_copy(const std::string& s){
  size_t a = 0, b = s.size();
  while(a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while(b > a && std::isspace(static_cast<unsigned char>(s[b-1]))) --b;
  return s.substr(a, b - a);
}

inline std::vector<std::string> git_collect_branch_names(){
  auto runCommand = [](const std::string& cmd, bool parseLegacyOutput){
    std::set<std::string> unique;
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if(!pipe) return std::vector<std::string>{};
    std::array<char, 512> buf{};
    while(fgets(buf.data(), static_cast<int>(buf.size()), pipe)){
      std::string line = buf.data();
      while(!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
      if(parseLegacyOutput){
        if(!line.empty() && line[0] == '*') line.erase(line.begin());
        line = git_trim_copy(line);
        auto arrow = line.find("->");
        if(arrow != std::string::npos){
          line = git_trim_copy(line.substr(0, arrow));
        }
      }else{
        line = git_trim_copy(line);
      }
      if(line.empty()) continue;
      unique.insert(std::move(line));
    }
    ::pclose(pipe);
    return std::vector<std::string>(unique.begin(), unique.end());
  };

  auto formatted = runCommand("git branch -a --format='%(refname:short)'", /*parseLegacyOutput*/false);
  if(!formatted.empty()) return formatted;
  return runCommand("git branch -a", /*parseLegacyOutput*/true);
}

inline OptionSpec gitFlag(const std::string& name){
  OptionSpec opt;
  opt.name = name;
  return opt;
}

inline OptionSpec gitValueOption(const std::string& name,
                                 std::vector<std::string> suggestions = {},
                                 std::string placeholder = ""){
  OptionSpec opt;
  opt.name = name;
  opt.takesValue = true;
  opt.valueSuggestions = std::move(suggestions);
  opt.placeholder = placeholder.empty() ? std::string("<value>") : std::move(placeholder);
  return opt;
}

inline OptionSpec gitPathOption(const std::string& name,
                                PathKind kind = PathKind::Any,
                                std::string placeholder = ""){
  OptionSpec opt;
  opt.name = name;
  opt.takesValue = true;
  opt.isPath = true;
  opt.pathKind = kind;
  if(placeholder.empty()){
    if(kind == PathKind::Dir) placeholder = "<dir>";
    else if(kind == PathKind::File) placeholder = "<file>";
    else placeholder = "<path>";
  }
  opt.placeholder = std::move(placeholder);
  return opt;
}

inline SubcommandSpec make_git_subcommand(std::string name,
                                          std::vector<OptionSpec> options,
                                          std::vector<std::string> positional,
                                          std::map<std::string, std::vector<std::string>> mutexGroups,
                                          std::function<void(const std::vector<std::string>&)> handler){
  SubcommandSpec sub;
  sub.name = std::move(name);
  sub.options = std::move(options);
  sub.positional = std::move(positional);
  sub.mutexGroups = std::move(mutexGroups);
  sub.handler = std::move(handler);
  return sub;
}

inline ToolSpec make_git(){
  ToolSpec tool;
  tool.name = "git";
  tool.summary = "Git wrapper loaded from config";

  auto runGit = [](const std::string& subcmd){
    return [subcmd](const std::vector<std::string>& args){
      std::string cmd = "git";
      if(!subcmd.empty()){
        cmd += " ";
        cmd += subcmd;
      }
      for(size_t i = 2; i < args.size(); ++i){
        cmd += " ";
        cmd += args[i];
      }
      int rc = std::system(cmd.c_str());
      if(rc != 0 && !args.empty()){
        g_parse_error_cmd = args[0];
      }
    };
  };

  auto runAlias = [](const std::string& command){
    return [command](const std::vector<std::string>& args){
      std::string cmd = command;
      for(size_t i = 2; i < args.size(); ++i){
        cmd += " ";
        cmd += args[i];
      }
      int rc = std::system(cmd.c_str());
      if(rc != 0 && !args.empty()){
        g_parse_error_cmd = args[0];
      }
    };
  };

  tool.subs.push_back(make_git_subcommand(
      "clone",
      {
        gitValueOption("--depth", {"1","10","50"}, "<depth>"),
        [](){
          OptionSpec opt = gitValueOption("--branch", {}, "<branch>");
          opt.dynamicValues = [](const std::vector<std::string>&){ return git_collect_branch_names(); };
          return opt;
        }(),
        gitFlag("--recursive")
      },
      {"<repo>", "[<dir>]"},
      {},
      runGit("clone")));

  tool.subs.push_back(make_git_subcommand(
      "init",
      {
        gitFlag("--bare"),
        gitValueOption("--initial-branch", {}, "<branch>")
      },
      {"[<directory>]"},
      {},
      runGit("init")));

  tool.subs.push_back(make_git_subcommand(
      "status",
      {
        gitFlag("-s"),
        gitFlag("--short"),
        gitFlag("--branch")
      },
      {},
      {},
      runGit("status")));

  tool.subs.push_back(make_git_subcommand(
      "add",
      {
        gitFlag("-A"),
        gitFlag("-u"),
        gitFlag("-p"),
        gitFlag("-N")
      },
      {"[<pathspec>...]"},
      {},
      runGit("add")));

  tool.subs.push_back(make_git_subcommand(
      "restore",
      {
        gitFlag("--staged"),
        gitValueOption("--source", {}, "<commit>")
      },
      {"[<pathspec>...]"},
      {},
      runGit("restore")));

  tool.subs.push_back(make_git_subcommand(
      "rm",
      {
        gitFlag("-r"),
        gitFlag("--cached"),
        gitFlag("-f")
      },
      {"<pathspec>..."},
      {},
      runGit("rm")));

  tool.subs.push_back(make_git_subcommand(
      "mv",
      {},
      {"<source>", "<destination>"},
      {},
      runGit("mv")));

  tool.subs.push_back(make_git_subcommand(
      "commit",
      {
        gitValueOption("-m", {}, "<message>"),
        gitFlag("--amend"),
        gitFlag("-a"),
        gitFlag("--no-verify")
      },
      {"[<message>]"},
      {{"mode", {"--amend", "-a"}}},
      runGit("commit")));

  tool.subs.push_back(make_git_subcommand(
      "diff",
      {
        gitFlag("--cached"),
        gitFlag("--name-only"),
        gitFlag("--stat")
      },
      {"[<commit>]", "[<path>]"},
      {},
      runGit("diff")));

  tool.subs.push_back(make_git_subcommand(
      "log",
      {
        gitFlag("--oneline"),
        gitFlag("--graph"),
        gitFlag("--stat"),
        gitFlag("-p")
      },
      {"[<path>]"},
      {},
      runGit("log")));

  tool.subs.push_back(make_git_subcommand(
      "show",
      {
        gitFlag("--stat"),
        gitFlag("--name-only")
      },
      {"[<object>]"},
      {},
      runGit("show")));

  tool.subs.push_back(make_git_subcommand(
      "blame",
      {
        gitValueOption("-L", {}, "<range>"),
        gitFlag("-e")
      },
      {"<file>"},
      {},
      runGit("blame")));

  tool.subs.push_back(make_git_subcommand(
      "grep",
      {
        gitFlag("-n"),
        gitFlag("--ignore-case"),
        gitFlag("--count")
      },
      {"<pattern>"},
      {},
      runGit("grep")));

  tool.subs.push_back(make_git_subcommand(
      "branch",
      {
        gitFlag("-a"),
        gitFlag("-r"),
        gitFlag("-v"),
        gitFlag("-c"),
        gitFlag("-d"),
        gitFlag("-D"),
        gitFlag("-m"),
        gitFlag("-M")
      },
      {"[<name>]"},
      {},
      runGit("branch")));

  tool.subs.push_back(make_git_subcommand(
      "switch",
      {
        gitFlag("-c")
      },
      {"<branch>"},
      {},
      runGit("switch")));

  tool.subs.push_back(make_git_subcommand(
      "checkout",
      {
        gitValueOption("-b", {}, "<new-branch>"),
        gitFlag("--detach")
      },
      {"<branch>", "[<path>]"},
      {},
      runGit("checkout")));

  tool.subs.push_back(make_git_subcommand(
      "merge",
      {
        gitFlag("--no-ff"),
        gitFlag("--squash"),
        gitFlag("--abort")
      },
      {"<branch>..."},
      {},
      runGit("merge")));

  tool.subs.push_back(make_git_subcommand(
      "rebase",
      {
        gitFlag("--continue"),
        gitFlag("--abort"),
        gitFlag("--interactive")
      },
      {"[<upstream>]"},
      {},
      runGit("rebase")));

  tool.subs.push_back(make_git_subcommand(
      "cherry-pick",
      {
        gitFlag("--continue"),
        gitFlag("--abort")
      },
      {"<commit>..."},
      {},
      runGit("cherry-pick")));

  tool.subs.push_back(make_git_subcommand(
      "revert",
      {
        gitFlag("--no-edit")
      },
      {"<commit>..."},
      {},
      runGit("revert")));

  tool.subs.push_back(make_git_subcommand(
      "fetch",
      {
        gitFlag("--all"),
        gitFlag("--prune"),
        gitFlag("--tags")
      },
      {"[<remote>]"},
      {},
      runGit("fetch")));

  tool.subs.push_back(make_git_subcommand(
      "pull",
      {
        gitFlag("--rebase"),
        gitFlag("--ff-only")
      },
      {"[<remote>]", "[<branch>]"},
      {},
      runGit("pull")));

  tool.subs.push_back(make_git_subcommand(
      "push",
      {
        gitFlag("-u"),
        gitFlag("--force"),
        gitFlag("--tags")
      },
      {"[<remote>]", "[<branch>]"},
      {},
      runGit("push")));

  tool.subs.push_back(make_git_subcommand(
      "remote",
      {
        gitFlag("add"),
        gitFlag("remove"),
        gitFlag("rename"),
        gitFlag("show"),
        gitFlag("set-url")
      },
      {"<name>", "[<url>]"},
      {},
      runGit("remote")));

  tool.subs.push_back(make_git_subcommand(
      "tag",
      {
        gitFlag("-a"),
        gitFlag("-d"),
        gitFlag("-l")
      },
      {"[<tagname>]"},
      {},
      runGit("tag")));

  tool.subs.push_back(make_git_subcommand(
      "describe",
      {
        gitFlag("--tags"),
        gitFlag("--always")
      },
      {"[<object>]"},
      {},
      runGit("describe")));

  tool.subs.push_back(make_git_subcommand(
      "stash",
      {
        gitFlag("save"),
        gitFlag("apply"),
        gitFlag("list"),
        gitFlag("drop"),
        gitFlag("clear"),
        gitFlag("pop")
      },
      {"[<message>]"},
      {},
      runGit("stash")));

  tool.subs.push_back(make_git_subcommand(
      "clean",
      {
        gitFlag("-n"),
        gitFlag("-f"),
        gitFlag("-d"),
        gitFlag("-x")
      },
      {"[<pathspec>...]"},
      {},
      runGit("clean")));

  tool.subs.push_back(make_git_subcommand(
      "reset",
      {
        gitFlag("--hard"),
        gitFlag("--soft"),
        gitFlag("--mixed")
      },
      {"[<commit>]"},
      {},
      runGit("reset")));

  tool.subs.push_back(make_git_subcommand(
      "reflog",
      {
        gitFlag("expire"),
        gitFlag("show")
      },
      {"[<ref>]"},
      {},
      runGit("reflog")));

  tool.subs.push_back(make_git_subcommand(
      "bisect",
      {
        gitFlag("start"),
        gitFlag("bad"),
        gitFlag("good"),
        gitFlag("reset"),
        gitFlag("run")
      },
      {"[<args>...]"},
      {},
      runGit("bisect")));

  tool.subs.push_back(make_git_subcommand(
      "submodule",
      {
        gitFlag("add"),
        gitFlag("init"),
        gitFlag("update"),
        gitFlag("foreach"),
        gitFlag("status")
      },
      {"[<path>]"},
      {},
      runGit("submodule")));

  tool.subs.push_back(make_git_subcommand(
      "worktree",
      {
        gitFlag("add"),
        gitFlag("list"),
        gitFlag("remove"),
        gitFlag("prune")
      },
      {"[<path>]"},
      {},
      runGit("worktree")));

  tool.subs.push_back(make_git_subcommand(
      "gc",
      {
        gitFlag("--aggressive")
      },
      {},
      {},
      runGit("gc")));

  tool.subs.push_back(make_git_subcommand(
      "archive",
      {
        gitFlag("--format=zip"),
        gitFlag("--format=tar")
      },
      {"<tree-ish>", "[<path>]"},
      {},
      runGit("archive")));

  tool.subs.push_back(make_git_subcommand(
      "hist",
      {},
      {},
      {},
      runAlias("git log --oneline --graph --decorate --all")));

  tool.subs.push_back(make_git_subcommand(
      "lga",
      {},
      {},
      {},
      runAlias("git log --graph --abbrev-commit --decorate --format=format:'%C(bold blue)%h%C(reset) - %C(bold green)(%ar)%C(reset) %C(white)%s%C(reset) %C(dim white)- %an%C(reset) %C(bold yellow)%d%C(reset)' --all")));

  tool.handler = [](const std::vector<std::string>&){
    std::cout << "usage: git <subcommand> [options]\n";
  };

  return tool;
}
