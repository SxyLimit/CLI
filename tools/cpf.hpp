#pragma once

#include "tool_common.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tool {

struct Cpf {
  static ToolSpec ui(){
    ToolSpec spec;
    spec.name = "cpf";
    spec.summary = "Copy file content to clipboard";
    set_tool_summary_locale(spec, "en", "Copy file content to clipboard");
    set_tool_summary_locale(spec, "zh", "复制文件内容到系统剪贴板");
    set_tool_help_locale(spec, "en", "Usage: cpf <file>");
    set_tool_help_locale(spec, "zh", "用法：cpf <文件路径>");
    spec.positional = {positional("<file>", true, PathKind::File, {}, false, false)};
    return spec;
  }

  static ToolExecutionResult run(const ToolExecutionRequest& request){
    const auto& args = request.tokens;
    if(args.size() != 2){
      g_parse_error_cmd = "cpf";
      return detail::text_result("usage: cpf <file>\n", 1);
    }

    std::filesystem::path path(args[1]);
    std::error_code ec;
    if(!std::filesystem::exists(path, ec) || ec){
      g_parse_error_cmd = "cpf";
      return detail::text_result("cpf: file not found: " + path.string() + "\n", 1);
    }
    if(std::filesystem::is_directory(path, ec)){
      g_parse_error_cmd = "cpf";
      return detail::text_result("cpf: path is a directory: " + path.string() + "\n", 1);
    }

#ifdef _WIN32
    std::ifstream in(path, std::ios::binary);
    if(!in.good()){
      g_parse_error_cmd = "cpf";
      return detail::text_result("cpf: failed to read file: " + path.string() + "\n", 1);
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if(!in.good() && !in.eof()){
      g_parse_error_cmd = "cpf";
      return detail::text_result("cpf: failed to read file: " + path.string() + "\n", 1);
    }

    int wideLen = MultiByteToWideChar(CP_UTF8, 0, content.data(), static_cast<int>(content.size()), nullptr, 0);
    if(!content.empty() && wideLen <= 0){
      g_parse_error_cmd = "cpf";
      return detail::text_result("cpf: failed to convert utf-8 content\n", 1);
    }
    std::wstring wide;
    wide.resize(static_cast<size_t>(wideLen));
    if(wideLen > 0){
      int converted = MultiByteToWideChar(
        CP_UTF8, 0, content.data(), static_cast<int>(content.size()), wide.data(), wideLen
      );
      if(converted <= 0){
        g_parse_error_cmd = "cpf";
        return detail::text_result("cpf: failed to convert utf-8 content\n", 1);
      }
    }

    if(!OpenClipboard(nullptr)){
      g_parse_error_cmd = "cpf";
      return detail::text_result("cpf: failed to open clipboard\n", 1);
    }
    if(!EmptyClipboard()){
      CloseClipboard();
      g_parse_error_cmd = "cpf";
      return detail::text_result("cpf: failed to clear clipboard\n", 1);
    }
    const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if(!handle){
      CloseClipboard();
      g_parse_error_cmd = "cpf";
      return detail::text_result("cpf: failed to allocate clipboard memory\n", 1);
    }
    void* mem = GlobalLock(handle);
    if(!mem){
      GlobalFree(handle);
      CloseClipboard();
      g_parse_error_cmd = "cpf";
      return detail::text_result("cpf: failed to lock clipboard memory\n", 1);
    }
    std::memcpy(mem, wide.c_str(), bytes);
    GlobalUnlock(handle);
    if(!SetClipboardData(CF_UNICODETEXT, handle)){
      GlobalFree(handle);
      CloseClipboard();
      g_parse_error_cmd = "cpf";
      return detail::text_result("cpf: failed to set clipboard data\n", 1);
    }
    CloseClipboard();
    return detail::text_result("cpf: copied to clipboard\n");
#elif defined(__APPLE__)
    std::string command = "pbcopy < " + shellEscape(path.string());
    auto result = detail::execute_shell(request, command);
    if(result.exitCode != 0){
      g_parse_error_cmd = "cpf";
      return detail::text_result("cpf: failed to copy file content to clipboard\n", 1);
    }
    return detail::text_result("cpf: copied to clipboard\n");
#else
    g_parse_error_cmd = "cpf";
    return detail::text_result("cpf: unsupported platform (requires macOS or Windows)\n", 1);
#endif
  }
};

inline ToolDefinition make_cpf_tool(){
  ToolDefinition def;
  def.ui = Cpf::ui();
  def.executor = Cpf::run;
  return def;
}

} // namespace tool
