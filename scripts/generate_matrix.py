#!/usr/bin/env python3
"""从 versions.json / codec_registry.json 生成：
1. GitHub Actions 构建矩阵（--emit-matrix）
2. 每个版本的发布描述与构建配置（dist/<id>/）
3. 编译期注入头文件（src/generated/TargetConfig.h、ProtocolTable.h）
本地无需 Windows 工具链即可运行；DLL 由 CI 产出。
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
GENERATED = ROOT / "src" / "generated"


def load(name: str) -> dict:
    return json.loads((ROOT / name).read_text(encoding="utf-8"))


def validate(versions: dict, registry: dict) -> list[str]:
    problems: list[str] = []
    protocols = [c["protocol"] for c in registry["codecs"]]
    if len(protocols) != len(set(protocols)):
        problems.append("codec_registry.json 中存在重复协议号")
    known = set(protocols)
    seen = set()
    for v in versions["versions"]:
        if v["id"] in seen:
            problems.append(f"versions.json 中存在重复版本 id: {v['id']}")
        seen.add(v["id"])
        if v["serverProtocol"] not in known:
            problems.append(f"{v['id']} 的服务端协议 {v['serverProtocol']} 未登记在 codec_registry.json")
    return problems


def selected_versions(versions: dict, only: str | None) -> list[dict]:
    items = versions["versions"]
    if only:
        wanted = {x.strip() for x in only.split(",") if x.strip()}
        items = [v for v in items if v["id"] in wanted]
        missing = wanted - {v["id"] for v in items}
        if missing:
            print(f"[warn] 未找到版本: {', '.join(sorted(missing))}", file=sys.stderr)
    return items


def matrix_entries(items: list[dict]) -> list[dict]:
    entries = []
    for v in items:
        entries.append(
            {
                "id": v["id"],
                "bds": v["bds"],
                "ll_version": v["levilamina"],
                "server_protocol": v["serverProtocol"],
                "artifact": f"CrossVersion-levilamina-{v['id']}-windows-x86_64.zip",
            }
        )
    return entries


def protocol_table_hpp(registry: dict) -> str:
    all_p = [c["protocol"] for c in registry["codecs"]]
    ready_p = [c["protocol"] for c in registry["codecs"] if c["status"] == "ready"]
    lines = [
        "// 自动生成，请勿手改。来源: codec_registry.json",
        "#pragma once",
        "#include <array>",
        "#include <cstdint>",
        "",
        "namespace cv {",
        f"inline constexpr std::array<std::uint32_t, {len(all_p)}> kAllProtocols = {{",
        "    " + ", ".join(f"{p}u" for p in all_p),
        "};",
        f"inline constexpr std::array<std::uint32_t, {len(ready_p)}> kReadyProtocols = {{",
        "    " + ", ".join(f"{p}u" for p in ready_p) if ready_p else "",
        "};",
        "",
        "inline constexpr bool isKnownProtocol(std::uint32_t p) noexcept {",
        "    for (auto v : kAllProtocols) { if (v == p) return true; }",
        "    return false;",
        "}",
        "",
        "} // namespace cv",
        "",
    ]
    return "\n".join(lines)


def target_config_hpp(v: dict) -> str:
    hooks = v.get("hookSymbols") or {}
    loop = hooks.get("dedicatedServerLoop", "")
    has_loop = "true" if loop else "false"
    lines = [
        "// 自动生成，请勿手改。来源: versions.json -> " + v["id"],
        "#pragma once",
        "",
        "namespace cv {",
        f'inline constexpr char kBdsVersion[] = R"cv({v["bds"]})cv";',
        f"inline constexpr std::uint32_t kServerProtocol = {v['serverProtocol']}u;",
        'inline constexpr char kCodecDir[] = R"cv(codecs)cv";',
        f"inline constexpr bool kHasLoopHook = {has_loop};",
        f'inline constexpr char kLoopHookSymbol[] = R"cv({loop})cv";',
        "inline constexpr bool kHasAnyHook = kHasLoopHook;",
        "} // namespace cv",
        "",
    ]
    return "\n".join(lines)


def write_generated(items: list[dict], registry: dict, target_id: str | None) -> None:
    GENERATED.mkdir(parents=True, exist_ok=True)
    (GENERATED / "ProtocolTable.h").write_text(protocol_table_hpp(registry), encoding="utf-8")
    chosen = next((v for v in items if v["id"] == target_id), items[-1])
    (GENERATED / "TargetConfig.h").write_text(target_config_hpp(chosen), encoding="utf-8")
    print(f"[ok] 生成编译期头文件 -> {GENERATED} (target={chosen['id']})", file=sys.stderr)


def version_readme(v: dict, registry: dict) -> str:
    server_p = v["serverProtocol"]
    ready = [c["protocol"] for c in registry["codecs"] if c["status"] == "ready"]
    planned = [c["protocol"] for c in registry["codecs"] if c["status"] != "ready"]
    hooks = v.get("hookSymbols") or {}
    return "\n".join(
        [
            f"CrossVersion 单插件构建 - BDS {v['bds']}",
            "=" * 40,
            "",
            f"目标 LeviLamina : {v['levilamina']}（若 xmake-repo 包名不同，用 --ll_version 覆盖）",
            f"服务端协议号   : {server_p}",
            f"产物           : CrossVersion-levilamina-{v['id']}-windows-x86_64.zip",
            "",
            "路由表",
            f"  登记的全部客户端协议 : {len(registry['codecs'])} 个",
            f"  已实现 codec 的协议   : {', '.join(map(str, ready))}",
            f"  待补 codec 的协议     : {len(planned)} 个（当前按 passthrough / strict 处理）",
            "",
            "本版本状态",
            f"  网络 hook 符号 : {'已填写' if hooks else '待补齐（网络接管关闭，插件仍可加载与配置）'}",
            f"  codec 可编译   : {'是（' + str(server_p) + ' 在 ready 列表）' if server_p in ready else '否，需补协议定义'}",
            "",
            "安装",
            "  解压后把 CrossVersion/ 放入 BDS 的 plugins/ 目录，",
            "  codec_v*.dll 放在 plugins/CrossVersion/codecs/ 目录。",
            "",
            "说明",
            "  本构建与其他 BDS 版本的构建互不通用：hook 的是 BDS 内部符号，逐版本变化。",
            "  配置见 config.jsonc；修改后重启服务器生效（Plugin::reload() 接口已预留）。",
            "",
        ]
    )


def write_dist(items: list[dict], registry: dict) -> None:
    DIST.mkdir(parents=True, exist_ok=True)
    for v in items:
        d = DIST / v["id"]
        d.mkdir(parents=True, exist_ok=True)
        (d / "TargetConfig.h").write_text(target_config_hpp(v), encoding="utf-8")
        (d / "README.txt").write_text(version_readme(v, registry), encoding="utf-8")
        cfg = {
            "id": v["id"],
            "bds": v["bds"],
            "levilamina": v["levilamina"],
            "serverProtocol": v["serverProtocol"],
            "clientProtocols": v["clientProtocols"],
            "artifact": f"CrossVersion-levilamina-{v['id']}-windows-x86_64.zip",
            "xmake": [
                "xmake f -y -p windows -a x64 -m release",
                f"  --ll_version={v['levilamina']}",
                f"  --cv_server_protocol={v['serverProtocol']}",
                f"  --cv_bds_version={v['id']}",
                "xmake -y",
            ],
            "hookSymbols": v.get("hookSymbols") or {},
        }
        (d / "build-config.json").write_text(
            json.dumps(cfg, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        manifest = {
            "name": "CrossVersion",
            "entry": "CrossVersion.dll",
            "version": f"0.1.0+{v['id']}",
            "type": "native",
        }
        (d / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=4) + "\n", encoding="utf-8"
        )
    (DIST / "matrix.json").write_text(
        json.dumps({"include": matrix_entries(items)}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    ready = [c["protocol"] for c in registry["codecs"] if c["status"] == "ready"]
    lines = [
        "# 构建覆盖度汇总",
        "",
        f"- 目标服务器版本数: {len(items)}（BDS {items[0]['id']} ~ {items[-1]['id']}）",
        f"- 路由表登记协议数: {len(registry['codecs'])}",
        f"- 已实现 codec 协议: {', '.join(map(str, ready))}",
        "",
        "| 版本 | LeviLamina | 服务端协议 | hook 符号 | codec |",
        "| --- | --- | --- | --- | --- |",
    ]
    for v in items:
        has_hook = "已填" if v.get("hookSymbols") else "待补"
        has_codec = "ready" if v["serverProtocol"] in ready else "planned"
        lines.append(f"| {v['id']} | {v['levilamina']} | {v['serverProtocol']} | {has_hook} | {has_codec} |")
    lines.append("")
    lines.append("结论：全部版本均可编译出单插件 DLL，路由表覆盖所有登记协议；实际跨版本翻译范围 = 已实现 codec 的协议集合。")
    lines.append("")
    (DIST / "COVERAGE.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"[ok] 生成 {len(items)} 个版本的构建描述 -> {DIST}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--emit-matrix", action="store_true", help="只输出单行 matrix JSON 到 stdout")
    ap.add_argument("--target", help="生成指定版本 id 的编译期头文件到 src/generated/")
    ap.add_argument("--only", help="只处理逗号分隔的版本 id")
    args = ap.parse_args()

    versions = load("versions.json")
    registry = load("codec_registry.json")
    problems = validate(versions, registry)
    for p in problems:
        print(f"[warn] {p}", file=sys.stderr)

    items = selected_versions(versions, args.only)
    if not items:
        print("[error] 没有可处理的版本", file=sys.stderr)
        return 1

    if args.emit_matrix:
        print(json.dumps({"include": matrix_entries(items)}, separators=(",", ":")))
        write_generated(items, registry, None)
        return 0

    write_dist(items, registry)
    write_generated(items, registry, args.target or items[-1]["id"])

    ready = [c["protocol"] for c in registry["codecs"] if c["status"] == "ready"]
    print(
        f"[done] 版本 {len(items)} 个 | 协议 {len(registry['codecs'])} 个 | 已实现 codec {ready}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
