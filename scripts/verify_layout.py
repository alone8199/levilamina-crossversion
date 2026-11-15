#!/usr/bin/env python3
"""工程静态自检：不需要 Windows / xmake / MSVC，验证配置、生成物与源码引用是否自洽。"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
DIST = ROOT / "dist"

REQUIRED = [
    "README.md", "versions.json", "codec_registry.json", "translation_rules.json",
    "manifest.json", "tooth.json",
    "xmake.lua", "CMakeLists.txt", "codec/CMakeLists.txt", "codec/CodecExports.cpp",
    "src/CodecApi.h", "src/CrossVersion.h", "src/CrossVersion.cpp",
    "src/NetworkHooks.cpp", "src/DllMain.cpp", "src/generated/TargetConfig.h",
    "src/generated/ProtocolTable.h", "tests/router_test.cpp",
    "compat/ll/api/Logger.h", "third_party/nlohmann/json.hpp",
    "config/config.jsonc", "lang/zh_CN.json", "lang/en.json",
    "scripts/generate_matrix.py", "scripts/build_codecs.ps1",
    ".gitignore", ".github/workflows/build.yml", "docs/COVERAGE.md",
]

LOCAL_INCLUDES = ["CodecApi.h", "CrossVersion.h", "generated/TargetConfig.h", "generated/ProtocolTable.h"]


def check(name: str, ok: bool, detail: str = "") -> bool:
    print(f"  [{'OK' if ok else 'FAIL'}] {name}" + (f" — {detail}" if detail else ""))
    return ok


def main() -> int:
    results: list[bool] = []
    print("== 文件完整性 ==")
    for rel in REQUIRED:
        results.append(check(rel, (ROOT / rel).exists()))

    print("== JSON 配置 ==")
    versions = json.loads((ROOT / "versions.json").read_text(encoding="utf-8"))
    registry = json.loads((ROOT / "codec_registry.json").read_text(encoding="utf-8"))
    protocols = [c["protocol"] for c in registry["codecs"]]
    results.append(check("codec_registry 协议号唯一", len(protocols) == len(set(protocols)), f"{len(protocols)} 个"))

    vlist = versions["versions"]
    ids = [v["id"] for v in vlist]
    results.append(check("versions 版本 id 唯一", len(ids) == len(set(ids)), f"{len(vlist)} 个版本"))

    unknown = [v["id"] for v in vlist if v["serverProtocol"] not in protocols]
    results.append(check("每个服务端协议都已登记", not unknown, ",".join(unknown) if unknown else ""))

    ready = [c["protocol"] for c in registry["codecs"] if c["status"] == "ready"]
    results.append(check("ready codec 都有分支", all(
        c.get("branch") and c.get("abi") for c in registry["codecs"] if c["status"] == "ready"
    ), f"ready = {ready}"))

    print("== 翻译规则 ==")
    rules = json.loads((ROOT / "translation_rules.json").read_text(encoding="utf-8"))
    pairs = rules.get("pairs", [])
    results.append(check("翻译规则非空", len(pairs) > 0, f"{len(pairs)} 对"))
    bad_ref = [f"{p['from']}->{p['to']}" for p in pairs
               if p.get("from") not in protocols or p.get("to") not in protocols]
    results.append(check("规则对的协议号都已登记", not bad_ref, ",".join(bad_ref) if bad_ref else ""))
    bad_ready = [f"{p['from']}->{p['to']}" for p in pairs
                 if p.get("status") == "ready"
                 and (p.get("from") not in ready or p.get("to") not in ready)]
    results.append(check("ready 翻译对的两侧 codec 都是 ready", not bad_ready,
                         ",".join(bad_ready) if bad_ready else ""))
    dup = [f"{p['from']}->{p['to']}" for p in pairs
           if len([q for q in pairs if q.get("from") == p.get("from") and q.get("to") == p.get("to")]) > 1]
    results.append(check("规则对无重复", len(set(dup)) == len(dup), ",".join(sorted(set(dup))) if dup else ""))

    print("== 生成头 ==")
    tc = (SRC / "generated/TargetConfig.h")
    pt = (SRC / "generated/ProtocolTable.h")
    if tc.exists():
        text = tc.read_text(encoding="utf-8")
        results.append(check("TargetConfig.h 含 BDS/协议/hook 常量", all(
            k in text for k in ["kBdsVersion", "kServerProtocol", "kHasAnyHook", "kLoopHookSymbol"]
        )))
    else:
        results.append(check("TargetConfig.h 存在", False))
    if pt.exists():
        text = pt.read_text(encoding="utf-8")
        results.append(check("ProtocolTable.h 协议数一致",
                             f"<std::uint32_t, {len(protocols)}>" in text,
                             f"{len(protocols)} 个"))
    else:
        results.append(check("ProtocolTable.h 存在", False))

    print("== 源码引用 ==")
    for cpp in sorted(SRC.glob("*.cpp")) + sorted((ROOT / "codec").glob("*.cpp")):
        text = cpp.read_text(encoding="utf-8")
        includes = re.findall(r'#include\s+"([^"]+)"', text)
        missing = [i for i in includes if i in LOCAL_INCLUDES and not (SRC / i).exists()]
        results.append(check(f"{cpp.relative_to(ROOT)} 本地头引用", not missing, ",".join(missing)))

    print("== 构建矩阵 ==")
    matrix_file = DIST / "matrix.json"
    if matrix_file.exists():
        matrix = json.loads(matrix_file.read_text(encoding="utf-8"))["include"]
        results.append(check("matrix 条目数 = 版本数", len(matrix) == len(vlist), f"{len(matrix)}"))
        artifacts = [m["artifact"] for m in matrix]
        results.append(check("产物名唯一", len(artifacts) == len(set(artifacts))))
        results.append(check("每个版本都有 dist 描述", all((DIST / m["id"] / "build-config.json").exists() for m in matrix)))
    else:
        results.append(check("dist/matrix.json 已生成（先跑 generate_matrix.py）", False))

    workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
    results.append(check("CI 含 prepare/verify/build-codecs/build/release", all(
        f"{job}:" in workflow for job in ["prepare", "verify", "build-codecs", "build", "release"]
    )))
    results.append(check("CI 用 CMake 独立构建路径", "cmake --build build" in workflow))
    results.append(check("CI 逐版本注入 TargetConfig.h", "--target ${{ matrix.id }}" in workflow))

    ok = sum(1 for r in results if r)
    total = len(results)
    print(f"\n== 结果: {ok}/{total} 通过 ==")
    print(f"目标: {len(vlist)} 个 BDS 版本 | {len(protocols)} 个协议 | ready codec {ready}")
    print("提示: Windows DLL 由 GitHub Actions 编译；本地可用 CMake + ctest 验证核心逻辑。")
    return 0 if ok == total else 1


if __name__ == "__main__":
    raise SystemExit(main())
