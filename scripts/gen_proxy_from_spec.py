#!/usr/bin/env python3
"""
Generic proxy generator from a Wine .spec file.
Takes DLL name and path to .spec file. Outputs xxx_proxy.cpp and xxx_exports.def
(fragments to paste into the real proxy_dll/ and exports.def manually).

  python scripts/gen_proxy_from_spec.py winmm scripts/specs/winmm.spec
  python scripts/gen_proxy_from_spec.py d3d11 scripts/specs/d3d11.spec
  python scripts/gen_proxy_from_spec.py dinput8 scripts/specs/dinput8.spec
  python scripts/gen_proxy_from_spec.py dbghelp scripts/specs/dbghelp.spec

Wine .spec format:
  # ordinal exports
  N stdcall @(params) Name   -> named export Name at ordinal N
  N stub @                   -> stub at ordinal N (emits WINMM_N for winmm)
  @ stdcall Name(params)     -> forward
  @ stdcall Name(params) AliasOrGdi32  -> forward (alias ignored; gdi32.X = try gdi32)
  @ stdcall -arch=win64 Foo(params) Bar  -> forward export Bar (alias); -arch=win32/win64 optional
  @ stdcall -import Name(params) NtDllName  -> forward (export Name)
  @ stub Name                -> stub
  # comment                  -> skipped

Generated proxy exports (winmm, d3d11, dbghelp, etc.) are pasted into proxy_dll/exports.def.
"""

import argparse
import os
import re


def wine_to_c(wine_type):
    t = wine_type.strip().lower()
    if t == "ptr":
        return "LPVOID"
    if t == "long":
        return "LONG"
    if t == "str":
        return "LPCSTR"
    if t == "wstr":
        return "LPCWSTR"
    if t == "int64":
        return "DWORD64"
    return "LPVOID"


def parse_spec(spec_text, dll_name):
    """
    Yield (name, is_stub, params_list, use_gdi32_fallback, ordinal_or_none).
    Skips # comment lines. Handles ordinal block and @ stdcall / @ stub.
    """
    dll_lower = dll_name.lower().replace(".dll", "")
    seen = set()
    for line in spec_text.splitlines():
        raw = line
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        # Ordinal: "2 stdcall @(ptr long long) PlaySoundA" or "3 stub @"
        m = re.match(r"(\d+)\s+stub\s+@\s*$", line)
        if m:
            num = m.group(1)
            name = "WINMM_%s" % num if dll_lower == "winmm" else "ORDINAL_%s" % num
            if name not in seen:
                seen.add(name)
                yield (name, True, [], False, int(num))
            continue
        m = re.match(r"\d+\s+stdcall\s+@\((.*?)\)\s+(\w+)\s*$", line)
        if m:
            params_str = m.group(1).strip()
            name = m.group(2)
            params = [wine_to_c(p) for p in re.split(r"[\s,]+", params_str) if p] if params_str else []
            if name not in seen:
                seen.add(name)
                yield (name, False, params, False, None)
            continue
        # @ stub Name
        m = re.match(r"@\s+stub\s+(\w+)\s*$", line)
        if m:
            name = m.group(1)
            if name not in seen:
                seen.add(name)
                yield (name, True, [], False, None)
            continue
        # @ stdcall [-arch=win32|-arch=win64] [-import] [-private] Name(params) [Alias]
        m = re.match(
            r"@\s+stdcall\s+"
            r"(?:-arch=win32|-arch=win64\s+)?"
            r"(-import\s+)?"
            r"(-private\s+)?"
            r"(\w+)\s*\((.*?)\)\s*"
            r"(\w+)?\s*$",
            line,
        )
        if m:
            has_import = m.group(1) is not None
            name = m.group(3)
            params_str = m.group(4).strip()
            alias = m.group(5)
            # -import: export the DLL name (e.g. ImageNtHeader), not the ntdll alias
            export_name = name if has_import else (alias if alias and "gdi32." not in alias else name)
            use_gdi32 = False
            params = [wine_to_c(p) for p in re.split(r"[\s,]+", params_str) if p] if params_str else []
            # When alias differs from name (e.g. EnumerateLoadedModulesEx -> 64), emit both so both are in .def
            to_emit = []
            if export_name not in seen:
                to_emit.append(export_name)
            if name != export_name and name not in seen:
                to_emit.append(name)
            for ex in to_emit:
                seen.add(ex)
                yield (ex, False, params, use_gdi32, None)
            continue
        # @ stdcall [-private] Name(params) [AliasOrGdi32] (no -arch/-import)
        m = re.match(r"@\s+stdcall\s+(-private\s+)?(\w+)\s*\((.*?)\)\s*(.*)$", line)
        if m:
            name = m.group(2)
            params_str = m.group(3).strip()
            rest = m.group(4).strip()
            use_gdi32 = "gdi32." in rest
            alias = rest.split()[-1] if rest and re.match(r"^\w+$", rest.split()[-1]) else None
            export_name = alias if alias and not use_gdi32 else name
            params = [wine_to_c(p) for p in re.split(r"[\s,]+", params_str) if p] if params_str else []
            if export_name not in seen:
                seen.add(export_name)
                yield (export_name, False, params, use_gdi32, None)
            continue


def default_return_type(name, dll_name):
    dll_lower = dll_name.lower().replace(".dll", "")
    if dll_lower == "d3d11" and name.startswith("D3D11"):
        return "HRESULT"
    if dll_lower == "dinput8":
        return "HRESULT"
    if dll_lower == "winmm":
        return "UINT"  # MMRESULT
    if dll_lower == "dbghelp":
        if name.startswith("Sym") or name.startswith("Image") or "EnumDirTree" in name:
            return "BOOL"
        if "ApiVersion" in name or name == "ExtensionApiVersion":
            return "DWORD"
        if "UnDecorate" in name or "SymUnDName" in name:
            return "DWORD"
        return "LONG"
    return "LONG"


def gen_proxy_cpp(entries, dll_name):
    dll_lower = dll_name.lower().replace(".dll", "")
    var = dll_lower.replace(".", "_")
    lines = [
        "/*",
        " * %s proxy. Generated by scripts/gen_proxy_from_spec.py - paste into proxy_dll/." % (dll_name + ".dll",),
        " */",
        "",
        "#ifndef WIN32_LEAN_AND_MEAN",
        "#define WIN32_LEAN_AND_MEAN",
        "#endif",
        "#include <Windows.h>",
        "#include <string>",
        "",
    ]
    if dll_lower == "winmm":
        lines.append('#include "winmm_proxy_init.hpp"')
        lines.append("")
    lines.append("static HMODULE g_%s_module = nullptr;" % var)
    # winmm: prefer winmmHooked.dll in same dir, optional init for DllMain
    if dll_lower == "winmm":
        lines += [
            "",
            "static bool LoadRealWinMM() {",
            "    if (g_winmm_module != nullptr) return true;",
            "    HMODULE hSelf = GetModuleHandleW(L\"winmm.dll\");",
            "    if (hSelf) {",
            "        WCHAR self_path[MAX_PATH];",
            "        if (GetModuleFileNameW(hSelf, self_path, MAX_PATH) != 0) {",
            "            std::wstring dir(self_path);",
            "            size_t last = dir.find_last_of(L\"\\\\/\");",
            "            if (last != std::wstring::npos) dir.resize(last + 1);",
            "            std::wstring hooked = dir + L\"winmmHooked.dll\";",
            "            if (GetFileAttributesW(hooked.c_str()) != INVALID_FILE_ATTRIBUTES) {",
            "                g_winmm_module = LoadLibraryW(hooked.c_str());",
            "            }",
            "        }",
            "    }",
            "    if (g_winmm_module == nullptr) {",
            "        WCHAR system_path[MAX_PATH];",
            "        if (GetSystemDirectoryW(system_path, MAX_PATH) != 0) {",
            "            std::wstring path = std::wstring(system_path) + L\"\\\\winmm.dll\";",
            "            g_winmm_module = LoadLibraryW(path.c_str());",
            "        }",
            "    }",
            "    return g_winmm_module != nullptr;",
            "}",
            "",
            "void LoadRealWinMMFromDllMain() { (void)LoadRealWinMM(); }",
            "",
        ]
        load_fn = "LoadRealWinMM"
    elif dll_lower == "d3d11":
        lines += [
            "static HMODULE g_gdi32_module = nullptr;",
            "",
            "static bool LoadRealD3D11() {",
            "    if (g_d3d11_module != nullptr) return true;",
            "    WCHAR system_path[MAX_PATH];",
            "    if (GetSystemDirectoryW(system_path, MAX_PATH) == 0) return false;",
            "    std::wstring path = std::wstring(system_path) + L\"\\\\d3d11.dll\";",
            "    g_d3d11_module = LoadLibraryW(path.c_str());",
            "    return g_d3d11_module != nullptr;",
            "}",
            "",
            "static HMODULE LoadRealGdi32() {",
            "    if (g_gdi32_module != nullptr) return g_gdi32_module;",
            "    WCHAR system_path[MAX_PATH];",
            "    if (GetSystemDirectoryW(system_path, MAX_PATH) == 0) return nullptr;",
            "    std::wstring path = std::wstring(system_path) + L\"\\\\gdi32.dll\";",
            "    g_gdi32_module = LoadLibraryW(path.c_str());",
            "    return g_gdi32_module;",
            "}",
            "",
            "static FARPROC GetD3D11OrGdi32Proc(const char* name) {",
            "    if (LoadRealD3D11()) { FARPROC fn = GetProcAddress(g_d3d11_module, name); if (fn) return fn; }",
            "    if (LoadRealGdi32()) return GetProcAddress(g_gdi32_module, name);",
            "    return nullptr;",
            "}",
            "",
        ]
        load_fn = "LoadRealD3D11"
    else:
        load_fn = "LoadReal" + "".join(x.capitalize() for x in dll_lower.split("_"))
        lines += [
            "",
            "static bool %s() {" % load_fn,
            "    if (g_%s_module != nullptr) return true;" % var,
            "    WCHAR system_path[MAX_PATH];",
            "    if (GetSystemDirectoryW(system_path, MAX_PATH) == 0) return false;",
            "    std::wstring path = std::wstring(system_path) + L\"\\\\%s.dll\";" % dll_lower,
            "    g_%s_module = LoadLibraryW(path.c_str());" % var,
            "    return g_%s_module != nullptr;" % var,
            "}",
            "",
        ]
    for name, is_stub, params, use_gdi32, ordinal in entries:
        ret = default_return_type(name, dll_name)
        n = len(params)
        param_names = ["p%u" % i for i in range(n)]
        param_decl = ", ".join("%s %s" % (t, n) for t, n in zip(params, param_names)) if params else "void"
        param_list = ", ".join(params) if params else "void"
        if is_stub:
            fail = "return E_FAIL;" if ret == "HRESULT" else "return 0;"
            lines.append("extern \"C\" %s WINAPI %s(%s) {" % (ret, name, param_decl))
            if param_names:
                lines.append("    (void)%s;" % param_names[0])
            lines.append("    %s" % fail)
            lines.append("}")
        else:
            if dll_lower == "d3d11" and use_gdi32:
                call_get = "GetD3D11OrGdi32Proc(\"%s\")" % name
            else:
                call_get = "%s() ? GetProcAddress(g_%s_module, \"%s\") : nullptr" % (load_fn, var, name)
            lines.append("extern \"C\" %s WINAPI %s(%s) {" % (ret, name, param_decl))
            if dll_lower == "d3d11" and use_gdi32:
                lines.append("    if (!LoadRealD3D11() && !LoadRealGdi32()) return 0;")
            else:
                lines.append("    if (!%s()) return %s;" % (load_fn, "E_FAIL" if ret == "HRESULT" else "0"))
            lines.append("    typedef %s (WINAPI *PFN)(%s);" % (ret, param_list))
            lines.append("    PFN fn = (PFN)(%s);" % call_get)
            lines.append("    if (!fn) return %s;" % ("E_FAIL" if ret == "HRESULT" else "0"))
            lines.append("    return fn(%s);" % ", ".join(param_names))
            lines.append("}")
        lines.append("")
    return "\n".join(lines)


def gen_exports_def_fragment(export_names, dll_name, ordinal_comment=None):
    """Generate fragment to paste into main exports.def."""
    dll_lower = dll_name.lower().replace(".dll", "")
    lines = [
        "; %s.dll exports - paste this block into proxy_dll/exports.def" % dll_lower,
        "",
    ]
    if ordinal_comment:
        lines.append("; %s" % ordinal_comment)
        lines.append("")
    for name in export_names:
        lines.append("\t%s PRIVATE" % name)
    lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Generate proxy .cpp and exports.def fragment from Wine .spec")
    parser.add_argument("dll", help="DLL name (e.g. winmm, d3d11)")
    parser.add_argument("spec", help="Path to .spec file")
    parser.add_argument("-o", "--out-dir", default=None, help="Output directory (default: scripts/output)")
    args = parser.parse_args()
    dll_name = args.dll.replace(".dll", "").strip()
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    spec_path = os.path.normpath(os.path.join(repo_root, args.spec) if not os.path.isabs(args.spec) else args.spec)
    if not os.path.isfile(spec_path):
        spec_path = args.spec
    if not os.path.isfile(spec_path):
        parser.error("Spec file not found: %s" % args.spec)
    with open(spec_path, "r", encoding="utf-8") as f:
        spec_text = f.read()
    entries = list(parse_spec(spec_text, dll_name))
    names = [e[0] for e in entries]
    base = dll_name.lower().replace(".dll", "")
    ordinal_comment = None
    if base == "winmm" and any(e[4] for e in entries):
        ordinal_comment = "Ordinals (add at top of winmm section): WINMM_2=PlaySoundA @2, WINMM_3 @3, WINMM_4 @4"
    out_dir = args.out_dir or os.path.join(repo_root, "scripts", "output")
    os.makedirs(out_dir, exist_ok=True)
    out_cpp = os.path.join(out_dir, "%s_proxy.cpp" % base)
    out_def = os.path.join(out_dir, "%s_exports.def" % base)
    cpp = gen_proxy_cpp(entries, dll_name)
    def_frag = gen_exports_def_fragment(names, dll_name, ordinal_comment)
    with open(out_cpp, "w", encoding="utf-8") as f:
        f.write(cpp)
    with open(out_def, "w", encoding="utf-8") as f:
        f.write(def_frag)
    print("Wrote %s (%d exports)" % (out_cpp, len(names)))
    print("Wrote %s (paste into proxy_dll/exports.def)" % out_def)


if __name__ == "__main__":
    main()
