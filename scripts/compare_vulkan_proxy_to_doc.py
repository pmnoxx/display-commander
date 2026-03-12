#!/usr/bin/env python3
"""
Compare vulkan-1 proxy (vulkan-1_proxy.cpp) to official Vulkan API documentation
(vulkan_core.h + vulkan_win32.h). Reports:
  - In doc but not in proxy (missing)
  - In proxy but not in doc (extra / unknown)
  - Parameter count mismatch
  - Return type mismatch (void vs VkResult/int vs pointer)

Output: human-readable report and optional JSON. Does NOT use the spec list file;
reads the headers (doc) and proxy source directly.
"""

import argparse
import os
import re
from dataclasses import dataclass, field


PFN_CALLBACKS = {
    "vkAllocationFunction",
    "vkFreeFunction",
    "vkReallocationFunction",
    "vkInternalAllocationNotification",
    "vkInternalFreeNotification",
    "vkVoidFunction",
}


@dataclass
class DocApi:
    name: str
    return_kind: str  # "void" | "VkResult" | "pointer" (PFN_vkVoidFunction) | "VkBool32" | "uint64" | "size_t"
    param_count: int


@dataclass
class ProxyApi:
    name: str
    return_kind: str  # "void" | "int"
    param_count: int


def parse_doc_header(path: str, apis: dict[str, DocApi]) -> None:
    """Extract API name, return type, param count from PFN_vk* typedefs."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()
    # typedef RET (VKAPI_PTR *PFN_vkNAME)(params);
    pattern = re.compile(
        r"typedef\s+(\S+(?:\s+\S+)?)\s+\(VKAPI_PTR\s+\*PFN_(vk[A-Za-z0-9]+)\)\s*\((.*?)\)\s*;",
        re.DOTALL,
    )
    for m in pattern.finditer(content):
        ret_raw = m.group(1).strip()
        name = m.group(2)
        if name in PFN_CALLBACKS:
            continue
        param_list = m.group(3).strip()
        param_count = 0 if not param_list else param_list.count(",") + 1
        if "void" in ret_raw and "void*" not in ret_raw and "VkBool32" not in ret_raw:
            return_kind = "void"
        elif "VkResult" in ret_raw:
            return_kind = "VkResult"
        elif "PFN_vkVoidFunction" in ret_raw:
            return_kind = "pointer"
        elif "VkBool32" in ret_raw:
            return_kind = "VkBool32"
        elif "uint64_t" in ret_raw or "VkDeviceSize" in ret_raw:
            return_kind = "uint64"
        elif "size_t" in ret_raw:
            return_kind = "size_t"
        else:
            return_kind = ret_raw
        apis[name] = DocApi(name=name, return_kind=return_kind, param_count=param_count)


def parse_proxy_cpp(path: str, apis: dict[str, ProxyApi]) -> None:
    """Extract API name, return (int/void/pointer), param count from proxy .cpp."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()
    # New format (gen_vulkan_proxy_from_abi): RET VKAPI_CALL vkName(PARAMS) {
    pattern_new = re.compile(
        r"(\S+(?:\s+\S+)?)\s+VKAPI_CALL\s+(vk[A-Za-z0-9]+)\s*\((.*?)\)\s*\{",
        re.DOTALL,
    )
    for m in pattern_new.finditer(content):
        ret_raw = m.group(1).strip()
        name = m.group(2)
        param_part = m.group(3).strip()
        if not param_part or param_part == "void":
            param_count = 0
        else:
            param_count = param_part.count(",") + 1
        if "void" in ret_raw and "*" not in ret_raw and "PFN" not in ret_raw:
            return_kind = "void"
        elif "PFN" in ret_raw or "Function" in ret_raw:
            return_kind = "pointer"
        else:
            return_kind = "int"
        apis[name] = ProxyApi(name=name, return_kind=return_kind, param_count=param_count)
    if apis:
        return
    # Legacy format: extern "C" int WINAPI vkName(...) or void WINAPI vkName(...)
    pattern_legacy = re.compile(
        r'extern\s+"C"\s+(int|void)\s+WINAPI\s+(vk[A-Za-z0-9]+)\s*\((.*?)\)\s*\{',
        re.DOTALL,
    )
    for m in pattern_legacy.finditer(content):
        ret = m.group(1)
        name = m.group(2)
        param_part = m.group(3).strip()
        if not param_part:
            param_count = 0
        else:
            param_count = len([p.strip() for p in param_part.split(",")])
        apis[name] = ProxyApi(name=name, return_kind=ret, param_count=param_count)


def doc_return_matches_proxy(doc_ret: str, proxy_ret: str) -> bool:
    """Doc return kind vs proxy (void / int / pointer)."""
    if proxy_ret == "void":
        return doc_ret == "void"
    if proxy_ret == "int":
        return doc_ret in (
            "VkResult",
            "VkBool32",
            "uint64",
            "size_t",
            "VkDeviceAddress",
            "uint32_t",
        )
    if proxy_ret == "pointer":
        return doc_ret == "pointer"
    return False


def main() -> None:
    parser = argparse.ArgumentParser(description="Compare vulkan-1 proxy to official Vulkan doc (headers).")
    parser.add_argument(
        "--headers-dir",
        default=os.path.join(
            os.path.dirname(__file__), "..", "external", "vulkan-headers", "include", "vulkan"
        ),
        help="Directory containing vulkan_core.h",
    )
    parser.add_argument(
        "--proxy-cpp",
        default=os.path.join(
            os.path.dirname(__file__), "..", "src", "addons", "display_commander", "proxy_dll", "vulkan-1_proxy.cpp"
        ),
        help="Path to vulkan-1_proxy.cpp",
    )
    parser.add_argument("--win32", action="store_true", help="Include vulkan_win32.h in doc.")
    parser.add_argument("-o", "--output", help="Write report to file.")
    args = parser.parse_args()

    doc_apis: dict[str, DocApi] = {}
    core_h = os.path.join(args.headers_dir, "vulkan_core.h")
    if not os.path.isfile(core_h):
        raise SystemExit(f"Doc header not found: {core_h}")
    parse_doc_header(core_h, doc_apis)
    if args.win32:
        win32_h = os.path.join(args.headers_dir, "vulkan_win32.h")
        if os.path.isfile(win32_h):
            parse_doc_header(win32_h, doc_apis)

    if not os.path.isfile(args.proxy_cpp):
        raise SystemExit(f"Proxy not found: {args.proxy_cpp}")
    proxy_apis: dict[str, ProxyApi] = {}
    parse_proxy_cpp(args.proxy_cpp, proxy_apis)

    in_doc = set(doc_apis)
    in_proxy = set(proxy_apis)

    missing_in_proxy = sorted(in_doc - in_proxy)
    only_in_proxy = sorted(in_proxy - in_doc)
    common = sorted(in_doc & in_proxy)

    param_mismatches: list[tuple[str, int, int]] = []
    return_mismatches: list[tuple[str, str, str]] = []

    for name in common:
        d = doc_apis[name]
        p = proxy_apis[name]
        if d.param_count != p.param_count:
            param_mismatches.append((name, d.param_count, p.param_count))
        if not doc_return_matches_proxy(d.return_kind, p.return_kind):
            if d.return_kind == "pointer" and p.return_kind == "int":
                return_mismatches.append((name, d.return_kind, f"{p.return_kind} (pointer truncated on 64-bit)"))
            else:
                return_mismatches.append((name, d.return_kind, p.return_kind))

    lines = [
        "# Vulkan-1 proxy vs official documentation (vulkan_core.h" + (" + vulkan_win32.h" if args.win32 else "") + ")",
        "",
        f"Doc APIs: {len(doc_apis)}",
        f"Proxy APIs: {len(proxy_apis)}",
        "",
        "## Missing in proxy (in doc only)",
        f"Count: {len(missing_in_proxy)}",
        "",
    ]
    for name in missing_in_proxy[:100]:
        lines.append(f"  {name}")
    if len(missing_in_proxy) > 100:
        lines.append(f"  ... and {len(missing_in_proxy) - 100} more")

    lines.extend([
        "",
        "## In proxy but not in doc",
        f"Count: {len(only_in_proxy)}",
        "",
    ])
    for name in only_in_proxy[:50]:
        lines.append(f"  {name}")
    if len(only_in_proxy) > 50:
        lines.append(f"  ... and {len(only_in_proxy) - 50} more")

    lines.extend([
        "",
        "## Parameter count mismatch (doc vs proxy)",
        f"Count: {len(param_mismatches)}",
        "",
    ])
    for name, doc_n, proxy_n in param_mismatches:
        lines.append(f"  {name}: doc={doc_n} proxy={proxy_n}")

    lines.extend([
        "",
        "## Return type mismatch",
        f"Count: {len(return_mismatches)}",
        "",
    ])
    for name, doc_ret, proxy_ret in return_mismatches:
        lines.append(f"  {name}: doc={doc_ret} proxy={proxy_ret}")

    report = "\n".join(lines)
    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(report)
        print(f"Wrote report to {args.output}")
    else:
        print(report)
    print(f"\nSummary: missing={len(missing_in_proxy)} extra={len(only_in_proxy)} param_mismatch={len(param_mismatches)} return_mismatch={len(return_mismatches)}")


if __name__ == "__main__":
    main()
