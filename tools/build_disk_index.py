#!/usr/bin/env python3
"""Build the sequential SD-card disk index consumed by EspAppleII."""

from __future__ import annotations

import argparse
from pathlib import Path


HEADER = "ESPAPPLEII-DISK-INDEX-1"
DISK_SIZE = 143_360
INDEX_NAME = "apple2-index.txt"
# Firmware stores "/SD/apple2/disks/" plus the relative path in 384 bytes.
MAX_RELATIVE_PATH_BYTES = 364


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Index standard 140K Apple II .dsk images for EspAppleII"
    )
    parser.add_argument(
        "root",
        type=Path,
        help="folder that will become /SD/apple2/disks on the SD card",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help=f"output path (default: ROOT/{INDEX_NAME})",
    )
    args = parser.parse_args()

    root = args.root.expanduser().resolve()
    output = (args.output or root / INDEX_NAME).expanduser().resolve()
    if not root.is_dir():
        parser.error(f"not a directory: {root}")

    entries: list[str] = []
    wrong_size = 0
    too_long = 0
    for image in root.rglob("*"):
        if not image.is_file() or image.suffix.casefold() != ".dsk":
            continue
        if image.stat().st_size != DISK_SIZE:
            wrong_size += 1
            continue
        relative = image.relative_to(root).as_posix()
        if len(relative.encode("utf-8")) > MAX_RELATIVE_PATH_BYTES:
            too_long += 1
            continue
        entries.append(relative)

    entries.sort(key=str.casefold)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text(HEADER + "\n" + "\n".join(entries) + "\n", encoding="utf-8")
    temporary.replace(output)

    print(f"Wrote {len(entries)} images to {output}")
    print(f"Skipped {wrong_size} non-140K .dsk files and {too_long} overlong paths")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
