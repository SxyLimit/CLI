#pragma once

#include "tool_common.hpp"

namespace tool {

struct Cat {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "cat";
    spec.summary = "Print file content (<=1MB, UTF-8)";
    set_tool_summary_locale(spec, "en", "Print file content (<=1MB, UTF-8)");
    set_tool_summary_locale(spec, "zh", "输出文件内容（<=1MB，UTF-8）");
    spec.positional = {"<file>"};
    spec.options = {
      {"--pipe", true, {}, nullptr, false, "<command>", false}
    };
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() < 2){
      g_parse_error_cmd = "cat";
      return detail::text_result("usage: cat <file> [file2 ...] [--pipe <command>]\n", 1);
    }
    std::vector<std::string> files;
    std::optional<std::string> pipeCommand;
    for(size_t i = 1; i < args.size(); ++i){
      if(args[i] == "--pipe"){
        if(i + 1 >= args.size()){
          g_parse_error_cmd = "cat";
          return detail::text_result("usage: cat <file> [--pipe <command>]\n", 1);
        }
        pipeCommand = args[++i];
      }else{
        files.push_back(args[i]);
      }
    }
    if(files.empty()){
      g_parse_error_cmd = "cat";
      return detail::text_result("usage: cat <file> [file2 ...] [--pipe <command>]\n", 1);
    }
    std::string data;
    for(const auto& file : files){
      std::ifstream in(file, std::ios::binary);
      if(!in.good()){
        g_parse_error_cmd = "cat";
        return detail::text_result("cat: unable to open " + file + "\n", 1);
      }
      in.seekg(0, std::ios::end);
      auto sz = in.tellg();
      in.seekg(0);
      if(sz > 1024 * 1024){
        g_parse_error_cmd = "cat";
        return detail::text_result("[cat] file too large (>1MB)\n", 1);
      }
      if(sz > 0){
        std::string chunk(static_cast<size_t>(sz), '\0');
        in.read(&chunk[0], sz);
        data += chunk;
      }
    }
    if(pipeCommand){
#ifdef _WIN32
      FILE* pipe = _popen(pipeCommand->c_str(), "wb");
#else
      FILE* pipe = popen(pipeCommand->c_str(), "w");
#endif
      if(!pipe){
        g_parse_error_cmd = "cat";
        return detail::text_result("cat: unable to open pipe command\n", 1);
      }
      if(!data.empty()){
        std::fwrite(data.data(), 1, data.size(), pipe);
      }
#ifdef _WIN32
      int rc = _pclose(pipe);
#else
      int rc = pclose(pipe);
#endif
      ToolExecutionResult result;
      result.exitCode = rc;
      if(request.silent){
        result.output = "(cat piped data to command)\n";
      }
      return result;
    }
    if(!data.empty() && data.back() != '\n') data.push_back('\n');
    return detail::text_result(data);
  }
};

inline ToolDefinition make_cat_tool(){
  ToolDefinition def;
  def.ui = Cat::ui();
  def.executor = Cat::run;
  return def;
}

} // namespace tool

