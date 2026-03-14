#!/usr/bin/env python3
"""
Generate bcrypt_proxy.cpp from bcrypt_proxy.hpp by parsing every PFN_ typedef.

  python scripts/gen_bcrypt_proxy_from_hpp.py

Reads src/addons/display_commander/proxy_dll/bcrypt_proxy.hpp and writes
src/addons/display_commander/proxy_dll/bcrypt_proxy.cpp.

Only lines matching:
  typedef <ret> (WINAPI *PFN_<Name>)(<params>);
are used. Export name is derived by stripping the PFN_ prefix (e.g. PFN_BCryptDecrypt -> BCryptDecrypt).
Return types: NTSTATUS -> LONG WINAPI wrapper; void -> void WINAPI; void* -> void* WINAPI (for Get*Interface).

After adding or changing PFN_ typedefs in bcrypt_proxy.hpp, run this script to regenerate the proxy.
"""

import re
import os


def parse_param_names(params_str: str) -> list[str]:
    """Parse 'ULONG dwTable, LPCWSTR pszContext' -> ['dwTable', 'pszContext']. 'void' -> []."""
    s = params_str.strip()
    if not s or s == "void":
        return []
    names = []
    for part in s.split(","):
        part = part.strip()
        if not part or part == "void":
            continue
        # Last token is the parameter name (e.g. "ULONG dwTable" -> "dwTable")
        tokens = part.split()
        if tokens:
            names.append(tokens[-1])
    return names


def parse_typedef_line(line: str) -> tuple[str, str, str, list[str]] | None:
    """
    Match: typedef RET (WINAPI *PFN_XXX)(PARAMS);
    Return (return_type, pfn_type, params_full, param_names) or None.
    """
    # return type: NTSTATUS | void | void*
    # pfn: PFN_BCryptXxx or PFN_GetXxxInterface
    # params: full string for signature; we also parse names for the call
    m = re.match(
        r"typedef\s+(NTSTATUS|void\s*\*|void)\s+\(WINAPI\s+\*(PFN_\w+)\)\s*\((.*)\)\s*;",
        line.strip(),
    )
    if not m:
        return None
    ret, pfn_type, params_str = m.group(1), m.group(2), m.group(3)
    param_names = parse_param_names(params_str)
    return (ret.strip(), pfn_type, params_str.strip(), param_names)


def export_name_from_pfn(pfn_type: str) -> str:
    """PFN_BCryptCloseAlgorithmProvider -> BCryptCloseAlgorithmProvider, PFN_GetHashInterface -> GetHashInterface."""
    if pfn_type.startswith("PFN_"):
        return pfn_type[4:]
    return pfn_type


def collect_typedefs(hpp_path: str) -> list[tuple[str, str, str, list[str]]]:
    """Read bcrypt_proxy.hpp and return list of (return_type, pfn_type, params_str, param_names)."""
    with open(hpp_path, "r", encoding="utf-8") as f:
        content = f.read()
    entries = []
    for line in content.splitlines():
        if "typedef" not in line or "PFN_" not in line:
            continue
        parsed = parse_typedef_line(line)
        if parsed:
            entries.append(parsed)
    return entries


def generate_cpp(entries: list[tuple[str, str, str, list[str]]], out_path: str, hpp_dir: str) -> None:
    """Write bcrypt_proxy.cpp with header and one extern \"C\" wrapper per entry."""
    lines = [
        "/*",
        " * bcrypt.dll proxy. Generated from bcrypt_proxy.hpp by scripts/gen_bcrypt_proxy_from_hpp.py",
        " * Regenerate: python scripts/gen_bcrypt_proxy_from_hpp.py",
        " *",
        " * This file must not include bcrypt.h so our extern \"C\" proxy symbols do not conflict with",
        " * the SDK declarations (C2733 on x86).",
        " */",
        "#ifndef WIN32_LEAN_AND_MEAN",
        "#define WIN32_LEAN_AND_MEAN",
        "#endif",
        "#include <Windows.h>",
        "",
        "// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top",
        '#include "../utils/detour_call_tracker.hpp"',
        '#include "../utils/timing.hpp"',
        '#include "bcrypt_proxy.hpp"',
        "",
        "// Libraries <standard C++>",
        "#include <string>",
        "",
        "#ifndef STATUS_NOT_IMPLEMENTED",
        "#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002L)",
        "#endif",
        "",
        "namespace {",
        "",
        "HMODULE g_bcrypt_module = nullptr;",
        "",
        "bool LoadRealBcrypt() {",
        "    if (g_bcrypt_module != nullptr) return true;",
        "    WCHAR system_path[MAX_PATH];",
        "    if (GetSystemDirectoryW(system_path, MAX_PATH) == 0) return false;",
        "    std::wstring path = std::wstring(system_path) + L\"\\\\bcrypt.dll\";",
        "    g_bcrypt_module = LoadLibraryW(path.c_str());",
        "    return g_bcrypt_module != nullptr;",
        "}",
        "",
        "}  // namespace",
        "",
    ]

    for ret_type, pfn_type, params_str, param_names in entries:
        name = export_name_from_pfn(pfn_type)
        symbol = '"' + name + '"'
        args_call = ", ".join(param_names)

        if ret_type == "NTSTATUS":
            ret_cpp = "LONG"
            fail_ret = "return (LONG)STATUS_NOT_IMPLEMENTED;"
            cast_ret = "(LONG)"
            lines.append(f'extern "C" {ret_cpp} WINAPI {name}({params_str}) {{')
            lines.append("    CALL_GUARD(utils::get_now_ns());")
            lines.append("    if (!LoadRealBcrypt()) " + fail_ret)
            lines.append(f"    auto fn = ({pfn_type})GetProcAddress(g_bcrypt_module, {symbol});")
            lines.append("    if (fn == nullptr) " + fail_ret)
            lines.append(f"    return {cast_ret}fn({args_call});")
        elif ret_type == "void":
            lines.append(f'extern "C" void WINAPI {name}({params_str}) {{')
            lines.append("    CALL_GUARD(utils::get_now_ns());")
            lines.append("    if (!LoadRealBcrypt()) return;")
            lines.append(f"    auto fn = ({pfn_type})GetProcAddress(g_bcrypt_module, {symbol});")
            lines.append("    if (fn == nullptr) return;")
            lines.append(f"    fn({args_call});")
        else:
            # void*
            lines.append(f'extern "C" void* WINAPI {name}({params_str}) {{')
            lines.append("    CALL_GUARD(utils::get_now_ns());")
            lines.append("    if (!LoadRealBcrypt()) return nullptr;")
            lines.append(f"    auto fn = ({pfn_type})GetProcAddress(g_bcrypt_module, {symbol});")
            lines.append("    if (fn == nullptr) return nullptr;")
            if not args_call:
                lines.append("    return fn();")
            else:
                lines.append(f"    return fn({args_call});")
        lines.append("}")
        lines.append("")

    text = "\n".join(lines)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src = os.path.join(repo_root, "src", "addons", "display_commander", "proxy_dll")
    hpp_path = os.path.join(src, "bcrypt_proxy.hpp")
    cpp_path = os.path.join(src, "bcrypt_proxy.cpp")

    if not os.path.isfile(hpp_path):
        raise SystemExit(f"Header not found: {hpp_path}")

    entries = collect_typedefs(hpp_path)
    if not entries:
        raise SystemExit("No PFN_ typedefs found in bcrypt_proxy.hpp")

    generate_cpp(entries, cpp_path, src)
    print(f"Wrote {len(entries)} exports to {cpp_path}")


if __name__ == "__main__":
    main()
