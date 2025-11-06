#!/usr/bin/env python3
import json
import os
import sys
import time
from pathlib import Path
from typing import Any, Dict, List

HISTORY_PATH = Path(os.path.expanduser("~/.mycli_llm_history.json"))
DEFAULT_MODEL = os.getenv("MYCLI_LLM_MODEL", "gpt-4o-mini")


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
    with HISTORY_PATH.open("w", encoding="utf-8") as fp:
        json.dump(items, fp, ensure_ascii=False, indent=2)


def call_openai(prompt: str) -> str:
    api_key = os.getenv("OPENAI_API_KEY")
    try:
        from openai import OpenAI  # type: ignore
    except Exception:
        OpenAI = None  # type: ignore

    if not api_key or OpenAI is None:
        return f"[stub] echo: {prompt}"

    try:
        client = OpenAI()
        response = client.responses.create(
            model=DEFAULT_MODEL,
            input=prompt,
        )
        if response.output:
            parts = []
            for item in response.output:
                if not hasattr(item, "content"):
                    continue
                for chunk in getattr(item, "content", []) or []:
                    text = getattr(chunk, "text", None)
                    if text:
                        parts.append(text)
            if parts:
                return "".join(parts)
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
