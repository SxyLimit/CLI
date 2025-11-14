#!/usr/bin/env python3
import json
import os
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from prompts import SYSTEM_PROMPT

# Keep history aligned with the C++ watcher (see `resolve_llm_history_path`).
# Persist history within the shared config directory so the watcher can
# observe updates and display the `[L]` badge in the prompt.


def resolve_config_home() -> Path:
    candidate = os.getenv("HOME_PATH", "").strip()
    if not candidate:
        candidate = "./settings"
    base = Path(os.path.expanduser(candidate))
    if not base.is_absolute():
        base = Path.cwd() / base
    return base.resolve()


CONFIG_HOME = resolve_config_home()
HISTORY_PATH = CONFIG_HOME / "mycli_llm_history.json"
ENV_PATH = Path(__file__).resolve().parent.parent / ".env"

SURROGATE_MIN = 0xD800
SURROGATE_MAX = 0xDFFF
HIGH_SURROGATE_MAX = 0xDBFF
LOW_SURROGATE_MIN = 0xDC00
LOW_SURROGATE_MAX = 0xDFFF


def _contains_surrogate(text: str) -> bool:
    return any(SURROGATE_MIN <= ord(ch) <= SURROGATE_MAX for ch in text)


def repair_surrogates(text: str) -> str:
    if not text or not _contains_surrogate(text):
        return text
    repaired: List[str] = []
    i = 0
    length = len(text)
    while i < length:
        code = ord(text[i])
        if SURROGATE_MIN <= code <= SURROGATE_MAX:
            if code <= HIGH_SURROGATE_MAX and i + 1 < length:
                next_code = ord(text[i + 1])
                if LOW_SURROGATE_MIN <= next_code <= LOW_SURROGATE_MAX:
                    combined = 0x10000 + ((code - SURROGATE_MIN) << 10) + (next_code - LOW_SURROGATE_MIN)
                    repaired.append(chr(combined))
                    i += 2
                    continue
            repaired.append("\ufffd")
        else:
            repaired.append(text[i])
        i += 1
    return "".join(repaired)


def sanitize_state_text(state: Dict[str, Any]) -> None:
    active = state.get("active")
    if isinstance(active, str):
        state["active"] = repair_surrogates(active)
    conversations = state.get("conversations")
    if not isinstance(conversations, list):
        return
    for conv in conversations:
        if not isinstance(conv, dict):
            continue
        for key in ("name", "prefix", "timestamp"):
            value = conv.get(key)
            if isinstance(value, str):
                conv[key] = repair_surrogates(value)
        messages = conv.get("messages")
        if not isinstance(messages, list):
            continue
        for message in messages:
            if not isinstance(message, dict):
                continue
            content = message.get("content")
            if isinstance(content, str):
                message["content"] = repair_surrogates(content)

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
        # Silently ignore .env parsing errors; runtime env vars still apply.
        return


load_env_file()


def default_state() -> Dict[str, Any]:
    return {"active": None, "conversations": [], "untitled_index": 1}


def load_state() -> Dict[str, Any]:
    if not HISTORY_PATH.exists():
        return default_state()
    try:
        with HISTORY_PATH.open("r", encoding="utf-8") as fp:
            raw = json.load(fp)
    except Exception:
        return default_state()
    state = ensure_state_shape(raw)
    sanitize_state_text(state)
    return state


def save_state(state: Dict[str, Any]) -> None:
    HISTORY_PATH.parent.mkdir(parents=True, exist_ok=True)
    sanitize_state_text(state)
    tmp_path = HISTORY_PATH.with_suffix(".tmp")
    try:
        with tmp_path.open("w", encoding="utf-8", errors="backslashreplace") as fp:
            json.dump(state, fp, ensure_ascii=False, indent=2)
            fp.flush()
            os.fsync(fp.fileno())
        os.replace(tmp_path, HISTORY_PATH)
    finally:
        if tmp_path.exists():
            try:
                tmp_path.unlink()
            except OSError:
                pass


def ensure_state_shape(raw: Any) -> Dict[str, Any]:
    if isinstance(raw, dict):
        state = default_state()
        state.update({
            "active": raw.get("active"),
            "conversations": raw.get("conversations", []),
            "untitled_index": raw.get("untitled_index", 1),
        })
        if not isinstance(state["conversations"], list):
            state["conversations"] = []
        for conv in state["conversations"]:
            if not isinstance(conv, dict):
                continue
            if not isinstance(conv.get("messages"), list):
                conv["messages"] = []
            conv.setdefault("messages", [])
            conv.setdefault("prefix", "")
            conv.setdefault("timestamp", timestamp_string())
            conv.setdefault("name", build_conversation_name(conv.get("prefix", ""), conv.get("timestamp", timestamp_string())))
            conv.setdefault("created_at", time.time())
            conv.setdefault("auto_named", False)
        ensure_active_exists(state)
        try:
            state["untitled_index"] = max(1, int(state.get("untitled_index", 1)))
        except (TypeError, ValueError):
            state["untitled_index"] = 1
        return state
    if isinstance(raw, list):
        # Legacy format: list of prompt/reply dictionaries.
        conv_ts = None
        messages: List[Dict[str, Any]] = []
        for item in raw:
            if not isinstance(item, dict):
                continue
            ts_value = item.get("ts")
            if isinstance(ts_value, (int, float)) and conv_ts is None:
                conv_ts = ts_value
            prompt = item.get("prompt")
            reply = item.get("reply")
            if prompt:
                messages.append({"role": "user", "content": prompt, "ts": ts_value or time.time()})
            if reply:
                messages.append({"role": "assistant", "content": reply, "ts": ts_value or time.time()})
        conv_ts = conv_ts or time.time()
        ts_str = timestamp_string(conv_ts)
        prefix = "历史记录"
        state = {
            "active": f"{prefix}-{ts_str}",
            "conversations": [
                {
                    "name": f"{prefix}-{ts_str}",
                    "prefix": prefix,
                    "timestamp": ts_str,
                    "created_at": conv_ts,
                    "messages": messages,
                    "auto_named": True,
                }
            ],
            "untitled_index": 1,
        }
        return state
    return default_state()


def timestamp_string(ts: Optional[float] = None) -> str:
    base = time.localtime(ts if ts is not None else time.time())
    return time.strftime("%Y%m%d%H%M%S", base)


def build_conversation_name(prefix: str, ts: str) -> str:
    prefix = prefix.strip() or ""
    return f"{prefix}-{ts}" if prefix else ts


def ensure_active_exists(state: Dict[str, Any]) -> None:
    conversations = [c for c in state.get("conversations", []) if isinstance(c, dict)]
    if not conversations:
        state["active"] = None
        return
    active = state.get("active")
    if active and any(conv.get("name") == active for conv in conversations):
        return
    # Default to the most recent conversation.
    conversations.sort(key=lambda c: c.get("created_at", 0), reverse=True)
    state["active"] = conversations[0].get("name")


def resolve_temperature(default: float = 0.6) -> float:
    temperature_raw = os.getenv("LLM_TEMPERATURE", str(default))
    try:
        return float(temperature_raw)
    except ValueError:
        return default


def call_openai(messages: List[Dict[str, str]], *, temperature: Optional[float] = None, fallback: str = "") -> str:
    api_key = os.getenv("LLM_API_KEY", "").strip() or os.getenv("MOONSHOT_API_KEY", "").strip()
    base_url = os.getenv("LLM_BASE_URL", "https://api.moonshot.cn/v1")
    model = os.getenv("LLM_MODEL", "kimi-k2-turbo-preview")
    temp_value = resolve_temperature()
    if temperature is not None:
        temp_value = temperature

    try:
        from openai import OpenAI  # type: ignore
    except Exception:
        OpenAI = None  # type: ignore

    if not api_key or OpenAI is None:
        return repair_surrogates(fallback or "[stub] no api key")

    try:
        client = OpenAI(api_key=api_key, base_url=base_url)
        completion = client.chat.completions.create(
            model=model,
            messages=messages,
            temperature=temp_value,
        )
        choices = getattr(completion, "choices", None)
        if choices:
            first = choices[0]
            message = getattr(first, "message", None)
            content = getattr(message, "content", None) if message else None
            if content:
                return repair_surrogates(content)
        return "(no content returned)"
    except Exception as exc:
        return repair_surrogates(f"[error] {exc}")


def get_system_prompt() -> str:
    return repair_surrogates(os.getenv("LLM_SYSTEM_PROMPT", SYSTEM_PROMPT))


def find_conversation(state: Dict[str, Any], name: Optional[str]) -> Optional[Dict[str, Any]]:
    if not name:
        return None
    for conv in state.get("conversations", []):
        if isinstance(conv, dict) and conv.get("name") == name:
            return conv
    return None


def ensure_active_conversation(state: Dict[str, Any]) -> Dict[str, Any]:
    conv = find_conversation(state, state.get("active"))
    if conv is not None:
        return conv
    # Create a new conversation if none exists.
    return create_conversation(state)


def next_untitled_prefix(state: Dict[str, Any]) -> Tuple[str, int]:
    index = int(state.get("untitled_index", 1))
    prefix = f"未命名{index}"
    state["untitled_index"] = index + 1
    return prefix, index


def create_conversation(state: Dict[str, Any], prefix: Optional[str] = None) -> Dict[str, Any]:
    if prefix is None:
        prefix, _ = next_untitled_prefix(state)
    prefix = repair_surrogates(prefix)
    created_at = time.time()
    ts_str = timestamp_string(created_at)
    name = build_conversation_name(prefix, ts_str)
    conv = {
        "name": name,
        "prefix": prefix,
        "timestamp": ts_str,
        "created_at": created_at,
        "messages": [],
        "auto_named": False,
    }
    state.setdefault("conversations", []).append(conv)
    state["active"] = name
    return conv


def conversation_messages_for_api(conv: Dict[str, Any]) -> List[Dict[str, str]]:
    messages = [{"role": "system", "content": get_system_prompt()}]
    for item in conv.get("messages", []):
        role = item.get("role")
        content = item.get("content")
        if role in {"user", "assistant"} and isinstance(content, str):
            messages.append({"role": role, "content": repair_surrogates(content)})
    return messages


def transcript_for_title(conv: Dict[str, Any]) -> str:
    lines: List[str] = []
    for item in conv.get("messages", []):
        role = item.get("role")
        content = item.get("content", "")
        if not isinstance(content, str):
            continue
        prefix = "用户" if role == "user" else "助手"
        lines.append(f"{prefix}: {repair_surrogates(content)}")
    return "\n".join(lines)


def sanitize_title(title: str) -> str:
    cleaned = title.replace("\n", " ").strip()
    cleaned = cleaned.replace("-", "")
    cleaned = "".join(ch for ch in cleaned if ch not in "\"'\u3000")
    return repair_surrogates(cleaned)[:10]


def maybe_auto_name_conversation(state: Dict[str, Any], conv: Dict[str, Any]) -> None:
    if conv.get("auto_named"):
        return
    assistant_count = sum(1 for msg in conv.get("messages", []) if msg.get("role") == "assistant")
    if assistant_count == 0:
        return
    fallback = "未命名对话"
    messages = [
        {"role": "system", "content": "你是一个对话命名助手，请为下面的对话生成一个不超过 10 个字的标题，不要包含标点或引号。"},
        {"role": "user", "content": transcript_for_title(conv)},
    ]
    title = call_openai(messages, temperature=0.3, fallback=fallback)
    if title.startswith("[error]") or title.startswith("[stub]"):
        sanitized = ""
    else:
        sanitized = sanitize_title(title)
    if not sanitized:
        sanitized = fallback
    conv["prefix"] = sanitized
    conv["name"] = build_conversation_name(sanitized, conv.get("timestamp", timestamp_string()))
    conv["auto_named"] = True
    state["active"] = conv["name"]


def format_conversation(conv: Dict[str, Any]) -> str:
    header = conv.get("name", "")
    lines = [f"对话：{header}"]
    for item in conv.get("messages", []):
        role = item.get("role")
        content = item.get("content", "")
        if not isinstance(content, str):
            continue
        ts = item.get("ts")
        when = ""
        if isinstance(ts, (int, float)):
            when = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(ts))
            when = f"[{when}] "
        role_label = "User" if role == "user" else "Assistant"
        lines.append(f"{when}{role_label}: {repair_surrogates(content)}")
    return "\n".join(lines)


def list_conversation_names(state: Dict[str, Any]) -> List[str]:
    items: List[Tuple[float, str]] = []
    for conv in state.get("conversations", []):
        if not isinstance(conv, dict):
            continue
        created = conv.get("created_at", 0.0)
        name = conv.get("name")
        if isinstance(name, str):
            items.append((created, name))
    items.sort(key=lambda item: item[0], reverse=True)
    return [name for _, name in items]


def handle_call(args: List[str]) -> int:
    if len(args) < 1:
        print("usage: llm call <message>")
        return 1
    prompt = repair_surrogates(" ".join(args))
    state = load_state()
    conv = ensure_active_conversation(state)
    ts = time.time()
    payload = conversation_messages_for_api(conv)
    payload.append({"role": "user", "content": prompt})
    conv.setdefault("messages", []).append({"role": "user", "content": prompt, "ts": ts})
    fallback = f"[stub] echo: {prompt}"
    reply = call_openai(payload, fallback=fallback)
    conv["messages"].append({"role": "assistant", "content": reply, "ts": time.time()})
    maybe_auto_name_conversation(state, conv)
    save_state(state)
    if os.getenv("MYCLI_LLM_SILENT", "0") != "1":
        print(reply)
    return 0


def handle_recall() -> int:
    state = load_state()
    conv = ensure_active_conversation(state)
    messages = conv.get("messages", [])
    if not messages:
        print("当前对话暂无历史记录。")
        return 0
    print(format_conversation(conv))
    return 0


def handle_new() -> int:
    state = load_state()
    conv = create_conversation(state)
    save_state(state)
    print(f"已新建对话：{conv['name']}")
    return 0


def handle_switch(args: List[str]) -> int:
    if len(args) != 1:
        print("usage: llm switch <conversation>")
        return 1
    state = load_state()
    target = args[0]
    conv = find_conversation(state, target)
    if conv is None:
        print(f"对话不存在：{target}")
        return 1
    state["active"] = conv.get("name")
    save_state(state)
    print(f"已切换到对话：{conv['name']}")
    return 0


def handle_rename(args: List[str]) -> int:
    if len(args) != 1:
        print("usage: llm rename <name>")
        return 1
    new_prefix = repair_surrogates(args[0].strip())
    if not new_prefix:
        print("对话名称不能为空。")
        return 1
    state = load_state()
    conv = ensure_active_conversation(state)
    timestamp = conv.get("timestamp", timestamp_string())
    conv["prefix"] = new_prefix
    conv["name"] = build_conversation_name(new_prefix, timestamp)
    conv["auto_named"] = True
    state["active"] = conv["name"]
    save_state(state)
    print(f"当前对话已重命名为：{conv['name']}")
    return 0


def handle_list_names() -> int:
    state = load_state()
    for name in list_conversation_names(state):
        print(name)
    return 0


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: llm <call|recall|new|switch|rename> ...")
        return 1
    command = sys.argv[1]
    if command == "call":
        return handle_call(sys.argv[2:])
    if command == "recall":
        if len(sys.argv) > 2:
            print("usage: llm recall")
            return 1
        return handle_recall()
    if command == "new":
        if len(sys.argv) > 2:
            print("usage: llm new")
            return 1
        return handle_new()
    if command == "switch":
        return handle_switch(sys.argv[2:])
    if command == "rename":
        return handle_rename(sys.argv[2:])
    if command == "list-names":
        return handle_list_names()
    print("unknown llm command")
    return 1


if __name__ == "__main__":
    sys.exit(main())
