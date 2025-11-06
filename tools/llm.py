#!/usr/bin/env python3
import json
import os
import sys
import time
from pathlib import Path
from typing import Any, Dict, List

HISTORY_PATH = Path(os.path.expanduser("~/.mycli_llm_history.json"))
ENV_PATH = Path(__file__).resolve().parent.parent / ".env"

DEFAULT_SYSTEM_PROMPT = (
    "你是 Kimi，由 Moonshot AI 提供的人工智能助手，你更擅长中文和英文的对话。"
    "你会为用户提供安全，有帮助，准确的回答。同时，你会拒绝一切涉及恐怖主义，"
    "种族歧视，黄色暴力等问题的回答。Moonshot AI 为专有名词，不可翻译成其他语言。"
)


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


def load_history() -> List[Dict[str, Any]]:
    if not HISTORY_PATH.exists():
        return []
    try:
        with HISTORY_PATH.open("r", encoding="utf-8") as fp:
            return json.load(fp)
    except Exception:
        return []


def save_history(items: List[Dict[str, Any]]) -> None:
    HISTORY_PATH.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = HISTORY_PATH.with_suffix(".tmp")
    try:
        with tmp_path.open("w", encoding="utf-8") as fp:
            json.dump(items, fp, ensure_ascii=False, indent=2)
            fp.flush()
            os.fsync(fp.fileno())
        os.replace(tmp_path, HISTORY_PATH)
    finally:
        if tmp_path.exists():
            try:
                tmp_path.unlink()
            except OSError:
                pass


def call_openai(prompt: str) -> str:
    api_key = os.getenv("LLM_API_KEY", "").strip() or os.getenv("MOONSHOT_API_KEY", "").strip()
    base_url = os.getenv("LLM_BASE_URL", "https://api.moonshot.cn/v1")
    model = os.getenv("LLM_MODEL", "kimi-k2-turbo-preview")
    system_prompt = os.getenv("LLM_SYSTEM_PROMPT", DEFAULT_SYSTEM_PROMPT)
    temperature_raw = os.getenv("LLM_TEMPERATURE", "0.6")
    try:
        temperature = float(temperature_raw)
    except ValueError:
        temperature = 0.6

    try:
        from openai import OpenAI  # type: ignore
    except Exception:
        OpenAI = None  # type: ignore

    if not api_key or OpenAI is None:
        return f"[stub] echo: {prompt}"

    try:
        client = OpenAI(api_key=api_key, base_url=base_url)
        completion = client.chat.completions.create(
            model=model,
            messages=[
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": prompt},
            ],
            temperature=temperature,
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


def handle_call(args: List[str]) -> int:
    if len(args) < 1:
        print("usage: llm call <message>")
        return 1
    prompt = " ".join(args)
    reply = call_openai(prompt)
    entry = {
        "ts": time.time(),
        "prompt": prompt,
        "reply": reply,
    }
    history = load_history()
    history.append(entry)
    save_history(history)
    if os.getenv("MYCLI_LLM_SILENT", "0") != "1":
        print(reply)
    return 0


def handle_recall() -> int:
    history = load_history()
    if not history:
        print("No previous LLM calls recorded.")
        return 0
    last = history[-1]
    ts = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(last.get("ts", time.time())))
    print(f"[{ts}] {last.get('prompt', '')}")
    reply = last.get("reply", "")
    if reply:
        print(reply)
    return 0


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: llm <call|recall> ...")
        return 1
    command = sys.argv[1]
    if command == "call":
        return handle_call(sys.argv[2:])
    if command == "recall":
        if len(sys.argv) > 2:
            print("usage: llm recall")
            return 1
        return handle_recall()
    print("unknown llm command")
    return 1


if __name__ == "__main__":
    sys.exit(main())
