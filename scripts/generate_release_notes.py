#!/usr/bin/env python3
"""从 versions.json / codec_registry.json 生成发行包支持矩阵：
1. docs/COMPATIBILITY.md   —— 面向用户的每个发行包支持说明
2. dist/RELEASE_NOTES.md   —— GitHub Release 的发布说明
数据只登记在这两份 JSON 里，发新版本前重跑本脚本即可同步矩阵。
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def load(name: str) -> dict:
    return json.loads((ROOT / name).read_text(encoding="utf-8"))


def main() -> int:
    versions = load("versions.json")["versions"]
    registry = load("codec_registry.json")
    codecs = registry["codecs"]
    ready = {c["protocol"] for c in codecs if c["status"] == "ready"}
    total_registered = len(codecs)

    def mc_of(protocol: int) -> str:
        return next((c["mc"] for c in codecs if c["protocol"] == protocol), str(protocol))

    # ---------------------------------------------------------- 兼容矩阵页
    lines = [
        "# 版本支持矩阵 — 每个发行包支持什么",
        "",
        "每个发行包按服务器版本单独编译。装好后，插件对每个来连接的客户端只会做三种处理：",
        "",
        "- **✅ 直连放行**：客户端版本与服务器相同，数据原样直通，完整支持。",
        "- **🔶 识别但不放行**：路由表认识这个客户端版本，但跨版本翻译规则还没补齐，"
        "默认拒绝并在日志里写明原因。以后补齐数据时**不需要换插件**，更新配置即可自动生效。",
        "- **❌ 表外版本**：路由表不认识的客户端版本，直接拒绝。",
        "",
        "## 每个发行包的支持情况",
        "",
        "| 下载包 | 服务器版本 | ✅ 直连的客户端 | 自带编解码模块 | 🔶 识别但不放行 | 跨版本翻译 |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for v in versions:
        p = v["serverProtocol"]
        own_codec = "✅" if p in ready else "✘"
        others = total_registered - 1
        lines.append(
            f"| `pkg-{v['id']}` | {v['id']} | {mc_of(p)}（协议 {p}） | {own_codec} | "
            f"其余 {others} 个登记版本 | 暂无 |"
        )
    lines += [
        "",
        f"> 「自带编解码模块」= 该协议的编解码器已在开源 "
        f"[Protocol](https://github.com/SculkCatalystMC/Protocol) 的分支里实现，"
        f"目前是 {sorted(ready)} 共 {len(ready)} 个。",
        "",
        "## 路由表登记的全部客户端版本",
        "",
        f"所有发行包的路由表都认识以下 {total_registered} 个客户端协议"
        "（覆盖 1.20.7x ~ 1.26.60）：",
        "",
        "| 协议号 | 客户端版本 | 编解码模块 |",
        "| --- | --- | --- |",
    ]
    for c in codecs:
        state = "✅ 已实现" if c["status"] == "ready" else "🔶 待补定义"
        lines.append(f"| {c['protocol']} | {c['mc']} | {state} |")
    lines += [
        "",
        "## 为什么「跨版本翻译」目前全是暂无",
        "",
        "跨版本进服 = 把 A 版本的包翻成 B 版本的包，需要三条数据同时到位：",
        "",
        "1. 客户端版本的协议定义（包结构）",
        "2. 服务器版本的协议定义",
        "3. 两个版本之间的字段级翻译规则（包 ID 重映射、字段增删、物品/方块注册表等）",
        "",
        "当前状态：路由、配置、编解码框架全部就绪；开源库直接提供 944 / 975 / 1001 三个版本"
        "的定义，其余 23 个要逐版本补；翻译规则表为空（0 对），所以跨版本客户端暂时会被"
        "明确拒绝而不是静默放行——这是刻意设计，防止玩家带着损坏的数据进服。",
        "",
        "推进计划见 [DEVELOPMENT.md](DEVELOPMENT.md)。",
        "",
    ]
    (ROOT / "docs" / "COMPATIBILITY.md").write_text("\n".join(lines), encoding="utf-8")

    # ---------------------------------------------------------- Release 说明
    notes = [
        "# CrossVersion v0.1.0",
        "",
        "LeviLamina 基岩版跨版本兼容插件，首批 20 个服务器版本的发行包。",
        "",
        "## 怎么选包",
        "",
        "按你的 **BDS 服务器版本**下载对应 zip，解压得到 `CrossVersion` 文件夹，"
        "放进服务器的 `plugins` 目录，重启即可。不同服务器版本的包**不能混用**。",
        "",
        "## 每个包支持什么",
        "",
        "| 下载包 | 服务器版本 | ✅ 直连客户端 | 自带编解码模块 | 跨版本翻译 |",
        "| --- | --- | --- | --- | --- |",
    ]
    for v in versions:
        p = v["serverProtocol"]
        notes.append(
            f"| `CrossVersion-levilamina-{v['id']}-windows-x86_64.zip` | {v['id']} | "
            f"{mc_of(p)}（协议 {p}） | {'✅' if p in ready else '✘'} | 暂无 |"
        )
    notes += [
        "",
        f"所有包的路由表都认识 {total_registered} 个客户端协议"
        "（1.20.7x ~ 1.26.60，完整对照表见仓库 docs/COMPATIBILITY.md）；"
        "跨版本翻译规则还在逐版本补齐，当前未翻译的连接会被明确拒绝并写明日志，"
        "不会让坏数据进服。",
        "",
        "## 包里有什么（每个包相同）",
        "",
        "```",
        "CrossVersion/",
        "├── CrossVersion.dll   插件本体（对应你的服务器版本）",
        "├── manifest.json      LeviLamina 加载清单",
        "├── config.jsonc       配置（白名单/模式/语言，带中文注释）",
        "├── lang/              中英文日志文案",
        "├── README.txt         本版本说明",
        "├── STATUS.txt         构建信息",
        "├── TargetConfig.h     本版本编译期注入的常量",
        "└── codecs/            协议编解码模块（随构建逐步补齐）",
        "```",
        "",
        "## 已知边界（说实话）",
        "",
        "1. 跨版本翻译规则尚未登记，所以除同版本直连外，其它客户端会被明确拒绝",
        "2. 本批 DLL 不接管 BDS 网络符号（hook 表待逐版本逆向补齐），仅做识别、分流与配置",
        "3. 激活码？没有。联网校验？没有。全程开源免费",
        "",
        "完整使用说明见仓库 README；技术细节与推进计划见 docs/。",
        "",
    ]
    (ROOT / "dist" / "RELEASE_NOTES.md").write_text("\n".join(notes), encoding="utf-8")

    print(f"[ok] docs/COMPATIBILITY.md（{len(versions)} 个包 x {total_registered} 个协议）")
    print(f"[ok] dist/RELEASE_NOTES.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
