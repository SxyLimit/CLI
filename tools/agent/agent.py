#!/usr/bin/env python3
import json
import os
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional

ENV_PATH = Path(__file__).resolve().parents[2] / ".env"


def load_env_file() -> None:
    if not ENV_PATH.exists():
        return
    try:
        with ENV_PATH.open("r", encoding="utf-8") as fp:
            for line in fp:
                stripped = line.strip()
                if not stripped or stripped.startswith("#"):
                    continue
                if "=" not in stripped:
                    continue
                key, value = stripped.split("=", 1)
                key = key.strip()
                value = value.strip()
                if key and key not in os.environ:
                    os.environ[key] = value
    except Exception:
        return


load_env_file()

DEFAULT_MODEL = "kimi-k2-turbo-preview"
DEFAULT_BASE_URL = "https://api.moonshot.cn/v1"


def resolve_temperature(default: float = 0.2) -> float:
    raw = os.getenv("LLM_TEMPERATURE", "").strip()
    if not raw:
        return default
    try:
        return float(raw)
    except ValueError:
        return default


def call_openai(messages: List[Dict[str, str]], *, fallback: str = "") -> str:
    api_key = os.getenv("LLM_API_KEY", "").strip() or os.getenv("MOONSHOT_API_KEY", "").strip()
    base_url = os.getenv("LLM_BASE_URL", DEFAULT_BASE_URL)
    model = os.getenv("LLM_MODEL", DEFAULT_MODEL)
    temperature = resolve_temperature()

    try:
        from openai import OpenAI  # type: ignore
    except Exception:
        OpenAI = None  # type: ignore

    if not api_key or OpenAI is None:
        return fallback or "[stub] llm unavailable"

    try:
        client = OpenAI(api_key=api_key, base_url=base_url)
        completion = client.chat.completions.create(
            model=model,
            temperature=temperature,
            messages=messages,
        )
        choices = getattr(completion, "choices", None)
        if choices:
            first = choices[0]
            message = getattr(first, "message", None)
            content = getattr(message, "content", None) if message else None
            if content:
                return content
        return "(no content returned)"
    except Exception as exc:
        return f"[error] {exc}"


AGENT_SYSTEM_PROMPT = (
    "You are the automation agent for the MyCLI terminal. "
    "The CLI exposes a sandboxed filesystem and a limited set of tools that you must call via JSON messages. "
    "Every response MUST be a single JSON object without backticks or commentary. "
    "Use the schema: {\"type\": \"tool\" | \"final\", \"thought\": string, \"tool\": string?, \"args\": object?, \"answer\": string?, \"artifacts\": list?}. "
    "When \"type\" is \"tool\", include the tool name in \"tool\" and provide arguments in \"args\" matching the provided catalog. "
    "When \"type\" is \"final\", supply the human-facing summary in \"answer\" and optional artifacts (with name/mime/content fields). "
    "Run terminal commands with fs.exec.shell and Python code with fs.exec.python instead of assuming implicit execution. "
    "Do not invent tools or arguments outside the policy. Provide concise thoughts explaining the reason for the action."
)

MAX_MESSAGE_LENGTH = 6000
MAX_STEPS = 12


def clamp(text: str, limit: int = MAX_MESSAGE_LENGTH) -> str:
    if len(text) <= limit:
        return text
    return text[:limit] + "\n...[truncated]"


def extract_json_object(text: str) -> Optional[Dict[str, Any]]:
    try:
        parsed = json.loads(text)
        if isinstance(parsed, dict):
            return parsed
    except json.JSONDecodeError:
        pass
    start = text.find("{")
    end = text.rfind("}")
    if start != -1 and end != -1 and end > start:
        candidate = text[start : end + 1]
        try:
            parsed = json.loads(candidate)
            if isinstance(parsed, dict):
                return parsed
        except json.JSONDecodeError:
            return None
    return None


def format_tool_catalog(tools: List[Dict[str, Any]]) -> str:
    lines: List[str] = []
    for tool in tools:
        name = tool.get("name", "")
        summary = tool.get("summary", "").strip()
        if name:
            header = f"Tool {name}: {summary}" if summary else f"Tool {name}"
            lines.append(header)
        schema = tool.get("args_schema") or {}
        props = schema.get("properties") or {}
        required = set(schema.get("required") or [])
        for key, desc in props.items():
            if not isinstance(desc, dict):
                continue
            arg_type = desc.get("type", "string")
            description = desc.get("description", "")
            required_marker = "(required)" if key in required else "(optional)"
            lines.append(f"- {key}: {arg_type} {required_marker} {description}".strip())
    return "\n".join(lines)


class AgentRunner:
    def __init__(self) -> None:
        self.hello: Dict[str, Any] = {}
        self.goal: str = ""
        self.context: Dict[str, Any] = {}
        self.allowed_tools: List[str] = []
        self.tool_catalog: List[Dict[str, Any]] = []
        self.stdout_limit: int = 4096
        self.messages: List[Dict[str, str]] = []
        self.inbox: List[Dict[str, Any]] = []
        self.pending_results: Dict[str, Dict[str, Any]] = {}
        self.step: int = 0

    def send(self, obj: Dict[str, Any]) -> None:
        sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
        sys.stdout.flush()

    def log(self, message: str) -> None:
        self.send({"type": "log", "message": message})

    def read_message(self) -> Optional[Dict[str, Any]]:
        if self.inbox:
            return self.inbox.pop(0)
        raw = sys.stdin.readline()
        if not raw:
            return None
        raw = raw.strip()
        if not raw:
            return None
        try:
            return json.loads(raw)
        except json.JSONDecodeError:
            self.send({"type": "error", "message": "invalid json"})
            return None

    def handshake(self) -> None:
        while True:
            message = self.read_message()
            if message is None:
                break
            mtype = message.get("type")
            if mtype == "hello":
                self.hello = message
                self.tool_catalog = message.get("tool_catalog", {}).get("tools", [])
                limits = message.get("limits", {})
                if isinstance(limits, dict):
                    self.stdout_limit = int(limits.get("stdout_bytes", self.stdout_limit))
                policy = message.get("policy", {})
                if isinstance(policy, dict):
                    allowed = policy.get("allowed_tools", [])
                    if isinstance(allowed, list):
                        self.allowed_tools = [str(item) for item in allowed]
            elif mtype == "start":
                self.goal = message.get("goal", "")
                context = message.get("context", {})
                if isinstance(context, dict):
                    self.context = context
                if self.hello:
                    break
            else:
                self.inbox.append(message)
            if self.hello and self.goal:
                break

    def prepare_conversation(self) -> None:
        catalog_text = format_tool_catalog(self.tool_catalog)
        system_prompt = AGENT_SYSTEM_PROMPT
        if catalog_text:
            system_prompt += "\n\nAvailable tools:\n" + catalog_text
        self.messages = [{"role": "system", "content": clamp(system_prompt)}]
        user_parts = [f"Goal: {self.goal or 'n/a'}"]
        if self.context:
            ctx_lines = [f"{key}: {value}" for key, value in self.context.items()]
            user_parts.append("Context:\n" + "\n".join(ctx_lines))
        if self.allowed_tools:
            user_parts.append("Allowed tools: " + ", ".join(self.allowed_tools))
        user_parts.append("Respond with JSON actions that follow the required schema.")
        self.messages.append({"role": "user", "content": clamp("\n\n".join(user_parts))})

    def wait_for_tool_result(self, call_id: str) -> Optional[Dict[str, Any]]:
        if call_id in self.pending_results:
            return self.pending_results.pop(call_id)
        while True:
            message = self.read_message()
            if message is None:
                return None
            mtype = message.get("type")
            if mtype == "tool_result":
                mid = str(message.get("id", ""))
                if mid == call_id:
                    return message
                self.pending_results[mid] = message
                continue
            if mtype == "error":
                self.log(f"CLI error: {message.get('message', '')}")
                return None
            # Store any other messages to process later.
            self.inbox.append(message)

    def format_tool_observation(self, tool: str, payload: Dict[str, Any]) -> str:
        ok = payload.get("ok")
        exit_code = payload.get("exit_code")
        stdout = clamp(str(payload.get("stdout", "")), self.stdout_limit)
        stderr = clamp(str(payload.get("stderr", "")), self.stdout_limit)
        meta = payload.get("meta")
        meta_text = ""
        if isinstance(meta, dict):
            meta_text = json.dumps(meta, ensure_ascii=False)
        parts = [
            f"Observation: tool {tool} completed (ok={ok}, exit_code={exit_code}).",
            f"stdout:\n{stdout or '<empty>'}",
        ]
        if stderr:
            parts.append(f"stderr:\n{stderr}")
        if meta_text:
            parts.append(f"meta: {meta_text}")
        return clamp("\n".join(parts))

    def ensure_action_schema(self, action: Dict[str, Any]) -> Optional[str]:
        action_type = action.get("type")
        if action_type not in {"tool", "final"}:
            return "`type` must be `tool` or `final`"
        if action_type == "tool":
            tool_name = action.get("tool")
            if not tool_name:
                return "`tool` is required when `type` is `tool`"
            if tool_name not in self.allowed_tools:
                return f"Tool `{tool_name}` is not allowed."
            args = action.get("args")
            if args is None:
                action["args"] = {}
        else:
            if "answer" not in action:
                return "`answer` is required when `type` is `final`"
        return None

    def send_final(self, answer: str, artifacts: Optional[List[Dict[str, Any]]] = None) -> None:
        self.send({"type": "final", "answer": answer, "artifacts": artifacts or []})

    def run(self) -> None:
        self.handshake()
        if not self.hello:
            self.send_final("Agent did not receive initialization data.")
            return
        self.prepare_conversation()
        for _ in range(MAX_STEPS):
            self.step += 1
            reply = call_openai(self.messages, fallback="[stub] llm unavailable")
            if reply.startswith("[stub]") or reply.startswith("[error]"):
                self.send_final(f"LLM unavailable: {reply}")
                return
            self.messages.append({"role": "assistant", "content": clamp(reply)})
            action = extract_json_object(reply)
            if action is None:
                self.log(f"step {self.step}: invalid JSON response, requesting retry")
                self.messages.append({
                    "role": "user",
                    "content": "Your last message was not valid JSON. Respond with a single JSON object that follows the required schema.",
                })
                continue
            thought = action.get("thought")
            if isinstance(thought, str) and thought.strip():
                self.log(f"step {self.step}: {thought.strip()}")
            error_text = self.ensure_action_schema(action)
            if error_text:
                self.log(f"step {self.step}: {error_text}")
                self.messages.append({
                    "role": "user",
                    "content": f"{error_text} Please reply with a valid JSON action.",
                })
                continue
            action_type = action.get("type")
            if action_type == "tool":
                tool_name = action.get("tool")
                args = action.get("args", {})
                call_id = f"{int(time.time()*1000)}-{self.step}"
                self.send({"type": "indicator", "status": "tool", "step": self.step, "tool": tool_name})
                self.send({"type": "tool_call", "id": call_id, "name": tool_name, "args": args})
                result = self.wait_for_tool_result(call_id)
                if result is None:
                    self.send_final("Tool execution failed or no result returned.")
                    return
                observation = self.format_tool_observation(tool_name, result)
                self.messages.append({"role": "user", "content": observation})
            else:
                answer = str(action.get("answer", "")).strip()
                artifacts = action.get("artifacts")
                if isinstance(artifacts, list):
                    filtered: List[Dict[str, Any]] = []
                    for item in artifacts:
                        if isinstance(item, dict) and {"name", "mime", "content"}.issubset(item.keys()):
                            filtered.append({
                                "name": str(item["name"]),
                                "mime": str(item["mime"]),
                                "content": str(item["content"]),
                            })
                    artifacts = filtered
                else:
                    artifacts = []
                self.send_final(answer or "Agent finished without a detailed answer.", artifacts)
                return
        self.send_final("Reached maximum number of reasoning steps without completion.")


def main() -> None:
    runner = AgentRunner()
    runner.run()


if __name__ == "__main__":
    main()
