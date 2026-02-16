#pragma once

#include "../globals.hpp"
#include "../settings.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <vector>

#include <dirent.h>
#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#define getcwd _getcwd
#endif

std::string shellEscape(const std::string& arg);

namespace tool {
namespace detail {

inline ToolExecutionResult text_result(const std::string& text, int exitCode = 0){
  ToolExecutionResult result;
  result.exitCode = exitCode;
  result.output = text;
  result.display = text;
  return result;
}

inline std::pair<int, std::string> run_command_capture(const std::string& command){
#ifdef _WIN32
  FILE* pipe = _popen(command.c_str(), "rt");
  if(!pipe) return {-1, std::string()};
  std::string output;
  char buffer[512];
  while(fgets(buffer, sizeof(buffer), pipe)){
    output.append(buffer);
  }
  int rc = _pclose(pipe);
  return {rc, output};
#else
  FILE* pipe = popen(command.c_str(), "r");
  if(!pipe) return {-1, std::string()};
  std::string output;
  char buffer[512];
  while(fgets(buffer, sizeof(buffer), pipe)){
    output.append(buffer);
  }
  int rc = pclose(pipe);
  return {rc, output};
#endif
}

inline ToolExecutionResult execute_shell(const ToolExecutionRequest& request,
                                         const std::string& command,
                                         bool captureWhenSilent = true){
  ToolExecutionResult result;
  if(request.silent && captureWhenSilent){
    auto [code, output] = run_command_capture(command);
    result.exitCode = code;
    result.output = output;
    return result;
  }
  platform::RawModeScope raw_guard;
  int rc = std::system(command.c_str());
  result.exitCode = rc;
  if(request.silent && !captureWhenSilent){
    result.output.clear();
  }
  return result;
}

enum class InteractiveLineStatus {
  Ok,
  Eof,
  Interrupted
};

struct InteractiveLineOptions {
  std::string prompt;
  const std::vector<std::string>* suggestions = nullptr;
  std::function<Candidates(const std::string&)> completionProvider;
  std::function<bool(const std::string&)> prefixValidator;
  std::string invalidSuffix;
  std::string maxLengthSuffix;
  size_t maxLength = 256;
  bool trimOutput = true;
  size_t suggestionRows = 3;
};

inline std::string trim_copy(const std::string& s){
  size_t a = 0;
  size_t b = s.size();
  while(a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while(b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

inline bool ci_starts_with(const std::string& text, const std::string& prefix){
  if(prefix.size() > text.size()) return false;
  for(size_t i = 0; i < prefix.size(); ++i){
    char a = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
    char b = static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[i])));
    if(a != b) return false;
  }
  return true;
}

inline bool autocomplete_unique(std::string& value, const std::vector<std::string>& suggestions){
  const std::string query = trim_copy(value);
  if(query.empty()) return false;
  std::string matched;
  bool found = false;
  for(const auto& candidate : suggestions){
    if(!ci_starts_with(candidate, query)) continue;
    if(found) return false;
    matched = candidate;
    found = true;
  }
  if(!found) return false;
  value = matched;
  return true;
}

inline size_t utf8_char_length(unsigned char lead){
  if((lead & 0x80) == 0) return 1;
  if((lead & 0xE0) == 0xC0) return 2;
  if((lead & 0xF0) == 0xE0) return 3;
  if((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

inline size_t utf8_prev_index(const std::string& text, size_t cursor){
  if(cursor == 0) return 0;
  size_t i = std::min(cursor, text.size());
  while(i > 0){
    --i;
    unsigned char byte = static_cast<unsigned char>(text[i]);
    if((byte & 0xC0) != 0x80) return i;
  }
  return 0;
}

inline size_t utf8_next_index(const std::string& text, size_t cursor){
  if(cursor >= text.size()) return text.size();
  unsigned char lead = static_cast<unsigned char>(text[cursor]);
  size_t advance = utf8_char_length(lead);
  if(cursor + advance > text.size()) advance = 1;
  return std::min(text.size(), cursor + advance);
}

inline std::vector<std::string> interactive_suggestion_matches(const InteractiveLineOptions& options,
                                                               const std::string& buffer){
  std::vector<std::string> matches;
  const std::string query = trim_copy(buffer);

  auto append_ranked_candidates = [&](const std::vector<std::string>& candidates){
    Candidates ranked;
    for(const auto& candidate : candidates){
      MatchResult match = compute_match(candidate, query);
      if(!match.matched) continue;
      ranked.items.push_back(candidate);
      ranked.labels.push_back(candidate);
      ranked.matchPositions.push_back(match.positions);
      ranked.annotations.push_back("");
      ranked.exactMatches.push_back(match.exact);
      ranked.matchDetails.push_back(std::move(match));
    }
    sortCandidatesByMatch(query, ranked);
    for(const auto& label : ranked.labels){
      if(std::find(matches.begin(), matches.end(), label) == matches.end()){
        matches.push_back(label);
      }
    }
  };

  if(options.completionProvider){
    Candidates provided = options.completionProvider(buffer);
    std::vector<std::string> labels = provided.labels;
    if(labels.empty()){
      labels = provided.items;
    }
    append_ranked_candidates(labels);
    return matches;
  }

  if(options.suggestions){
    append_ranked_candidates(*options.suggestions);
  }
  return matches;
}

struct InteractiveWindowSlice {
  size_t start = 0;
  size_t end = 0;
  bool leftEllipsis = false;
  bool rightEllipsis = false;
};

inline InteractiveWindowSlice interactive_slice_window(const std::string& buffer,
                                                       size_t cursor,
                                                       int promptWidth,
                                                       int reserveWidth){
  InteractiveWindowSlice slice;
  size_t n = buffer.size();
  size_t clampedCursor = std::min(cursor, n);
  slice.start = 0;
  slice.end = n;

  int terminalWidth = platform::terminal_columns();
  if(terminalWidth <= 0) terminalWidth = 80;
  int available = terminalWidth - promptWidth;
  if(available < 8) available = 8;
  available -= std::max(0, reserveWidth);
  if(available < 4) available = 4;

  int rightWidth = available;
  if(g_settings.promptInputEllipsisEnabled && !g_settings.promptInputEllipsisRightWidthAuto){
    rightWidth = g_settings.promptInputEllipsisRightWidth;
    if(rightWidth <= 0) rightWidth = available;
    rightWidth = std::min(rightWidth, available);
    if(rightWidth < 4) rightWidth = 4;
  }

  size_t width = static_cast<size_t>(std::max(4, rightWidth));
  if(n <= width){
    slice.end = n;
    return slice;
  }

  size_t leftKeep = width / 2;
  if(g_settings.promptInputEllipsisEnabled){
    if(g_settings.promptInputEllipsisLeftWidth > 0){
      leftKeep = static_cast<size_t>(g_settings.promptInputEllipsisLeftWidth);
      if(leftKeep >= width) leftKeep = width - 1;
    }
  }

  size_t start = 0;
  if(clampedCursor > leftKeep){
    start = clampedCursor - leftKeep;
  }
  if(start + width > n){
    start = n - width;
  }
  size_t end = start + width;
  if(clampedCursor < start){
    start = clampedCursor;
    end = std::min(n, start + width);
  }else if(clampedCursor > end){
    end = clampedCursor;
    if(end > width){
      start = end - width;
    }else{
      start = 0;
    }
  }
  if(end > n){
    end = n;
    if(end > width) start = end - width;
  }

  slice.start = start;
  slice.end = end;
  slice.leftEllipsis = start > 0;
  slice.rightEllipsis = end < n;
  return slice;
}

inline InteractiveLineStatus read_interactive_line(const InteractiveLineOptions& options,
                                                   std::string& out){
  out.clear();
  std::string buffer;
  size_t cursor = 0;
  bool hitLengthLimit = false;
  size_t selectedSuggestion = 0;
  size_t lastShownSuggestionRows = 0;
  bool selectionSticky = false;

  auto clearSuggestionRows = [&](){
    if(lastShownSuggestionRows == 0) return;
    for(size_t i = 0; i < lastShownSuggestionRows; ++i){
      std::cout << "\n\x1b[2K";
    }
    std::cout << "\x1b[" << lastShownSuggestionRows << "A\r";
    lastShownSuggestionRows = 0;
  };

  auto render = [&](){
    std::vector<std::string> matches = interactive_suggestion_matches(options, buffer);
    if(matches.empty()){
      selectedSuggestion = 0;
      selectionSticky = false;
    }else if(!selectionSticky){
      selectedSuggestion = 0;
    }else if(selectedSuggestion >= matches.size()){
      selectedSuggestion = matches.size() - 1;
    }

    bool showInvalid = false;
    if(options.prefixValidator){
      std::string inputForValidation = options.trimOutput ? trim_copy(buffer) : buffer;
      showInvalid = !options.prefixValidator(inputForValidation);
    }

    std::string suffix;
    if(hitLengthLimit && !options.maxLengthSuffix.empty()){
      suffix = options.maxLengthSuffix;
    }else if(showInvalid && !options.invalidSuffix.empty()){
      suffix = options.invalidSuffix;
    }

    size_t clampedCursorForWord = std::min(cursor, buffer.size());
    size_t wordStart = clampedCursorForWord;
    while(wordStart > 0){
      unsigned char c = static_cast<unsigned char>(buffer[wordStart - 1]);
      if(std::isspace(c)) break;
      --wordStart;
    }
    std::string wordQuery = buffer.substr(wordStart, clampedCursorForWord - wordStart);

    auto renderHighlightedCandidate = [&](const std::string& candidate, const std::string& query)->std::string{
      MatchResult match = compute_match(candidate, query);
      if(!match.matched){
        return std::string(ansi::GRAY) + candidate + ansi::RESET;
      }
      std::unordered_set<size_t> matched;
      matched.reserve(match.positions.size() * 2 + 1);
      for(int pos : match.positions){
        if(pos >= 0) matched.insert(static_cast<size_t>(pos));
      }
      std::string outAnsi;
      int colorState = 0; // 1 white, 2 gray
      auto switchColor = [&](int next){
        if(colorState == next) return;
        if(colorState != 0) outAnsi += ansi::RESET;
        if(next == 1) outAnsi += ansi::WHITE;
        else if(next == 2) outAnsi += ansi::GRAY;
        colorState = next;
      };
      size_t i = 0;
      while(i < candidate.size()){
        unsigned char lead = static_cast<unsigned char>(candidate[i]);
        size_t advance = utf8_char_length(lead);
        if(advance == 0 || i + advance > candidate.size()) advance = 1;
        bool isMatch = matched.find(i) != matched.end();
        switchColor(isMatch ? 1 : 2);
        outAnsi.append(candidate, i, advance);
        i += advance;
      }
      switchColor(0);
      return outAnsi;
    };

    bool showInlineCandidate = false;
    std::string inlineTail;
    if(!matches.empty() && suffix.empty() && cursor == buffer.size()){
      const std::string& selected = matches[selectedSuggestion];
      if(ci_starts_with(selected, buffer) && selected.size() >= buffer.size()){
        inlineTail = selected.substr(buffer.size());
        showInlineCandidate = !inlineTail.empty();
      }
    }

    std::cout << ansi::CLR;
    for(size_t i = 0; i < lastShownSuggestionRows; ++i){
      std::cout << "\n\x1b[2K";
    }
    if(lastShownSuggestionRows > 0){
      std::cout << "\x1b[" << lastShownSuggestionRows << "A";
    }
    std::cout << "\r";

    const int promptWidth = static_cast<int>(options.prompt.size());
    const int suffixReserve = static_cast<int>(suffix.size());
    InteractiveWindowSlice slice = interactive_slice_window(buffer, cursor, promptWidth, suffixReserve);

    size_t clampedCursor = std::min(cursor, buffer.size());
    size_t visibleCursor = std::min(clampedCursor, slice.end);
    if(visibleCursor < slice.start) visibleCursor = slice.start;

    std::string visibleBefore = buffer.substr(slice.start, visibleCursor - slice.start);
    std::string visibleAfter = buffer.substr(visibleCursor, slice.end - visibleCursor);

    std::cout << options.prompt;
    if(slice.leftEllipsis){
      std::cout << ansi::GRAY << "..." << ansi::RESET;
    }
    std::cout << visibleBefore;
    if(!suffix.empty()){
      if(hitLengthLimit){
        std::cout << ansi::YELLOW << suffix << ansi::RESET;
      }else{
        std::cout << ansi::RED << suffix << ansi::RESET;
      }
    }
    if(showInlineCandidate){
      std::cout << ansi::GRAY << inlineTail << ansi::RESET;
    }
    std::cout << visibleAfter;
    if(slice.rightEllipsis){
      std::cout << ansi::GRAY << "..." << ansi::RESET;
    }
    std::cout << "\x1b[0K";

    size_t shownRows = 0;
    if(!matches.empty() && options.suggestionRows > 0){
      size_t startOffset = 1;
      size_t rows = 0;
      if(matches.size() > startOffset){
        rows = std::min(options.suggestionRows, matches.size() - startOffset);
      }
      size_t visibleWordStart = wordStart;
      if(visibleWordStart < slice.start) visibleWordStart = slice.start;
      if(visibleWordStart > slice.end) visibleWordStart = slice.end;
      size_t anchorColumn = options.prompt.size() +
                            (slice.leftEllipsis ? 3 : 0) +
                            (visibleWordStart - slice.start) + 1;
      size_t indent = (anchorColumn > 1) ? (anchorColumn - 1) : 0;
      for(size_t i = 0; i < rows; ++i){
        size_t idx = (selectedSuggestion + startOffset + i) % matches.size();
        std::cout << "\n\x1b[2K";
        if(indent > 0){
          std::cout << std::string(indent, ' ');
        }
        std::cout << renderHighlightedCandidate(matches[idx], wordQuery);
      }
      shownRows = rows;
    }
    lastShownSuggestionRows = shownRows;

    if(shownRows > 0){
      std::cout << "\x1b[" << shownRows << "A";
    }
    size_t caretColumn = options.prompt.size() + (slice.leftEllipsis ? 3 : 0) + visibleBefore.size() + 1;
    if(caretColumn < 1) caretColumn = 1;
    std::cout << "\x1b[" << caretColumn << "G" << std::flush;
  };

  render();
  while(true){
    char ch = 0;
    if(!platform::read_char(ch)){
      clearSuggestionRows();
      std::cout << "\n";
      return InteractiveLineStatus::Eof;
    }

    unsigned char uch = static_cast<unsigned char>(ch);
    if(ch == '\n' || ch == '\r'){
      out = options.trimOutput ? trim_copy(buffer) : buffer;
      clearSuggestionRows();
      std::cout << "\n";
      return InteractiveLineStatus::Ok;
    }
    if(uch == 0x03){
      clearSuggestionRows();
      std::cout << "\n";
      return InteractiveLineStatus::Interrupted;
    }
    if(ch == '\t'){
      hitLengthLimit = false;
      if(options.suggestions || options.completionProvider){
        std::vector<std::string> matches = interactive_suggestion_matches(options, buffer);
        std::string completed = buffer;
        bool canComplete = false;
        if(!matches.empty()){
          if(selectionSticky){
            if(selectedSuggestion >= matches.size()) selectedSuggestion = matches.size() - 1;
          }else{
            selectedSuggestion = 0;
          }
          completed = matches[selectedSuggestion];
          canComplete = true;
        }else if(options.suggestions && !options.completionProvider &&
                 autocomplete_unique(completed, *options.suggestions)){
          canComplete = true;
        }
        if(canComplete){
          if(options.maxLength == 0 || completed.size() <= options.maxLength){
            buffer = completed;
            cursor = buffer.size();
            selectionSticky = false;
          }else{
            hitLengthLimit = true;
          }
        }
      }
      render();
      continue;
    }
    if(uch == 0x7f || uch == 0x08){
      hitLengthLimit = false;
      if(cursor > 0){
        size_t prev = utf8_prev_index(buffer, cursor);
        if(prev < cursor){
          buffer.erase(prev, cursor - prev);
          cursor = prev;
          selectionSticky = false;
        }
      }
      render();
      continue;
    }
    if(ch == '\x1b'){
      hitLengthLimit = false;
      char seq0 = 0;
      if(!platform::read_char(seq0)){
        render();
        continue;
      }
      if(seq0 == '['){
        char seq1 = 0;
        if(!platform::read_char(seq1)){
          render();
          continue;
        }
        if(seq1 == 'D'){
          size_t prev = utf8_prev_index(buffer, cursor);
          if(prev != cursor) cursor = prev;
        }else if(seq1 == 'C'){
          size_t next = utf8_next_index(buffer, cursor);
          if(next != cursor) cursor = next;
        }else if(seq1 == 'A'){
          std::vector<std::string> matches = interactive_suggestion_matches(options, buffer);
          if(!matches.empty()){
            if(!selectionSticky){
              selectionSticky = true;
              selectedSuggestion = matches.size() - 1;
            }else if(selectedSuggestion == 0){
              selectedSuggestion = matches.size() - 1;
            }else{
              selectedSuggestion -= 1;
            }
          }
        }else if(seq1 == 'B'){
          std::vector<std::string> matches = interactive_suggestion_matches(options, buffer);
          if(!matches.empty()){
            if(!selectionSticky){
              selectionSticky = true;
              selectedSuggestion = (matches.size() > 1) ? 1 : 0;
            }else{
              selectedSuggestion = (selectedSuggestion + 1) % matches.size();
            }
          }
        }else if(seq1 == 'H'){
          cursor = 0;
        }else if(seq1 == 'F'){
          cursor = buffer.size();
        }else if(seq1 == '3'){
          char tail = 0;
          if(platform::read_char(tail) && tail == '~'){
            if(cursor < buffer.size()){
              size_t next = utf8_next_index(buffer, cursor);
              if(next > cursor){
                buffer.erase(cursor, next - cursor);
                selectionSticky = false;
              }
            }
          }
        }else if(std::isdigit(static_cast<unsigned char>(seq1))){
          std::string digits(1, seq1);
          char tail = 0;
          while(platform::read_char(tail)){
            if(std::isdigit(static_cast<unsigned char>(tail))){
              digits.push_back(tail);
              continue;
            }
            if(tail == '~'){
              if(digits == "1" || digits == "7"){
                cursor = 0;
              }else if(digits == "4" || digits == "8"){
                cursor = buffer.size();
              }else if(digits == "3"){
                if(cursor < buffer.size()){
                  size_t next = utf8_next_index(buffer, cursor);
                  if(next > cursor){
                    buffer.erase(cursor, next - cursor);
                    selectionSticky = false;
                  }
                }
              }
              break;
            }
            break;
          }
        }
      }
      render();
      continue;
    }
#ifdef _WIN32
    if(uch == 0x00 || uch == 0xE0){
      hitLengthLimit = false;
      char code = 0;
      if(!platform::read_char(code)){
        render();
        continue;
      }
      switch(static_cast<unsigned char>(code)){
        case 72: { // Up
          std::vector<std::string> matches = interactive_suggestion_matches(options, buffer);
          if(!matches.empty()){
            if(!selectionSticky){
              selectionSticky = true;
              selectedSuggestion = matches.size() - 1;
            }else if(selectedSuggestion == 0){
              selectedSuggestion = matches.size() - 1;
            }else{
              selectedSuggestion -= 1;
            }
          }
          break;
        }
        case 80: { // Down
          std::vector<std::string> matches = interactive_suggestion_matches(options, buffer);
          if(!matches.empty()){
            if(!selectionSticky){
              selectionSticky = true;
              selectedSuggestion = (matches.size() > 1) ? 1 : 0;
            }else{
              selectedSuggestion = (selectedSuggestion + 1) % matches.size();
            }
          }
          break;
        }
        case 75: { // Left
          size_t prev = utf8_prev_index(buffer, cursor);
          if(prev != cursor) cursor = prev;
          break;
        }
        case 77: { // Right
          size_t next = utf8_next_index(buffer, cursor);
          if(next != cursor) cursor = next;
          break;
        }
        case 71: cursor = 0; break; // Home
        case 79: cursor = buffer.size(); break; // End
        case 83: // Delete
          if(cursor < buffer.size()){
            size_t next = utf8_next_index(buffer, cursor);
            if(next > cursor){
              buffer.erase(cursor, next - cursor);
              selectionSticky = false;
            }
          }
          break;
        default: break;
      }
      render();
      continue;
    }
#endif
    if(uch >= 0x20){
      if(options.maxLength > 0 && buffer.size() >= options.maxLength){
        hitLengthLimit = true;
        render();
        continue;
      }
      hitLengthLimit = false;
      buffer.insert(cursor, 1, ch);
      cursor += 1;
      selectionSticky = false;
      render();
      continue;
    }
  }
}

} // namespace detail

inline PositionalArgSpec positional(const std::string& placeholder,
                                    bool isPath = false,
                                    PathKind kind = PathKind::Any,
                                    std::vector<std::string> extensions = {},
                                    bool allowDirectory = true,
                                    bool inferFromPlaceholder = true){
  PositionalArgSpec spec;
  spec.placeholder = placeholder;
  spec.isPath = isPath;
  spec.pathKind = kind;
  spec.allowedExtensions = std::move(extensions);
  spec.allowDirectory = allowDirectory;
  spec.inferFromPlaceholder = inferFromPlaceholder;
  return spec;
}

} // namespace tool
