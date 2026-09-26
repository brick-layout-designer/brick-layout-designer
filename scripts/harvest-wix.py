#!/usr/bin/env python3
"""Generate Harvested.wxs for the Windows MSI build.

WiX v4's CLI doesn't ship heat.exe, so we synthesise the harvested
component group ourselves. The previous bash version put every file
into INSTALLFOLDER as a flat component, which broke Qt at runtime:
qwindows.dll lives under platforms/ in the staging tree, but the MSI
installed it next to the .exe, so QGuiApplication couldn't load any
platform plugin and the app died with "no Qt platform plugin could
be initialized".

This script walks staging/, mirrors the directory tree as nested
<Directory> elements under INSTALLFOLDER, and emits one <Component>
per file with Directory= pointing at the matching subdir id.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys


def safe_id(rel: str, prefix: str) -> str:
    # MSI identifiers are capped at 72 chars, and mangling the path alone
    # collides ("a-b" and "a_b" both become "a_b"). Hash the full relative
    # path for uniqueness/stability; keep a short readable tail for debugging.
    digest = hashlib.sha1(rel.encode("utf-8")).hexdigest()[:16]
    tail = re.sub(r"[^A-Za-z0-9]", "_", rel.rsplit("/", 1)[-1])[:32]
    return f"{prefix}_{digest}_{tail}"


def build_tree(staging: str, exclude_top: set[str]) -> tuple[dict, list[tuple[str, str]]]:
    """Return (dir_tree, files).

    dir_tree is a nested dict mirroring staging/ subdirs.
    files is a list of (rel_posix_path, parent_rel_posix_path).
    """
    tree: dict = {}
    files: list[tuple[str, str]] = []
    for root, dirs, fnames in os.walk(staging):
        rel_root = os.path.relpath(root, staging).replace("\\", "/")
        if rel_root == ".":
            rel_root = ""
        # Build dir tree node for this root
        node = tree
        if rel_root:
            for part in rel_root.split("/"):
                node = node.setdefault(part, {})
        for d in dirs:
            node.setdefault(d, {})
        for f in fnames:
            if not rel_root and f in exclude_top:
                continue
            rel = f if not rel_root else f"{rel_root}/{f}"
            files.append((rel, rel_root))
    return tree, files


def emit_dirs(node: dict, parent_rel: str, indent: str) -> list[str]:
    out: list[str] = []
    for name in sorted(node):
        rel = name if not parent_rel else f"{parent_rel}/{name}"
        did = safe_id(rel, "Dir")
        out.append(f'{indent}<Directory Id="{did}" Name="{name}">')
        out.extend(emit_dirs(node[name], rel, indent + "  "))
        out.append(f"{indent}</Directory>")
    return out


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--staging", required=True, help="Path to staging directory")
    p.add_argument("--out", required=True, help="Output .wxs path")
    p.add_argument(
        "--exclude-top",
        action="append",
        default=[],
        help="Top-level filename to skip (declared elsewhere). Repeatable.",
    )
    args = p.parse_args()

    if not os.path.isdir(args.staging):
        print(f"staging dir not found: {args.staging}", file=sys.stderr)
        return 1

    tree, files = build_tree(args.staging, set(args.exclude_top))

    lines: list[str] = []
    lines.append('<?xml version="1.0" encoding="utf-8"?>')
    lines.append('<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">')
    lines.append("  <Fragment>")
    lines.append('    <DirectoryRef Id="INSTALLFOLDER">')
    lines.extend(emit_dirs(tree, "", "      "))
    lines.append("    </DirectoryRef>")
    lines.append("  </Fragment>")
    lines.append("")
    lines.append("  <Fragment>")
    lines.append('    <ComponentGroup Id="HarvestedComponents">')
    for rel, parent in files:
        parent_id = "INSTALLFOLDER" if not parent else safe_id(parent, "Dir")
        cid = safe_id(rel, "Harv")
        src = "staging\\" + rel.replace("/", "\\")
        lines.append(f'      <Component Id="{cid}" Directory="{parent_id}" Guid="*">')
        lines.append(f'        <File Id="{cid}_F" Source="{src}" KeyPath="yes" />')
        lines.append("      </Component>")
    lines.append("    </ComponentGroup>")
    lines.append("  </Fragment>")
    lines.append("</Wix>")

    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
