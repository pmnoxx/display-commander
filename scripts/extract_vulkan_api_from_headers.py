#!/usr/bin/env python3
"""
Extract the list of Vulkan API command names from official Khronos Vulkan headers.
Output is a plain list (one vk* name per line) for use as a spec to audit vulkan-1 proxy.

Source: Vulkan-Headers (vulkan_core.h, optionally vulkan_win32.h).
Not derived from this project's vulkan-1 proxy or exports.

Usage:
  python scripts/extract_vulkan_api_from_headers.py [--win32] [--output FILE]
  Default output: scripts/specs/vulkan-1_official_api_list.txt
"""

import argparse
import re
import os

# Callback typedefs in vulkan_core.h that are not API entry points (exclude from list).
PFN_CALLBACKS = {
    "vkAllocationFunction",
    "vkFreeFunction",
    "vkReallocationFunction",
    "vkInternalAllocationNotification",
    "vkInternalFreeNotification",
    "vkVoidFunction",
}


def extract_from_file(path: str, apis: set[str]) -> None:
    """Extract vk* names from PFN_vk* typedefs in a header file."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.search(r"\*PFN_(vk[A-Za-z0-9]+)\)", line)
            if m:
                name = m.group(1)
                if name not in PFN_CALLBACKS:
                    apis.add(name)


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract Vulkan API list from official headers.")
    parser.add_argument(
        "--win32",
        action="store_true",
        help="Also scan vulkan_win32.h (Windows-specific entry points).",
    )
    parser.add_argument(
        "--output",
        "-o",
        default=os.path.join(os.path.dirname(__file__), "specs", "vulkan-1_official_api_list.txt"),
        help="Output file path.",
    )
    parser.add_argument(
        "--headers-dir",
        default=os.path.join(
            os.path.dirname(__file__), "..", "external", "vulkan-headers", "include", "vulkan"
        ),
        help="Directory containing vulkan_core.h (and optionally vulkan_win32.h).",
    )
    args = parser.parse_args()

    apis: set[str] = set()
    core_h = os.path.join(args.headers_dir, "vulkan_core.h")
    if not os.path.isfile(core_h):
        raise SystemExit(f"Header not found: {core_h}")

    extract_from_file(core_h, apis)
    if args.win32:
        win32_h = os.path.join(args.headers_dir, "vulkan_win32.h")
        if os.path.isfile(win32_h):
            extract_from_file(win32_h, apis)

    out_dir = os.path.dirname(args.output)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    with open(args.output, "w", encoding="utf-8") as f:
        f.write(
            "# Vulkan API list extracted from official Khronos Vulkan headers.\n"
            "# Source: vulkan_core.h"
        )
        if args.win32:
            f.write(" + vulkan_win32.h")
        f.write("\n# One API name per line (no proxy code). Use to audit vulkan-1 proxy.\n#\n")
        for name in sorted(apis):
            f.write(name + "\n")

    print(f"Wrote {len(apis)} APIs to {args.output}")


if __name__ == "__main__":
    main()
