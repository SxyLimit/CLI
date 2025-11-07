#!/usr/bin/env python3
"""Convert images into structured text for MyCLI's themed art renderer."""
import argparse
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover - informative failure for missing pillow
    raise SystemExit("Pillow is required to run this tool (pip install Pillow)") from exc


def positive_int(value: str) -> int:
    ivalue = int(value)
    if ivalue <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return ivalue


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Convert an image into the structured text format understood by MyCLI's "
            "themed art renderer."
        )
    )
    parser.add_argument("input", type=Path, help="Path to the source image")
    parser.add_argument("output", type=Path, help="Path to the generated structured text file")
    parser.add_argument(
        "--max-width",
        type=positive_int,
        default=None,
        help="Resize the image so that the width does not exceed this value",
    )
    parser.add_argument(
        "--max-height",
        type=positive_int,
        default=None,
        help="Resize the image so that the height does not exceed this value",
    )
    parser.add_argument(
        "--no-keep-aspect",
        action="store_true",
        help="Allow resizing to stretch the image instead of preserving the aspect ratio",
    )
    return parser.parse_args()


def resize_image(img: Image.Image, max_w: int | None, max_h: int | None, keep_aspect: bool) -> Image.Image:
    if max_w is None and max_h is None:
        return img

    width, height = img.size
    new_width, new_height = width, height

    if keep_aspect:
        scale = 1.0
        if max_w is not None:
            scale = min(scale, max_w / width)
        if max_h is not None:
            scale = min(scale, max_h / height)
        if scale >= 1.0:
            return img
        new_width = max(1, int(round(width * scale)))
        new_height = max(1, int(round(height * scale)))
    else:
        if max_w is not None:
            new_width = min(width, max_w)
        if max_h is not None:
            new_height = min(height, max_h)

    if (new_width, new_height) == (width, height):
        return img
    return img.resize((new_width, new_height), Image.LANCZOS)


def main() -> None:
    args = parse_args()

    if not args.input.exists():
        raise SystemExit(f"input image not found: {args.input}")

    image = Image.open(args.input).convert("RGB")
    image = resize_image(image, args.max_width, args.max_height, not args.no_keep_aspect)

    args.output.parent.mkdir(parents=True, exist_ok=True)

    with args.output.open("w", encoding="utf-8") as fh:
        width, height = image.size
        fh.write(f"{width} {height}\n")
        pixels = image.load()
        for y in range(height):
            for x in range(width):
                r, g, b = pixels[x, y]
                fh.write(f"{r} {g} {b}\n")


if __name__ == "__main__":
    main()
