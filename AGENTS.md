# CLI Contribution Guidelines

This project now uses the redesigned tool architecture. Please follow these rules when adding or modifying features:

## Tool architecture
- Each tool is defined as a pair of UI metadata (`ToolSpec`) and an execution handler (`ToolExecutor`).
- Only the core bootstrapping commands (`show`, `setting`, and the `exit` aliases) live directly inside `tools.hpp`.
- Every other built-in tool must live in its own header under `tools/` (for example `tools/ls.hpp`, `tools/cat.hpp`).
  - Each header includes `tools/tool_common.hpp`, defines a struct with `static ToolSpec ui()` / `static ToolExecutionResult run(...)`, and exposes `tool::make_<name>_tool()`.
- `tools/tool_common.hpp` hosts the shared helpers available to all tool headers.
- Register new built-in tools in `register_all_tools()` using the exported factory. Do **not** call `REG.registerTool` directly outside that helper.
- When you need custom completions, attach a `ToolCompletionProvider` in the returned `ToolDefinition` (see the `setting` tool for an example).

### Extending the tool set (workflow)
1. Create a new header under `tools/` named after the command (for example `tools/mkdir.hpp`).
   - Define a `struct` with `static ToolSpec ui()` and `static ToolExecutionResult run(const ToolExecutionRequest&)`.
   - Optionally add `static Candidates complete(...)` when custom completions are required.
2. Populate the `ToolSpec` with bilingual metadata.
   - Always fill `spec.summary`/`spec.help` in English.
   - Mirror the copy into `spec.summaryLocales["zh"]` / `spec.helpLocales["zh"]` (and any other locales) so that `help.<lang>` from config files behaves consistently.
3. Model the command tree using `spec.subs`, `spec.positional`, `spec.options`, and mutex groups to keep the UI and autocomplete in sync.
4. Export a factory (`ToolDefinition make_<name>_tool()`) returning the `ToolSpec`, executor, and optional completion provider.
5. Include the new header from `tools.hpp` and register the factory inside `register_all_tools()` via `REG.registerTool(tool::make_<name>_tool());` so that both CLI and programmatic invocations can discover it.
6. If the tool needs asynchronous behaviour (e.g., LLM polling), expose reusable helpers that update prompt indicators (see below) and document them here for future reuse.
7. Every change to the tool catalogue **must** update `README.md` so the user guide stays in sync with these steps.

## Asynchronous indicators
- Prompt badges are now managed via `PromptIndicatorDescriptor` / `PromptIndicatorState`.
- Register new indicators with `register_prompt_indicator` and update their state using `update_prompt_indicator`.
- Use `PromptIndicatorState.textColor` to control glyph colours. Brackets are rendered using the descriptor's `bracketColor` (default white).
- When integrating asynchronous workflows (LLM, message polling, etc.), update the indicator state from the relevant watcher and expose helper functions in the watcher API when necessary.
- Other tools can hook into the async system by calling `register_prompt_indicator` during initialization and `update_prompt_indicator` whenever background work starts/completes. Keep glyphs short (single-letter, like `L` for LLM, `M` for message). Pending states should prefer `ansi::YELLOW`, completion states `ansi::RED`, and idle states `ansi::WHITE` to match the current UX contract.

## Invoking tools programmatically
- Use `invoke_registered_tool(commandLine, silent)` to call any registered tool from code (including tools loaded from configuration). This returns a `ToolExecutionResult` that is not automatically printed.
- `ToolExecutionRequest::forLLM` is set for these invocations; avoid writing directly to `std::cout` inside tool logic and rely on the returned result text instead.

## Dynamic tool configuration
- `mycli_tools.conf` now supports optional multilingual help strings: use `help=` for the default value and `help.<lang>=` for localised variants.
- Dynamic tools are executed through the unified tool interface, so they also benefit from `invoke_registered_tool` and silent execution capture.

## File utilities
- The built-in `cat` command (implemented in `tools/cat.hpp`) supports piping via `--pipe <command>` (data is streamed to the command's STDIN). Capture of the piped command output is not provided; consumers should redirect output within the command itself.
- Common file operations (`mv`, `rm`) are implemented as first-class tools in `tools/mv.hpp` and `tools/rm.hpp`. Use their implementations as a template for adding other filesystem helpers.

## Help output
- Long-form help strings are pulled from `ToolSpec::help`. Populate this field (and `helpLocales`) when adding new tools so that `help <command>` can display detailed guidance.

## Documentation hygiene
- Whenever you add, remove, or significantly update a built-in tool (or change this workflow), reflect the change in `README.md` in the same pull request.

## Testing & style
- Prefer returning `ToolExecutionResult` instances from tool logic rather than writing to stdout directly. If legacy behaviour requires printing, keep it inside the executor function so that silent invocations work correctly.
- When you need to run external commands, prefer `tool::detail::execute_shell` to ensure silent-mode capture works consistently.
