#pragma once

#include "tool_common.hpp"
#include "../utils/json.hpp"

namespace tool {

struct CdsEntry {
  std::string name;
  std::string path;
};

struct CdsState {
  std::vector<CdsEntry> entries;
  std::string lastFrom;
  std::string lastAlias;
  std::string lastTarget;
};

struct Cds {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "cds";
    spec.summary = "Quick jump between bookmarked directories";
    set_tool_summary_locale(spec, "en", "Quick jump between bookmarked directories");
    set_tool_summary_locale(spec, "zh", "在书签目录之间快速跳转");
    set_tool_help_locale(spec, "en",
                         "cds /<name> | cds\n"
                         "cds add <name> <path> | cds set <name> <path>\n"
                         "cds rm <name> | cds rename <old> <new> | cds here <name>\n"
                         "cds list | cds clear");
    set_tool_help_locale(spec, "zh",
                         "cds /<快捷名> | cds\n"
                         "cds add <快捷名> <路径> | cds set <快捷名> <路径>\n"
                         "cds rm <快捷名> | cds rename <旧名> <新名> | cds here <快捷名>\n"
                         "cds list | cds clear");
    spec.subs = {
      SubcommandSpec{"add", {}, {positional("<name>"), positional("<path>", true, PathKind::Dir, {}, true, false)}, {}, nullptr},
      SubcommandSpec{"set", {}, {positional("<name>"), positional("<path>", true, PathKind::Dir, {}, true, false)}, {}, nullptr},
      SubcommandSpec{"rm", {}, {positional("<name>")}, {}, nullptr},
      SubcommandSpec{"rename", {}, {positional("<old>"), positional("<new>")}, {}, nullptr},
      SubcommandSpec{"here", {}, {positional("<name>")}, {}, nullptr},
      SubcommandSpec{"list", {}, {}, {}, nullptr},
      SubcommandSpec{"clear", {}, {}, {}, nullptr}
    };
    spec.positional = {positional("[/<name>]")};
    return spec;
  }

  static Candidates complete(const std::string& buffer, const std::vector<std::string>& tokens){
    Candidates cand;
    if(tokens.empty() || tokens[0] != "cds") return cand;

    const bool trailingSpace = (!buffer.empty() && std::isspace(static_cast<unsigned char>(buffer.back())));
    const SplitWord sw = splitLastWord(buffer);
    const CdsState state = loadState();

    auto addCandidate = [&](const std::string& value, const std::string& annotation = std::string()){
      MatchResult match = compute_match(value, sw.word);
      if(!match.matched) return;
      cand.items.push_back(sw.before + value);
      cand.labels.push_back(value);
      cand.matchPositions.push_back(match.positions);
      cand.annotations.push_back(annotation);
      cand.exactMatches.push_back(match.exact);
      cand.matchDetails.push_back(match);
    };

    auto addAliases = [&](bool withSlash){
      for(const auto& entry : state.entries){
        const std::string label = withSlash ? ("/" + entry.name) : entry.name;
        addCandidate(label, entry.path);
      }
    };

    auto addSubcommands = [&](){
      for(const auto& sub : {"add", "set", "rm", "rename", "here", "list", "clear"}){
        addCandidate(sub);
      }
    };

    if(tokens.size() == 1 || (tokens.size() == 2 && !trailingSpace)){
      if(startsWith(sw.word, "/")){
        addAliases(true);
      }else{
        addSubcommands();
        addAliases(true);
      }
      sortCandidatesByMatch(sw.word, cand);
      return cand;
    }

    const std::string first = (tokens.size() >= 2) ? tokens[1] : std::string();
    if(first.empty() || startsWith(first, "/")) return cand;

    auto aliasSlot = [&](size_t idx){
      if(tokens.size() == idx && trailingSpace) return true;
      if(tokens.size() == idx + 1 && !trailingSpace) return true;
      return false;
    };

    if(first == "add" || first == "set"){
      if(aliasSlot(2)){
        addAliases(false);
        sortCandidatesByMatch(sw.word, cand);
        return cand;
      }
      bool editingPath = (tokens.size() == 3 && trailingSpace) || (tokens.size() >= 4 && !trailingSpace);
      if(editingPath){
        return pathCandidatesForWord(buffer, sw.word, PathKind::Dir, nullptr, true);
      }
      return cand;
    }

    if(first == "rm" || first == "here"){
      if(aliasSlot(2)){
        addAliases(false);
        sortCandidatesByMatch(sw.word, cand);
      }
      return cand;
    }

    if(first == "rename"){
      if(aliasSlot(2) || aliasSlot(3)){
        addAliases(false);
        sortCandidatesByMatch(sw.word, cand);
      }
      return cand;
    }

    return cand;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() == 1){
      return handleReturn();
    }

    const std::string token = args[1];
    if(!token.empty() && token[0] == '/'){
      return handleJump(token);
    }

    if(token == "add") return handleAdd(args, /*allowOverwrite=*/false);
    if(token == "set") return handleAdd(args, /*allowOverwrite=*/true);
    if(token == "rm") return handleRemove(args);
    if(token == "rename") return handleRename(args);
    if(token == "here") return handleHere(args);
    if(token == "list") return handleList(args);
    if(token == "clear") return handleClear(args);

    return handleJump(token);
  }

private:
  static std::filesystem::path statePath(){
    return std::filesystem::path(config_home()) / "cds.json";
  }

  static bool ensureStateFolder(std::string& error){
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(config_home()), ec);
    if(ec){
      error = "cds: failed to initialize config folder: " + ec.message();
      return false;
    }
    return true;
  }

  static std::string normalizeAlias(const std::string& raw, bool stripLeadingSlash){
    std::string alias = detail::trim_copy(raw);
    if(stripLeadingSlash && startsWith(alias, "/")){
      alias.erase(alias.begin());
    }
    return alias;
  }

  static bool isValidAlias(const std::string& alias){
    if(alias.empty()) return false;
    for(char ch : alias){
      if(std::isspace(static_cast<unsigned char>(ch))) return false;
      if(ch == '/' || ch == '\\') return false;
    }
    return true;
  }

  static bool normalizeDirectoryPath(const std::string& raw, std::string& normalized, std::string& error){
    std::filesystem::path p(detail::trim_copy(raw));
    if(p.empty()){
      error = "cds: empty path";
      return false;
    }
    std::error_code ec;
    if(p.is_relative()){
      p = std::filesystem::absolute(p, ec);
      if(ec){
        error = "cds: invalid path";
        return false;
      }
    }
    p = p.lexically_normal();
    if(!std::filesystem::exists(p, ec) || ec){
      error = "cds: path not found: " + p.string();
      return false;
    }
    if(!std::filesystem::is_directory(p, ec) || ec){
      error = "cds: path is not a directory: " + p.string();
      return false;
    }
    normalized = p.string();
    return true;
  }

  static std::string currentDirectory(){
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    if(ec) return std::string();
    return cwd.lexically_normal().string();
  }

  static bool loadStateFromDisk(CdsState& out){
    std::ifstream in(statePath());
    if(!in.good()) return true;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if(content.empty()) return true;
    try{
      sj::Parser parser(content);
      sj::Value root = parser.parse();
      if(!root.isObject()) return true;
      const sj::Object& obj = root.asObject();
      if(const auto* arr = root.find("entries"); arr && arr->isArray()){
        for(const auto& item : arr->asArray()){
          if(!item.isObject()) continue;
          const sj::Object& eo = item.asObject();
          auto nameIt = eo.find("name");
          auto pathIt = eo.find("path");
          if(nameIt == eo.end() || pathIt == eo.end()) continue;
          if(!nameIt->second.isString() || !pathIt->second.isString()) continue;
          CdsEntry entry;
          entry.name = detail::trim_copy(nameIt->second.asString());
          entry.path = detail::trim_copy(pathIt->second.asString());
          if(!isValidAlias(entry.name) || entry.path.empty()) continue;
          out.entries.push_back(std::move(entry));
        }
      }
      if(auto it = obj.find("last_from"); it != obj.end() && it->second.isString()){
        out.lastFrom = detail::trim_copy(it->second.asString());
      }
      if(auto it = obj.find("last_alias"); it != obj.end() && it->second.isString()){
        out.lastAlias = detail::trim_copy(it->second.asString());
      }
      if(auto it = obj.find("last_target"); it != obj.end() && it->second.isString()){
        out.lastTarget = detail::trim_copy(it->second.asString());
      }
      std::sort(out.entries.begin(), out.entries.end(), [](const CdsEntry& a, const CdsEntry& b){
        return a.name < b.name;
      });
      out.entries.erase(std::unique(out.entries.begin(), out.entries.end(),
                                    [](const CdsEntry& a, const CdsEntry& b){ return a.name == b.name; }),
                        out.entries.end());
      return true;
    }catch(...){
      return false;
    }
  }

  static CdsState loadState(){
    CdsState state;
    (void)loadStateFromDisk(state);
    return state;
  }

  static bool saveState(const CdsState& state, std::string& error){
    if(!ensureStateFolder(error)) return false;
    sj::Array arr;
    for(const auto& entry : state.entries){
      sj::Object obj;
      obj.emplace("name", sj::Value(entry.name));
      obj.emplace("path", sj::Value(entry.path));
      arr.push_back(sj::Value(std::move(obj)));
    }
    sj::Object root;
    root.emplace("entries", sj::Value(std::move(arr)));
    root.emplace("last_from", sj::Value(state.lastFrom));
    root.emplace("last_alias", sj::Value(state.lastAlias));
    root.emplace("last_target", sj::Value(state.lastTarget));

    std::ofstream out(statePath());
    if(!out.good()){
      error = "cds: failed to open state file for write";
      return false;
    }
    out << sj::dump(sj::Value(std::move(root)));
    if(!out.good()){
      error = "cds: failed to write state file";
      return false;
    }
    return true;
  }

  static CdsEntry* findEntry(std::vector<CdsEntry>& entries, const std::string& name){
    for(auto& entry : entries){
      if(entry.name == name) return &entry;
    }
    return nullptr;
  }

  static const CdsEntry* findEntryConst(const std::vector<CdsEntry>& entries, const std::string& name){
    for(const auto& entry : entries){
      if(entry.name == name) return &entry;
    }
    return nullptr;
  }

  static bool changeDirectory(const std::string& path, std::string& error){
    if(chdir(path.c_str()) == 0) return true;
    error = "cds: " + path + ": " + std::strerror(errno);
    return false;
  }

  static ToolExecutionResult handleJump(const std::string& rawAlias){
    std::string alias = normalizeAlias(rawAlias, /*stripLeadingSlash=*/true);
    if(!isValidAlias(alias)){
      g_parse_error_cmd = "cds";
      return detail::text_result("usage: cds /<name>\n", 1);
    }
    CdsState state = loadState();
    const CdsEntry* entry = findEntryConst(state.entries, alias);
    if(!entry){
      g_parse_error_cmd = "cds";
      return detail::text_result("cds: alias not found: " + alias + "\n", 1);
    }
    std::string from = currentDirectory();
    std::string chdirError;
    if(!changeDirectory(entry->path, chdirError)){
      g_parse_error_cmd = "cds";
      return detail::text_result(chdirError + "\n", 1);
    }
    state.lastFrom = from;
    state.lastAlias = entry->name;
    state.lastTarget = entry->path;
    std::string saveError;
    if(!saveState(state, saveError)){
      g_parse_error_cmd = "cds";
      return detail::text_result(saveError + "\n", 1);
    }
    std::ostringstream oss;
    oss << "cds: /" << entry->name << " -> " << entry->path << "\n";
    return detail::text_result(oss.str());
  }

  static ToolExecutionResult handleReturn(){
    CdsState state = loadState();
    if(state.lastFrom.empty()){
      return detail::text_result("cds: no previous jump source\n", 1);
    }
    std::string chdirError;
    if(!changeDirectory(state.lastFrom, chdirError)){
      g_parse_error_cmd = "cds";
      return detail::text_result(chdirError + "\n", 1);
    }
    std::string returnedPath = state.lastFrom;
    state.lastFrom.clear();
    state.lastAlias.clear();
    state.lastTarget.clear();
    std::string saveError;
    if(!saveState(state, saveError)){
      g_parse_error_cmd = "cds";
      return detail::text_result(saveError + "\n", 1);
    }
    return detail::text_result("cds: returned to " + returnedPath + "\n");
  }

  static ToolExecutionResult handleAdd(const std::vector<std::string>& args, bool allowOverwrite){
    if(args.size() < 4){
      g_parse_error_cmd = "cds";
      return detail::text_result("usage: cds " + std::string(allowOverwrite ? "set" : "add") + " <name> <path>\n", 1);
    }
    std::string alias = normalizeAlias(args[2], /*stripLeadingSlash=*/false);
    if(!isValidAlias(alias)){
      g_parse_error_cmd = "cds";
      return detail::text_result("cds: invalid alias\n", 1);
    }
    std::vector<std::string> pathParts(args.begin() + 3, args.end());
    std::string pathInput = join(pathParts, " ");
    std::string normalizedPath;
    std::string normalizeError;
    if(!normalizeDirectoryPath(pathInput, normalizedPath, normalizeError)){
      g_parse_error_cmd = "cds";
      return detail::text_result(normalizeError + "\n", 1);
    }

    CdsState state = loadState();
    CdsEntry* existing = findEntry(state.entries, alias);
    if(existing && !allowOverwrite){
      g_parse_error_cmd = "cds";
      return detail::text_result("cds: alias already exists: " + alias + " (use `cds set` to overwrite)\n", 1);
    }
    if(existing){
      existing->path = normalizedPath;
    }else{
      state.entries.push_back(CdsEntry{alias, normalizedPath});
      std::sort(state.entries.begin(), state.entries.end(), [](const CdsEntry& a, const CdsEntry& b){
        return a.name < b.name;
      });
    }
    std::string saveError;
    if(!saveState(state, saveError)){
      g_parse_error_cmd = "cds";
      return detail::text_result(saveError + "\n", 1);
    }
    std::string action = existing ? "updated" : "added";
    return detail::text_result("cds: " + action + " /" + alias + " -> " + normalizedPath + "\n");
  }

  static ToolExecutionResult handleRemove(const std::vector<std::string>& args){
    if(args.size() != 3){
      g_parse_error_cmd = "cds";
      return detail::text_result("usage: cds rm <name>\n", 1);
    }
    std::string alias = normalizeAlias(args[2], /*stripLeadingSlash=*/false);
    if(!isValidAlias(alias)){
      g_parse_error_cmd = "cds";
      return detail::text_result("cds: invalid alias\n", 1);
    }
    CdsState state = loadState();
    auto it = std::remove_if(state.entries.begin(), state.entries.end(),
                             [&](const CdsEntry& entry){ return entry.name == alias; });
    if(it == state.entries.end()){
      g_parse_error_cmd = "cds";
      return detail::text_result("cds: alias not found: " + alias + "\n", 1);
    }
    state.entries.erase(it, state.entries.end());
    if(state.lastAlias == alias){
      state.lastAlias.clear();
      state.lastTarget.clear();
    }
    std::string saveError;
    if(!saveState(state, saveError)){
      g_parse_error_cmd = "cds";
      return detail::text_result(saveError + "\n", 1);
    }
    return detail::text_result("cds: removed /" + alias + "\n");
  }

  static ToolExecutionResult handleRename(const std::vector<std::string>& args){
    if(args.size() != 4){
      g_parse_error_cmd = "cds";
      return detail::text_result("usage: cds rename <old> <new>\n", 1);
    }
    std::string oldAlias = normalizeAlias(args[2], /*stripLeadingSlash=*/false);
    std::string newAlias = normalizeAlias(args[3], /*stripLeadingSlash=*/false);
    if(!isValidAlias(oldAlias) || !isValidAlias(newAlias)){
      g_parse_error_cmd = "cds";
      return detail::text_result("cds: invalid alias\n", 1);
    }
    CdsState state = loadState();
    CdsEntry* src = findEntry(state.entries, oldAlias);
    if(!src){
      g_parse_error_cmd = "cds";
      return detail::text_result("cds: alias not found: " + oldAlias + "\n", 1);
    }
    if(findEntry(state.entries, newAlias)){
      g_parse_error_cmd = "cds";
      return detail::text_result("cds: alias already exists: " + newAlias + "\n", 1);
    }
    src->name = newAlias;
    std::sort(state.entries.begin(), state.entries.end(), [](const CdsEntry& a, const CdsEntry& b){
      return a.name < b.name;
    });
    if(state.lastAlias == oldAlias){
      state.lastAlias = newAlias;
    }
    std::string saveError;
    if(!saveState(state, saveError)){
      g_parse_error_cmd = "cds";
      return detail::text_result(saveError + "\n", 1);
    }
    return detail::text_result("cds: renamed /" + oldAlias + " -> /" + newAlias + "\n");
  }

  static ToolExecutionResult handleHere(const std::vector<std::string>& args){
    if(args.size() != 3){
      g_parse_error_cmd = "cds";
      return detail::text_result("usage: cds here <name>\n", 1);
    }
    std::string alias = normalizeAlias(args[2], /*stripLeadingSlash=*/false);
    if(!isValidAlias(alias)){
      g_parse_error_cmd = "cds";
      return detail::text_result("cds: invalid alias\n", 1);
    }
    std::string cwd = currentDirectory();
    if(cwd.empty()){
      g_parse_error_cmd = "cds";
      return detail::text_result("cds: failed to resolve current directory\n", 1);
    }
    CdsState state = loadState();
    CdsEntry* existing = findEntry(state.entries, alias);
    if(existing){
      existing->path = cwd;
    }else{
      state.entries.push_back(CdsEntry{alias, cwd});
      std::sort(state.entries.begin(), state.entries.end(), [](const CdsEntry& a, const CdsEntry& b){
        return a.name < b.name;
      });
    }
    std::string saveError;
    if(!saveState(state, saveError)){
      g_parse_error_cmd = "cds";
      return detail::text_result(saveError + "\n", 1);
    }
    return detail::text_result("cds: saved /" + alias + " -> " + cwd + "\n");
  }

  static ToolExecutionResult handleList(const std::vector<std::string>& args){
    if(args.size() != 2){
      g_parse_error_cmd = "cds";
      return detail::text_result("usage: cds list\n", 1);
    }
    CdsState state = loadState();
    if(state.entries.empty()){
      return detail::text_result("cds: no aliases configured\n");
    }
    std::ostringstream oss;
    for(const auto& entry : state.entries){
      oss << "/" << entry.name << " -> " << entry.path << "\n";
    }
    return detail::text_result(oss.str());
  }

  static ToolExecutionResult handleClear(const std::vector<std::string>& args){
    if(args.size() != 2){
      g_parse_error_cmd = "cds";
      return detail::text_result("usage: cds clear\n", 1);
    }
    CdsState state;
    std::string saveError;
    if(!saveState(state, saveError)){
      g_parse_error_cmd = "cds";
      return detail::text_result(saveError + "\n", 1);
    }
    return detail::text_result("cds: all aliases cleared\n");
  }
};

inline ToolDefinition make_cds_tool(){
  ToolDefinition def;
  def.ui = Cds::ui();
  def.executor = Cds::run;
  def.completion = Cds::complete;
  return def;
}

} // namespace tool

