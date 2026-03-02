#!/usr/bin/env python3
# Find unused functions in Display Commander addon using cppcheck and compile_commands.json.
#
# Prereqs:
#   1. Configure CMake with compile commands, e.g.:
#        cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
#   2. Install cppcheck (e.g. choco install cppcheck, or https://cppcheck.sourceforge.io/)
#
# Usage:
#   python tools/find_unused_api.py [build_dir]
#   python tools/find_unused_api.py [build_dir] --addon-only
#   build_dir defaults to "build" (relative to repo root).
#   --addon-only: print only unusedFunction lines in addon sources (exclude external headers).

from __future__ import annotations

import json
import os
import subprocess
import sys


def repo_root() -> str:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.dirname(script_dir)


def main() -> int:
    argv = [a for a in sys.argv[1:] if a != "--addon-only"]
    addon_only = "--addon-only" in sys.argv
    root = repo_root()
    build_dir = argv[0] if argv else "build"
    compile_commands_path = os.path.join(root, build_dir, "compile_commands.json")

    if not os.path.isfile(compile_commands_path):
        print(
            f"compile_commands.json not found at {compile_commands_path}",
            file=sys.stderr,
        )
        print(
            "Configure CMake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON, e.g.:",
            file=sys.stderr,
        )
        print(f"  cmake -B {build_dir} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON", file=sys.stderr)
        return 1

    with open(compile_commands_path, encoding="utf-8", errors="replace") as f:
        commands = json.load(f)

    # Restrict to Display Commander addon sources (exclude cli_ui_exe, minhook, etc.)
    addon_subdir = "addons/display_commander"
    exclude = "cli_ui_exe"
    filtered = [
        entry
        for entry in commands
        if addon_subdir in entry.get("file", "").replace("\\", "/")
        and exclude not in entry.get("file", "").replace("\\", "/")
    ]

    if not filtered:
        print("No addon source entries found in compile_commands.json.", file=sys.stderr)
        return 1

    filtered_path = os.path.join(root, build_dir, "compile_commands_addon.json")
    with open(filtered_path, "w", encoding="utf-8") as f:
        json.dump(filtered, f, indent=2)

    cppcheck_exe = "cppcheck"
    if sys.platform == "win32":
        for candidate in (
            os.path.join(os.environ.get("ProgramFiles", "C:\\Program Files"), "Cppcheck", "cppcheck.exe"),
            os.path.join(os.environ.get("ProgramFiles(x86)", "C:\\Program Files (x86)"), "Cppcheck", "cppcheck.exe"),
        ):
            if os.path.isfile(candidate):
                cppcheck_exe = candidate
                break

    cppcheck_cmd = [
        cppcheck_exe,
        "--enable=unusedFunction",
        "--quiet",
        "--project=" + os.path.abspath(filtered_path),
    ]

    try:
        if addon_only:
            proc = subprocess.run(
                cppcheck_cmd,
                cwd=root,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
            for line in (proc.stdout + proc.stderr).splitlines():
                line_n = line.replace("\\", "/")
                if "unusedFunction" in line and "addons/display_commander" in line_n and "external" not in line_n:
                    print(line)
            return proc.returncode
        result = subprocess.run(cppcheck_cmd, cwd=root)
        return result.returncode
    except FileNotFoundError:
        print("cppcheck not found. Install it (e.g. winget install Cppcheck.Cppcheck).", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
