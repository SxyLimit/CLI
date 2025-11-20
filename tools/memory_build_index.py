#!/usr/bin/env python3
import argparse
import hashlib
import json
import sys
from datetime import datetime
from pathlib import Path
from typing import Dict, List

SUPPORTED_SUFFIXES = {".md", ".txt"}


def summarize_text(text: str, min_len: int, max_len: int) -> str:
    cleaned = " ".join(text.strip().split())
    if not cleaned:
        return "空文档，暂无可用摘要。"
    if len(cleaned) > max_len:
        cleaned = cleaned[:max_len]
    if len(cleaned) < min_len and len(text) >= min_len:
        cleaned = text.strip().replace("\n", " ")
        cleaned = cleaned[:max_len]
    if len(cleaned) < min_len:
        cleaned = (cleaned + " ") * ((min_len // max_len) + 1)
        cleaned = cleaned[:min_len]
    return cleaned


def hash_text(text: str) -> str:
    return "sha256:" + hashlib.sha256(text.encode("utf-8", errors="ignore")).hexdigest()


def rel_from_root(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix()


def eager_expose_for(path_parts: List[str], is_file: bool) -> bool:
    if not path_parts:
        return True
    if len(path_parts) == 1:
        return True
    if len(path_parts) == 2 and is_file:
        return True
    return False


def build_index(root: Path, index_path: Path, personal: str, lang: str, min_len: int, max_len: int) -> None:
    nodes: Dict[str, Dict] = {}

    def add_node(node: Dict) -> None:
        nodes[node["rel_path"]] = node

    now = datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")

    add_node(
        {
            "id": "",
            "kind": "dir",
            "rel_path": "",
            "parent": None,
            "depth": 0,
            "title": "Memory 根",
            "summary": "整体记忆系统的概览，总结 personal 与 knowledge 下的主题入口。",
            "is_personal": False,
            "bucket": "other",
            "eager_expose": True,
            "updated_at": now,
            "created_at": now,
        }
    )

    for path in sorted(root.rglob("*")):
        if path.is_dir():
            rel = rel_from_root(root, path)
            parts = rel.split("/") if rel else []
            depth = len(parts)
            node = {
                "id": rel,
                "kind": "dir",
                "rel_path": rel,
                "parent": "" if depth == 0 else "/".join(parts[:-1]),
                "depth": depth,
                "title": path.name if path.name else "root",
                "summary": "",
                "is_personal": rel.startswith(personal + "/") or rel == personal,
                "bucket": "personal" if rel.startswith(personal) else "knowledge",
                "eager_expose": eager_expose_for(parts, False),
                "created_at": now,
                "updated_at": now,
            }
            add_node(node)
            continue
        if path.is_file() and path.suffix.lower() in SUPPORTED_SUFFIXES:
            rel = rel_from_root(root, path)
            parts = rel.split("/") if rel else []
            depth = len(parts)
            try:
                raw = path.read_text(encoding="utf-8")
            except Exception:
                raw = ""
            snippet = raw[:2000]
            summary = summarize_text(snippet, min_len, max_len)
            hashed = hash_text(raw)
            node = {
                "id": rel,
                "kind": "file",
                "rel_path": rel,
                "parent": "" if depth == 0 else "/".join(parts[:-1]),
                "depth": depth,
                "title": path.stem,
                "summary": summary,
                "is_personal": rel.startswith(personal + "/") or rel == personal,
                "bucket": "personal" if rel.startswith(personal) else "knowledge",
                "eager_expose": eager_expose_for(parts, True),
                "hash": hashed,
                "size_bytes": path.stat().st_size if path.exists() else 0,
                "token_est": max(1, len(raw) // 4) if raw else 0,
                "created_at": now,
                "updated_at": now,
            }
            add_node(node)

    # Fill directory summaries based on children
    for rel, node in list(nodes.items()):
        if node.get("kind") != "dir":
            continue
        children = [n for n in nodes.values() if n.get("parent") == rel]
        node["children"] = [c["rel_path"] for c in children]
        if not node.get("summary"):
            titles = [c.get("title", c.get("rel_path")) for c in children[:6]]
            joined = "；".join(titles)
            node["summary"] = summarize_text(joined or "目录", min_len, max_len)
        nodes[rel] = node

    with index_path.open("w", encoding="utf-8") as fp:
        for rel in sorted(nodes.keys()):
            fp.write(json.dumps(nodes[rel], ensure_ascii=False) + "\n")



def main() -> int:
    parser = argparse.ArgumentParser(description="Build or refresh the memory index")
    parser.add_argument("--root", required=True, help="Memory root directory")
    parser.add_argument("--index", required=True, help="Index file path")
    parser.add_argument("--personal", default="personal", help="Personal subdir name")
    parser.add_argument("--lang", default="", help="Summary language (unused stub)")
    parser.add_argument("--min-len", type=int, default=50)
    parser.add_argument("--max-len", type=int, default=100)
    args = parser.parse_args()

    root = Path(args.root).expanduser().resolve()
    index_path = Path(args.index).expanduser().resolve()
    if not root.exists():
        root.mkdir(parents=True, exist_ok=True)
    build_index(root, index_path, args.personal, args.lang or "en", args.min_len, args.max_len)
    print(f"index written to {index_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
