#!/usr/bin/env python3
"""Compile Markdown files into PDF via pandoc."""

import argparse
import os
import shutil
import subprocess
from pathlib import Path

CJK_FONT_CANDIDATES = [
    "PingFang SC",
    "Hiragino Sans GB",
    "Songti SC",
    "Heiti SC",
    "STHeiti",
    "Noto Serif CJK SC",
    "Noto Sans CJK SC",
    "Source Han Serif SC",
    "Source Han Sans SC",
    "WenQuanYi Zen Hei",
    "SimSun",
    "SimHei",
    "Microsoft YaHei",
]


def parse_args() -> tuple[argparse.Namespace, list[str]]:
    parser = argparse.ArgumentParser(
        description="Compile Markdown into PDF using pandoc."
    )
    parser.add_argument("input", type=Path, help="Path to the source Markdown file")
    parser.add_argument(
        "output",
        nargs="?",
        type=Path,
        help="Output PDF path (default: same name as input)",
    )
    parser.add_argument(
        "--engine",
        type=str,
        default=None,
        help="PDF engine passed to pandoc (for example: xelatex, lualatex, pdflatex)",
    )
    parser.add_argument("--toc", action="store_true", help="Enable table of contents")
    parser.add_argument(
        "--number-sections",
        action="store_true",
        help="Enable section numbering",
    )
    return parser.parse_known_args()


def resolve_output(input_path: Path, output_path: Path | None) -> Path:
    if output_path is None:
        return input_path.with_suffix(".pdf")
    if output_path.suffix.lower() != ".pdf":
        return output_path.with_suffix(".pdf")
    return output_path


def pick_engines(explicit: str | None) -> list[str | None]:
    if explicit:
        return [explicit]
    preferred = ["xelatex", "lualatex", "pdflatex"]
    available = [name for name in preferred if shutil.which(name) is not None]
    if available:
        return available
    return [None]


def run_pandoc(
    input_path: Path,
    output_path: Path,
    engine: str | None,
    cjk_font: str | None,
    args: argparse.Namespace,
    passthrough: list[str],
) -> None:
    cmd = ["pandoc", str(input_path), "-o", str(output_path)]
    if engine:
        cmd.append(f"--pdf-engine={engine}")
    if cjk_font:
        cmd.extend(["-V", f"CJKmainfont={cjk_font}"])
    if args.toc:
        cmd.append("--toc")
    if args.number_sections:
        cmd.append("--number-sections")
    if passthrough:
        cmd.extend(passthrough)

    subprocess.run(cmd, check=True, text=True, capture_output=True)


def cjk_font_candidates() -> list[str]:
    custom = os.environ.get("MYCLI_MD2PDF_CJK_FONT", "").strip()
    if custom:
        return [custom]
    return CJK_FONT_CANDIDATES


def main() -> None:
    args, passthrough = parse_args()

    if shutil.which("pandoc") is None:
        raise SystemExit("pandoc not found in PATH")
    if not args.input.exists():
        raise SystemExit(f"input markdown not found: {args.input}")
    if not args.input.is_file():
        raise SystemExit(f"input is not a file: {args.input}")

    output_path = resolve_output(args.input, args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    engines = pick_engines(args.engine)
    errors: list[str] = []
    for engine in engines:
        attempts: list[tuple[str | None, str]] = []
        if engine in {"xelatex", "lualatex"}:
            for font_name in cjk_font_candidates():
                attempts.append((font_name, f"{engine} + CJKmainfont={font_name}"))
            attempts.append((None, f"{engine} + default-font"))
        else:
            attempts.append((None, engine if engine else "pandoc-default"))

        for cjk_font, label in attempts:
            try:
                run_pandoc(args.input, output_path, engine, cjk_font, args, passthrough)
                print(output_path)
                return
            except subprocess.CalledProcessError as exc:
                stderr = (exc.stderr or "").strip()
                stdout = (exc.stdout or "").strip()
                detail = stderr or stdout or "pandoc failed"
                errors.append(f"[{label}] {detail}")

    if args.engine:
        raise SystemExit(errors[-1])
    attempted = ", ".join(engine if engine else "pandoc-default" for engine in engines)
    raise SystemExit(
        "all pdf engines failed. attempted: "
        + attempted
        + "\n"
        + "\n".join(errors)
    )


if __name__ == "__main__":
    main()
