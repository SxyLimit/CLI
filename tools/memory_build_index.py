#!/usr/bin/env python3
import argparse
import hashlib
import json
import sys
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Any

SUPPORTED_SUFFIXES = {".md", ".txt"}


def load_existing_index(index_path: Path) -> Dict[str, Dict[str, Any]]:
    if not index_path.exists():
        return {}
    data: Dict[str, Dict[str, Any]] = {}
    try:
        with index_path.open("r", encoding="utf-8") as fp:
            for line in fp:
                line = line.strip()
                if not line:
                    continue
                obj = json.loads(line)
                rel = obj.get("rel_path") or obj.get("id") or ""
                data[rel] = obj
    except Exception:
        return {}
    return data


def summarize_text(text: str, min_len: int, max_len: int) -> str:
    cleaned = " ".join(text.strip().split())
    if not cleaned:
        return "（空内容）"
    if len(cleaned) > max_len:
        cleaned = cleaned[:max_len]
    if len(cleaned) < min_len and len(text) >= min_len:
        cleaned = text.strip().replace("\n", " ")[:min_len]
    return cleaned


def summarize_directory(children: List[Dict[str, Any]]) -> str:
    if not children:
        return "目录为空，可通过 memory import 导入笔记。"
    titles = [child.get("title") or child.get("rel_path") for child in children]
    titles = [t for t in titles if t]
    preview = "、".join(titles[:4])
    extra = max(len(titles) - 4, 0)
    if extra > 0:
        return f"包含 {preview} 等 {len(titles)} 条内容。"
    return f"包含 {preview}。"


def compute_title(path: Path, content: str) -> str:
    for line in content.splitlines():
        stripped = line.strip()
        if stripped.startswith("#"):
            return stripped.lstrip("# ") or path.stem
    return path.stem


def load_overrides(path: Path) -> Dict[str, Dict[str, Any]]:
    if not path or not path.exists():
        return {}
    try:
        with path.open("r", encoding="utf-8") as fp:
            data = json.load(fp)
            if isinstance(data, dict):
                return data
    except Exception:
        return {}
    return {}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--index", required=True)
    parser.add_argument("--personal-subdir", default="personal")
    parser.add_argument("--lang", default="zh")
    parser.add_argument("--min-len", type=int, default=50)
    parser.add_argument("--max-len", type=int, default=100)
    parser.add_argument("--bootstrap-depth", type=int, default=1)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--overrides")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    index_path = Path(args.index).resolve()
    personal_prefix = args.personal_subdir.strip("/") or "personal"
    overrides = load_overrides(Path(args.overrides)) if args.overrides else {}

    existing = load_existing_index(index_path)
    nodes: List[Dict[str, Any]] = []
    now = datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")

    all_dirs = {""}
    for path in root.rglob("*"):
        rel = path.relative_to(root).as_posix()
        if path.is_dir():
            all_dirs.add(rel)

    for path in root.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if Path(rel).suffix.lower() not in SUPPORTED_SUFFIXES:
            continue
        try:
            raw = path.read_text(encoding="utf-8")
        except Exception:
            raw = ""
        digest = hashlib.sha256(raw.encode("utf-8")).hexdigest()
        depth = 1 if "/" not in rel else len(rel.split("/"))
        parent = "" if "/" not in rel else rel.rsplit("/", 1)[0]
        bucket = "personal" if rel.startswith(f"{personal_prefix}/") else "knowledge"
        overrides_entry = overrides.get(rel, {}) if isinstance(overrides, dict) else {}
        old = existing.get(rel)
        summary = summarize_text(raw, args.min_len, args.max_len)
        title = compute_title(path, raw)
        created_at = old.get("created_at") if isinstance(old, dict) else now
        updated_at = now
        if old and not args.force and old.get("hash") == digest:
            summary = old.get("summary", summary)
            title = old.get("title", title)
            created_at = old.get("created_at", created_at)
        file_expose_limit = max(args.bootstrap_depth, 0) + 1
        node = {
            "id": rel,
            "kind": "file",
            "rel_path": rel,
            "parent": parent,
            "depth": depth,
            "title": title,
            "summary": summary,
            "is_personal": bucket == "personal",
            "bucket": bucket,
            "eager_expose": depth <= file_expose_limit,
            "hash": digest,
            "size_bytes": path.stat().st_size,
            "token_est": max(len(raw) // 4, 1),
            "created_at": created_at,
            "updated_at": updated_at,
        }
        if overrides_entry:
            node["source"] = overrides_entry.get("source", overrides_entry)
        nodes.append(node)
        all_dirs.add(parent)

    dir_nodes: Dict[str, Dict[str, Any]] = {}
    sorted_dirs = sorted(all_dirs, key=lambda d: d.count("/"))
    dir_expose_limit = max(args.bootstrap_depth, 0)
    for rel in sorted_dirs:
        if rel == "":
            parent = None
            depth = 0
        else:
            depth = len(rel.split("/"))
            parent = "" if depth == 1 else rel.rsplit("/", 1)[0]
        bucket = "personal" if rel.startswith(f"{personal_prefix}") and rel else "knowledge"
        children = [node for node in nodes if node.get("parent") == rel]
        title = rel if rel else "Memory 根"
        summary = summarize_directory(children)
        node = {
            "id": rel,
            "kind": "dir",
            "rel_path": rel,
            "parent": parent,
            "depth": depth,
            "title": title,
            "summary": summary,
            "is_personal": bucket == "personal",
            "bucket": bucket if rel else "other",
            "eager_expose": depth <= dir_expose_limit,
            "children": [child.get("rel_path") for child in children],
            "created_at": existing.get(rel, {}).get("created_at", now),
            "updated_at": now,
        }
        dir_nodes[rel] = node
    full_nodes = list(dir_nodes.values()) + [node for node in nodes if node["kind"] == "file"]
    full_nodes.sort(key=lambda n: (n["depth"], n["rel_path"]))

    index_path.parent.mkdir(parents=True, exist_ok=True)
    with index_path.open("w", encoding="utf-8") as fp:
        for node in full_nodes:
            fp.write(json.dumps(node, ensure_ascii=False) + "\n")
    print(f"[memory] indexed {len(full_nodes)} nodes under {root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
