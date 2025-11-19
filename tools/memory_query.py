#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def synthesize_answer(question: str, documents) -> str:
    pieces = []
    for doc in documents:
        path = doc.get("path", "unknown")
        summary = (doc.get("summary") or "").strip()
        content = (doc.get("content") or "").strip()
        snippet = summary or content[:160]
        snippet = snippet.strip()
        if snippet:
            pieces.append(f"{snippet}（来源：{path}）")
    if not pieces:
        return "已检索到相关记忆，但这些条目缺少摘要内容，请直接打开文件查看细节。"
    return "；".join(pieces)


def build_answer(question: str, documents):
    question = question.strip() or "（未提供问题）"
    if not documents:
        return "\n".join([
            "未在 Memory 中找到可用的记忆，请先导入或记录相关内容。",
            f"问题：{question}"
        ])
    answer_body = synthesize_answer(question, documents)
    excerpts = []
    for doc in documents:
        path = doc.get("path", "unknown")
        summary = (doc.get("summary") or "").strip()
        content = (doc.get("content") or "").strip()
        excerpt = summary or content[:200]
        excerpt = excerpt.strip() or "（该文档没有摘要）"
        excerpts.append(f"- {path}: {excerpt}")
    lines = [
        "仅基于以下记忆作答：",
        f"问题：{question}",
        "",
        "回答：",
        answer_body,
        "",
        "引用的记忆片段：",
        *excerpts
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    args = parser.parse_args()
    data = json.loads(Path(args.input).read_text(encoding="utf-8"))
    question = data.get("question", "")
    documents = data.get("documents", [])
    answer = build_answer(question, documents)
    print(answer)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
