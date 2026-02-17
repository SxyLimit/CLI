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
  static std::string escapeAppleScriptString(const std::string& raw){
    std::string out;
    out.reserve(raw.size() + 8);
    for(char ch : raw){
      if(ch == '\\' || ch == '"') out.push_back('\\');
      out.push_back(ch);
    }
    return out;
  }

#ifdef _WIN32
  static bool setWindowsClipboardUnicode(const std::wstring& text, std::string& error){
    if(!OpenClipboard(nullptr)){
      error = "failed to open clipboard";
      return false;
    }
    if(!EmptyClipboard()){
      CloseClipboard();
      error = "failed to clear clipboard";
      return false;
    }
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if(!handle){
      CloseClipboard();
      error = "failed to allocate clipboard memory";
      return false;
    }
    void* mem = GlobalLock(handle);
    if(!mem){
      GlobalFree(handle);
      CloseClipboard();
      error = "failed to lock clipboard memory";
      return false;
    }
    std::memcpy(mem, text.c_str(), bytes);
    GlobalUnlock(handle);
    if(!SetClipboardData(CF_UNICODETEXT, handle)){
      GlobalFree(handle);
      CloseClipboard();
      error = "failed to set clipboard data";
      return false;
    }
    CloseClipboard();
    return true;
  }

  static bool setWindowsClipboardAnsi(const std::string& text, std::string& error){
    if(!OpenClipboard(nullptr)){
      error = "failed to open clipboard";
      return false;
    }
    if(!EmptyClipboard()){
      CloseClipboard();
      error = "failed to clear clipboard";
      return false;
    }
    const size_t bytes = text.size() + 1;
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if(!handle){
      CloseClipboard();
      error = "failed to allocate clipboard memory";
      return false;
    }
    void* mem = GlobalLock(handle);
    if(!mem){
      GlobalFree(handle);
      CloseClipboard();
      error = "failed to lock clipboard memory";
      return false;
    }
    std::memcpy(mem, text.data(), text.size());
    static_cast<char*>(mem)[text.size()] = '\0';
    GlobalUnlock(handle);
    if(!SetClipboardData(CF_TEXT, handle)){
      GlobalFree(handle);
      CloseClipboard();
      error = "failed to set clipboard data";
      return false;
    }
    CloseClipboard();
    return true;
  }
#endif

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

    bool copied = false;
    std::string clipboardError;
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, content.data(), static_cast<int>(content.size()), nullptr, 0);
    if(content.empty() || wideLen > 0){
      std::wstring wide;
      if(wideLen > 0){
        wide.resize(static_cast<size_t>(wideLen));
        int converted = MultiByteToWideChar(
          CP_UTF8, 0, content.data(), static_cast<int>(content.size()), wide.data(), wideLen
        );
        if(converted > 0){
          copied = setWindowsClipboardUnicode(wide, clipboardError);
        }else{
          clipboardError = "failed to convert utf-8 content";
        }
      }else{
        copied = setWindowsClipboardUnicode(std::wstring(), clipboardError);
      }
    }
    if(!copied){
      if(content.find('\0') != std::string::npos){
        g_parse_error_cmd = "cpf";
        return detail::text_result("cpf: binary file content is not supported for clipboard text\n", 1);
      }
      copied = setWindowsClipboardAnsi(content, clipboardError);
    }
    if(!copied){
      g_parse_error_cmd = "cpf";
      return detail::text_result("cpf: " + clipboardError + "\n", 1);
    }
    return detail::text_result("cpf: copied to clipboard\n");
#elif defined(__APPLE__)
    std::string command = "pbcopy < " + shellEscape(path.string()) + " 2>/dev/null";
    auto [pbcopyCode, pbcopyOutput] = detail::run_command_capture(command);
    (void)pbcopyOutput;
    if(pbcopyCode != 0){
      std::error_code absEc;
      std::filesystem::path absolutePath = std::filesystem::absolute(path, absEc);
      const std::string pathForScript = absEc ? path.string() : absolutePath.string();
      const std::string escapedPath = escapeAppleScriptString(pathForScript);
      std::vector<std::string> lines = {
        "set f to POSIX file \"" + escapedPath + "\"",
        "set fh to open for access f",
        "set txt to (read fh)",
        "close access fh",
        "set the clipboard to txt"
      };
      std::string osa = "osascript";
      for(const auto& line : lines){
        osa += " -e " + shellEscape(line);
      }
      auto [osaCode, osaOutput] = detail::run_command_capture(osa + " 2>/dev/null");
      (void)osaOutput;
      if(osaCode != 0){
        g_parse_error_cmd = "cpf";
        return detail::text_result(
          "cpf: failed to copy file content to clipboard (pbcopy/osascript unavailable)\n", 1
        );
      }
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
