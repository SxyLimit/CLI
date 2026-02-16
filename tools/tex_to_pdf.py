#!/usr/bin/env python3
"""Compile TeX files into PDF via TeX engines."""

import argparse
import os
import re
import shutil
import subprocess
import tempfile
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

TEMP_EXTENSIONS = [
    ".aux",
    ".bbl",
    ".bcf",
    ".blg",
    ".fdb_latexmk",
    ".fls",
    ".lof",
    ".log",
    ".lot",
    ".nav",
    ".out",
    ".run.xml",
    ".snm",
    ".synctex.gz",
    ".toc",
    ".xdv",
]


def parse_args() -> tuple[argparse.Namespace, list[str]]:
    parser = argparse.ArgumentParser(
        description="Compile TeX into PDF using xelatex/lualatex/pdflatex."
    )
    parser.add_argument("input", type=Path, help="Path to the source TeX file")
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
        help="TeX engine (for example: xelatex, lualatex, pdflatex)",
    )
    return parser.parse_known_args()


def resolve_output(input_path: Path, output_path: Path | None) -> Path:
    if output_path is None:
        return input_path.with_suffix(".pdf")
    if output_path.suffix.lower() != ".pdf":
        return output_path.with_suffix(".pdf")
    return output_path


def has_xelatex_markers(text: str) -> bool:
    lower = text.lower()
    markers = [
        "ts-program = xelatex",
        "\\requirepackage{xltxtra",
        "\\usepackage{xltxtra",
        "\\requirepackage{xecjk",
        "\\usepackage{xecjk",
    ]
    return any(marker in lower for marker in markers)


def local_dependency_paths(input_path: Path, tex_source: str) -> list[Path]:
    dep_paths: list[Path] = []
    seen: set[Path] = set()
    base = input_path.parent

    class_match = re.search(
        r"\\documentclass(?:\[[^\]]*\])?\{([^}]+)\}",
        tex_source,
        flags=re.IGNORECASE,
    )
    if class_match:
        class_name = class_match.group(1).strip()
        if class_name:
            cls_path = (base / f"{class_name}.cls").resolve()
            if cls_path.exists() and cls_path not in seen:
                dep_paths.append(cls_path)
                seen.add(cls_path)

    for pkg_group in re.findall(
        r"\\usepackage(?:\[[^\]]*\])?\{([^}]+)\}",
        tex_source,
        flags=re.IGNORECASE,
    ):
        for name in pkg_group.split(","):
            pkg = name.strip()
            if not pkg:
                continue
            sty_path = (base / f"{pkg}.sty").resolve()
            if sty_path.exists() and sty_path not in seen:
                dep_paths.append(sty_path)
                seen.add(sty_path)

    return dep_paths


def requires_xelatex(input_path: Path, tex_source: str) -> bool:
    if has_xelatex_markers(tex_source):
        return True
    for dep in local_dependency_paths(input_path, tex_source):
        dep_text = read_text_best_effort(dep)
        if has_xelatex_markers(dep_text):
            return True
    return False


def pick_engines(explicit: str | None, require_xelatex: bool) -> list[str]:
    if explicit:
        if require_xelatex and explicit != "xelatex":
            raise SystemExit("this document requires xelatex; please use `--engine xelatex`")
        if shutil.which(explicit) is None:
            raise SystemExit(f"tex engine not found in PATH: {explicit}")
        return [explicit]
    if require_xelatex:
        if shutil.which("xelatex") is None:
            raise SystemExit("this document requires xelatex, but `xelatex` is not found in PATH")
        return ["xelatex"]
    preferred = ["xelatex", "lualatex", "pdflatex"]
    available = [name for name in preferred if shutil.which(name) is not None]
    if not available:
        raise SystemExit("no TeX engine found in PATH (tried: xelatex, lualatex, pdflatex)")
    return available


def cjk_font_candidates() -> list[str]:
    custom = os.environ.get("MYCLI_TEX2PDF_CJK_FONT", "").strip()
    if custom:
        return [custom]
    return CJK_FONT_CANDIDATES


def read_text_best_effort(path: Path) -> str:
    for encoding in ("utf-8", "utf-8-sig", "gb18030"):
        try:
            return path.read_text(encoding=encoding)
        except UnicodeDecodeError:
            continue
    return path.read_text(encoding="utf-8", errors="ignore")


def has_cjk_support(tex_source: str) -> bool:
    lower = tex_source.lower()
    markers = [
        "\\usepackage{ctex",
        "\\documentclass{ctex",
        "\\usepackage{xecjk",
        "\\setcjkmainfont",
        "\\usepackage{cjkutf8",
        "\\begin{cjk",
        "\\usepackage{luatexja",
        "\\setmainjfont",
    ]
    return any(marker in lower for marker in markers)


def source_has_cjk_support(input_path: Path, tex_source: str) -> bool:
    if has_cjk_support(tex_source):
        return True
    for dep in local_dependency_paths(input_path, tex_source):
        dep_text = read_text_best_effort(dep)
        if has_cjk_support(dep_text):
            return True
    return False


def build_injection(engine: str, cjk_font: str | None) -> str:
    if engine == "xelatex":
        lines = ["\\usepackage{fontspec}", "\\usepackage{xeCJK}"]
        if cjk_font:
            lines.append(f"\\setCJKmainfont{{{cjk_font}}}")
        return "\n".join(lines) + "\n"
    if engine == "lualatex":
        lines = ["\\usepackage{fontspec}", "\\usepackage{luatexja-fontspec}"]
        if cjk_font:
            lines.append(f"\\setmainjfont{{{cjk_font}}}")
        return "\n".join(lines) + "\n"
    return ""


def prepare_source_for_engine(
    input_path: Path,
    temp_dir: Path,
    engine: str,
    cjk_font: str | None,
    has_cjk_support_in_source: bool,
) -> Path:
    if engine not in {"xelatex", "lualatex"}:
        return input_path

    source = read_text_best_effort(input_path)
    if has_cjk_support_in_source or has_cjk_support(source):
        return input_path

    injection = build_injection(engine, cjk_font)
    if not injection:
        return input_path

    match = re.search(r"\\documentclass(?:\[[^\]]*\])?\{[^}]+\}", source, flags=re.IGNORECASE)
    if not match:
        return input_path

    insert_at = match.end()
    patched = source[:insert_at] + "\n" + injection + source[insert_at:]
    patched_path = temp_dir / f"{input_path.stem}.mycli.cjk.tex"
    patched_path.write_text(patched, encoding="utf-8")
    return patched_path


def cleanup_temporary_outputs(input_path: Path) -> None:
    stem = input_path.stem
    parent = input_path.parent
    for ext in TEMP_EXTENSIONS:
        candidate = parent / f"{stem}{ext}"
        if candidate.exists():
            candidate.unlink(missing_ok=True)
    minted_dir = parent / f"_minted-{stem}"
    if minted_dir.exists() and minted_dir.is_dir():
        shutil.rmtree(minted_dir, ignore_errors=True)


def run_engine(
    input_path: Path,
    output_path: Path,
    engine: str,
    cjk_font: str | None,
    has_cjk_support_in_source: bool,
    passthrough: list[str],
) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mycli-tex2pdf-") as tmpdir:
        temp_dir = Path(tmpdir)
        texmf_var = temp_dir / "texmf-var"
        texmf_config = temp_dir / "texmf-config"
        texmf_cache = temp_dir / "texmf-cache"
        texmf_var.mkdir(parents=True, exist_ok=True)
        texmf_config.mkdir(parents=True, exist_ok=True)
        texmf_cache.mkdir(parents=True, exist_ok=True)

        compile_source = prepare_source_for_engine(
            input_path,
            temp_dir,
            engine,
            cjk_font,
            has_cjk_support_in_source,
        )
        cmd = [
            engine,
            "-interaction=nonstopmode",
            "-halt-on-error",
            "-file-line-error",
            "-jobname",
            input_path.stem,
            "-output-directory",
            str(temp_dir),
        ]
        if passthrough:
            cmd.extend(passthrough)
        cmd.append(str(compile_source))

        env = os.environ.copy()
        env["TEXMFVAR"] = str(texmf_var)
        env["TEXMFCONFIG"] = str(texmf_config)
        env["TEXMFCACHE"] = str(texmf_cache)

        subprocess.run(
            cmd,
            check=True,
            text=True,
            capture_output=True,
            cwd=str(input_path.parent),
            env=env,
        )

        generated = temp_dir / f"{input_path.stem}.pdf"
        if not generated.exists():
            raise SystemExit(f"expected output not found: {generated}")

        shutil.move(str(generated), str(output_path))


def main() -> None:
    args, passthrough = parse_args()
    input_path = args.input.resolve()
    if not input_path.exists():
        raise SystemExit(f"input tex not found: {args.input}")
    if not input_path.is_file():
        raise SystemExit(f"input is not a file: {args.input}")

    # Remove stale artifacts from previous runs before compiling.
    cleanup_temporary_outputs(input_path)

    source_text = read_text_best_effort(input_path)
    need_xelatex = requires_xelatex(input_path, source_text)
    has_cjk = source_has_cjk_support(input_path, source_text)

    output_path = resolve_output(input_path, args.output).resolve()
    engines = pick_engines(args.engine, need_xelatex)

    errors: list[str] = []
    for engine in engines:
        attempts: list[tuple[str | None, str]] = []
        if engine in {"xelatex", "lualatex"} and not has_cjk and not need_xelatex:
            for font_name in cjk_font_candidates():
                attempts.append((font_name, f"{engine} + CJK font {font_name}"))
            attempts.append((None, f"{engine} + default-font"))
        else:
            attempts.append((None, engine))

        for cjk_font, label in attempts:
            try:
                run_engine(
                    input_path,
                    output_path,
                    engine,
                    cjk_font,
                    has_cjk,
                    passthrough,
                )
                cleanup_temporary_outputs(input_path)
                print(output_path)
                return
            except subprocess.CalledProcessError as exc:
                stderr = (exc.stderr or "").strip()
                stdout = (exc.stdout or "").strip()
                detail = stderr or stdout or "tex engine failed"
                errors.append(f"[{label}] {detail}")
            finally:
                cleanup_temporary_outputs(input_path)

    raise SystemExit(
        "all tex engines failed. attempted: "
        + ", ".join(engines)
        + "\n"
        + "\n".join(errors)
    )


if __name__ == "__main__":
    main()
