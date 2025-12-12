# CLI Contribution Guidelines

This project now uses the redesigned tool architecture. Please follow these rules when adding or modifying features:

## Tool architecture
- Each tool is defined as a pair of UI metadata (`ToolSpec`) and an execution handler (`ToolExecutor`).
- Only the core bootstrapping commands (`show`, `setting`, and the `exit` aliases) live directly inside `tools.hpp`.
- Every other built-in tool must live in its own header under `tools/` (for example `tools/ls.hpp`, `tools/cat.hpp`).
  - Each header includes `tools/tool_common.hpp`, defines a struct with `static ToolSpec ui()` / `static ToolExecutionResult run(...)`, and exposes `tool::make_<name>_tool()`.
  - Populate `spec.positional` with `PositionalArgSpec` instances by calling the helper `tool::positional(...)`. This keeps placeholder labels, path-kind hints, and allowed extensions in sync with the autocomplete engine.
- `tools/tool_common.hpp` hosts the shared helpers available to all tool headers.
- Register new built-in tools in `register_all_tools()` using the exported factory. Do **not** call `REG.registerTool` directly outside that helper.
- When you need custom completions, attach a `ToolCompletionProvider` in the returned `ToolDefinition` (see the `setting` tool for an example).
- Commands that expose multiple short flags must accept stacked short options (for example, `-lt` behaving like `-l -t`).

### `setting` command expectations
- Keep the CLI grammar as `setting <get|set> …`. The first argument must always be the action keyword.
- `setting get` without extra segments must dump every known key/value pair. With partial prefixes it should list the entire subtree, while exact matches still show the resolved value.
- `setting set` must resolve the key before reading the new value; rely on the known key catalogue to decide where the path ends and surface completion hints for both segments and allowed values.
- Autocomplete should continue offering hierarchical segments once a token is completed (pressing <Tab> should append the space and expose children), and must never drop suggestions after typing a valid prefix.
- Path-valued settings must flag `SettingKeyInfo::isPath = true` and fill in `pathKind`/`allowedExtensions`/`allowDirectory` so that `setting set` can surface extension-aware filesystem completions (including prefixes like `/` or `./`). When adding dynamic branches (e.g. new `prompt.theme_art_path.*` themes), remember to feed them into `settings_list_keys()` so child segments still appear.

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

### Path metadata & extensions
- `PositionalArgSpec` and `OptionSpec` both expose `allowedExtensions` and `allowDirectory`.
  - Use `tool::positional("placeholder", /*isPath=*/true, PathKind::File, {".climg"})` to require specific suffixes for positional arguments.
  - For option values, fill `OptionSpec::allowedExtensions` (and optionally `pathKind`) so that autocomplete filters and error hints enforce the suffix rule.
- When you introduce new suffix requirements in code, document the behaviour in `README.md` and provide examples in the configuration guide.
- `pathCandidatesForWord` now accepts optional extension and directory parameters; pass them whenever you forward context data to the helper.

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
- `mycli_tools.conf` supports optional multilingual help strings: use `help=` for the default value and `help.<lang>=` for localised variants.
- `optionPaths` entries accept typed descriptors: `--output:file:.climg|.png` will mark the option as a file path and limit completions to the listed suffixes.
- `positionalPaths` uses `1:file:.climg` style descriptors (1-based index). When this metadata is present, the runtime automatically toggles autocomplete, error messages, and help output to reflect the requirement.
- Dynamic tools are executed through the unified tool interface, so they also benefit from `invoke_registered_tool` and silent execution capture.

## Settings storage
- Persisted user preferences live in `mycli_settings.conf` and follow a simple `key=value` line format. Avoid describing or serialising them as JSON.
- When you extend the settings surface, update both the CLI behaviour (including `setting get` with empty prefixes) and the README examples in the same change.

## Command helpers
- The built-in `run` command simply shells out to whatever command follows (`run ls -al`), escaping each argument safely via `shellEscape`. Use this tool for delegating to external commands without adding a bespoke wrapper. **Any time the CLI launches another process (run, config-driven tools, agent helpers, background threads, etc.) you must restore the terminal to its default cursor/rendering state first, then return to raw mode after the subprocess exits.**
- Prefer `tool::detail::execute_shell` for all subprocesses so silent-mode capture and the raw-mode suspension stay consistent. If you ever have to call `std::system` directly (for example a detached thread), wrap it in `platform::RawModeScope`.

## File utilities
- The built-in `cat` command (implemented in `tools/cat.hpp`) supports piping via `--pipe <command>` (data is streamed to the command's STDIN). Capture of the piped command output is not provided; consumers should redirect output within the command itself.
- Common file operations (`mv`, `rm`) are implemented as first-class tools in `tools/mv.hpp` and `tools/rm.hpp`. Use their implementations as a template for adding other filesystem helpers.

## Help output
- Long-form help strings are pulled from `ToolSpec::help`. Populate this field (and `helpLocales`) when adding new tools so that `help <command>` can display detailed guidance.

## Documentation hygiene
- Whenever you add, remove, or significantly update a built-in tool (or change this workflow), reflect the change in `README.md` in the same pull request.
- **中文提醒**：每次新增或调整内置工具时，必须同步在 `README.md` 中补充或更新该工具的使用说明。

## Testing & style
- Prefer returning `ToolExecutionResult` instances from tool logic rather than writing to stdout directly. If legacy behaviour requires printing, keep it inside the executor function so that silent invocations work correctly.
