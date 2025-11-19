#!/usr/bin/env python3
import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path
from typing import Dict, List

TOOLS_DIR = Path(__file__).resolve().parent
SUPPORTED_SUFFIXES = {".md", ".txt"}


def run_builder(root: Path,
                index: Path,
                personal: str,
                lang: str,
                min_len: int,
                max_len: int,
                bootstrap_depth: int,
                extra: List[str]) -> int:
    builder = TOOLS_DIR / "memory_build_index.py"
    cmd = [sys.executable, str(builder), "--root", str(root), "--index", str(index), "--personal-subdir", personal,
           "--lang", lang, "--min-len", str(min_len), "--max-len", str(max_len),
           "--bootstrap-depth", str(bootstrap_depth)]
    cmd.extend(extra)
    return subprocess.call(cmd)


def copy_file(src: Path, dest: Path, mode: str) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if mode == "copy":
        shutil.copy2(src, dest)
    elif mode in {"link", "mirror"}:
        try:
            if dest.exists() or dest.is_symlink():
                if dest.is_dir():
                    shutil.rmtree(dest)
                else:
                    dest.unlink()
        except Exception:
            pass
        try:
            os.symlink(src, dest)
            return
        except (OSError, NotImplementedError):
            shutil.copy2(src, dest)
    else:
        shutil.copy2(src, dest)


def discover_files(src: Path) -> List[Path]:
    if src.is_file():
        return [src] if src.suffix.lower() in SUPPORTED_SUFFIXES else []
    files: List[Path] = []
    for path in src.rglob("*"):
        if path.is_file() and path.suffix.lower() in SUPPORTED_SUFFIXES:
            files.append(path)
    return files


def make_relative(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def handle_import(args: argparse.Namespace) -> int:
    root = Path(args.root).resolve()
    index = Path(args.index).resolve()
    personal_subdir = args.personal_subdir.strip("/") or "personal"
    src = Path(args.src).resolve()
    if not src.exists():
        print(f"[memory] source not found: {src}", file=sys.stderr)
        return 1
    files = discover_files(src)
    if not files:
        print("[memory] no markdown or text files found to import", file=sys.stderr)
        return 1
    target_base = root / (personal_subdir if args.personal else "knowledge")
    category = args.category or (src.stem if src.is_dir() else "misc")
    target_base = target_base / category
    overrides: Dict[str, Dict[str, Dict[str, str]]] = {}
    for file in files:
        rel_inside = file.relative_to(src).as_posix() if src.is_dir() else file.name
        dest = target_base / rel_inside
        copy_file(file, dest, args.mode)
        rel_path = make_relative(dest, root)
        overrides[rel_path] = {"source": {"import_path": str(file), "import_mode": args.mode}}
    with tempfile.NamedTemporaryFile("w", delete=False, encoding="utf-8", suffix=".json") as fp:
        json_path = Path(fp.name)
        fp.write(json.dumps(overrides, ensure_ascii=False))
    extra = []
    if args.force:
        extra.append("--force")
    extra.extend(["--overrides", str(json_path)])
    rc = run_builder(root, index, personal_subdir, args.lang, args.min_len, args.max_len, args.bootstrap_depth, extra)
    json_path.unlink(missing_ok=True)
    if rc != 0:
        return rc
    print(f"[memory] imported {len(files)} files into {target_base}")
    return 0


def capture_editor_text() -> str:
    editor = os.environ.get("EDITOR") or "vi"
    try:
        parts = shlex.split(editor)
    except ValueError:
        parts = [editor]
    if not parts:
        parts = ["vi"]
    with tempfile.NamedTemporaryFile("w+", delete=False, suffix=".md", encoding="utf-8") as tmp:
        path = Path(tmp.name)
    try:
        subprocess.call(parts + [str(path)])
    except FileNotFoundError:
        subprocess.call(["vi", str(path)])
    try:
        content = path.read_text(encoding="utf-8")
    except Exception:
        content = ""
    path.unlink(missing_ok=True)
    return content.strip()


def handle_note(args: argparse.Namespace) -> int:
    root = Path(args.root).resolve()
    personal_subdir = args.personal_subdir.strip("/") or "personal"
    index = Path(args.index).resolve()
    notes_dir = root / personal_subdir / "notes"
    notes_dir.mkdir(parents=True, exist_ok=True)
    content = " ".join(args.text or []).strip()
    if args.editor:
        content = capture_editor_text()
    if not content:
        print("[memory] note content is empty", file=sys.stderr)
        return 1
    stamp = datetime.utcnow().strftime("%Y-%m-%d-%H%M%S")
    counter = 1
    while True:
        name = f"{stamp}-{counter:03d}.md"
        dest = notes_dir / name
        if not dest.exists():
            break
        counter += 1
    dest.write_text(content + "\n", encoding="utf-8")
    rel_path = make_relative(dest, root)
    overrides = {rel_path: {"source": {"import_path": str(dest), "import_mode": "note"}}}
    with tempfile.NamedTemporaryFile("w", delete=False, encoding="utf-8", suffix=".json") as fp:
        json_path = Path(fp.name)
        fp.write(json.dumps(overrides, ensure_ascii=False))
    extra = ["--overrides", str(json_path)]
    rc = run_builder(root, index, personal_subdir, args.lang, args.min_len, args.max_len, args.bootstrap_depth, extra)
    json_path.unlink(missing_ok=True)
    if rc != 0:
        return rc
    print(f"[memory] created personal note {dest}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--index", required=True)
    parser.add_argument("--personal-subdir", default="personal")
    parser.add_argument("--lang", default="zh")
    parser.add_argument("--min-len", type=int, default=50)
    parser.add_argument("--max-len", type=int, default=100)
    parser.add_argument("--bootstrap-depth", type=int, default=1)
    sub = parser.add_subparsers(dest="command")

    imp = sub.add_parser("import")
    imp.add_argument("src")
    imp.add_argument("--personal", action="store_true")
    imp.add_argument("--category")
    imp.add_argument("--mode", choices=["copy", "link", "mirror"], default="copy")
    imp.add_argument("--force", action="store_true")

    note = sub.add_parser("note")
    note.add_argument("text", nargs="*")
    note.add_argument("-e", "--editor", action="store_true")

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.command == "import":
        return handle_import(args)
    if args.command == "note":
        return handle_note(args)
    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
