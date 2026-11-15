#!/usr/bin/env python3
"""从 versions.json / codec_registry.json / translation_rules.json 生成发行包支持矩阵：
1. docs/COMPATIBILITY.md   —— 面向用户的每个发行包支持说明
2. dist/RELEASE_NOTES.md   —— GitHub Release 的发布说明
数据只登记在这几份 JSON 里，发新版本前重跑本脚本即可同步矩阵。
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
    derived = {c["protocol"] for c in codecs if c.get("fidelity") == "derived"}
    total_registered = len(codecs)

    rules = load("translation_rules.json")["pairs"]
    ready_pairs = {(p["from"], p["to"]) for p in rules if p.get("status") == "ready"}

    # 每个服务器协议可翻译的客户端协议（进服方向：client -> server）
    def translate_in(p: int) -> list[int]:
        return sorted({f for (f, t) in ready_pairs if t == p and f != p})

    # 每个服务器协议可翻译的下行目标（出服方向：server -> client）
    def translate_out(p: int) -> list[int]:
        return sorted({t for (f, t) in ready_pairs if f == p and t != p})

    def trans_text(p: int) -> str:
        ins, outs = translate_in(p), translate_out(p)
        if not ins and not outs:
            return "暂无"
        parts = []
        if ins:
            parts.append("进服: " + ", ".join(f"v{x}" for x in ins))
        if outs:
            parts.append("出服: " + ", ".join(f"v{x}" for x in outs))
        return "；".join(parts)

    def mc_of(protocol: int) -> str:
        return next((c["mc"] for c in codecs if c["protocol"] == protocol), str(protocol))

    # ---------------------------------------------------------- 兼容矩阵页
    lines = [
        "# 版本支持矩阵 — 每个发行包支持什么",
        "",
        "每个发行包按服务器版本单独编译。装好后，插件对每个来连接的客户端只会做三种处理：",
        "",
        "- **✅ 直连放行**：客户端版本与服务器相同，数据原样直通，完整支持。",
        "- **🔁 跨版本翻译**：客户端版本与服务器不同，但翻译规则已就绪，插件翻译数据后放行。",
        "- **🔶 识别但不放行**：路由表认识这个客户端版本，但跨版本翻译规则还没补齐，"
        "默认拒绝并在日志里写明原因。以后补齐数据时**不需要换插件**，更新配置即可自动生效。",
        "- **❌ 表外版本**：路由表不认识的客户端版本，直接拒绝。",
        "",
        "## 每个发行包的支持情况",
        "",
        "| 下载包 | 服务器版本 | ✅ 直连的客户端 | 🔁 已就绪的翻译对 | 自带编解码模块 | 🔶 识别但不放行 |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for v in versions:
        p = v["serverProtocol"]
        own_codec = "✅" if p in ready else "✘"
        trans = trans_text(p)
        others = total_registered - 1
        lines.append(
            f"| `pkg-{v['id']}` | {v['id']} | {mc_of(p)}（协议 {p}） | {trans} | "
            f"{own_codec} | 其余 {others} 个登记版本 |"
        )
    lines += [
        "",
        f"> 「自带编解码模块」= 该协议的编解码器已就绪，目前是 {sorted(ready)} 共 {len(ready)} 个。",
        "  其中 " + (f"{sorted(derived)} 来自开源 Protocol 分支的派生编译" if derived else "全部来自开源 Protocol 分支")
        + "（包结构以基准分支为准，专属字段差异待实测校准），"
        + f"其余 {len(ready) - len(derived)} 个来自 [SculkCatalystMC/Protocol](https://github.com/SculkCatalystMC/Protocol) 的原生分支。",
        "",
        "## 当前就绪的翻译规则",
        "",
    ]
    if ready_pairs:
        lines.append("| 源协议 | 目标协议 | 策略 | 说明 |")
        lines.append("| --- | --- | --- | --- |")
        for p in rules:
            if p.get("status") != "ready":
                continue
            lines.append(
                f"| v{p['from']} | v{p['to']} | {p.get('strategy', 'header')} | {p.get('note', '')} |"
            )
    else:
        lines.append("暂无。")
    lines += [
        "",
        "## 路由表登记的全部客户端版本",
        "",
        f"所有发行包的路由表都认识以下 {total_registered} 个客户端协议"
        "（覆盖 1.20.7x ~ 1.26.60）：",
        "",
        "| 协议号 | 客户端版本 | 编解码模块 | 来源 |",
        "| --- | --- | --- | --- |",
    ]
    for c in codecs:
        state = "✅ 已实现" if c["status"] == "ready" else "🔶 待补定义"
        src = "派生（v944 基准）" if c.get("fidelity") == "derived" else ("上游分支" if c.get("status") == "ready" else "—")
        lines.append(f"| {c['protocol']} | {c['mc']} | {state} | {src} |")
    lines += [
        "",
        "## 为什么大多数「跨版本翻译」还没就绪",
        "",
        "跨版本进服 = 把 A 版本的包翻成 B 版本的包，需要三条数据同时到位：",
        "",
        "1. 客户端版本的协议定义（包结构）",
        "2. 服务器版本的协议定义",
        "3. 两个版本之间的翻译规则（包 ID 重映射、字段增删、物品/方块注册表等）",
        "",
        "当前状态：路由、配置、编解码框架、翻译规则引擎全部就绪；"
        "898 <-> 944 已登记为首对就绪规则（v898 codec 由 v944 分支派生，包结构同源）；"
        "其余翻译对在 translation_rules.json 里按 planned 登记，未就绪的连接会被"
        "明确拒绝而不是静默放行——这是刻意设计，防止玩家带着损坏的数据进服。",
        "",
        "推进计划见 [DEVELOPMENT.md](DEVELOPMENT.md)。",
        "",
    ]
    (ROOT / "docs" / "COMPATIBILITY.md").write_text("\n".join(lines), encoding="utf-8")

    # ---------------------------------------------------------- Release 说明
    notes = [
        "# CrossVersion v0.2.0",
        "",
        "LeviLamina 基岩版跨版本兼容插件。本版新增：1.21.132 发行包、翻译规则引擎、"
        "首批跨版本翻译规则（v898 ⇄ v944）与 898/944/975/1001 四个协议编解码模块。",
        "",
        "## 怎么选包",
        "",
        "按你的 **BDS 服务器版本**下载对应 zip，解压得到 `CrossVersion` 文件夹，"
        "放进服务器的 `plugins` 目录，重启即可。不同服务器版本的包**不能混用**。",
        "",
        "## 每个包支持什么",
        "",
        "| 下载包 | 服务器版本 | ✅ 直连客户端 | 🔁 翻译就绪 | 自带编解码模块 |",
        "| --- | --- | --- | --- | --- |",
    ]
    for v in versions:
        p = v["serverProtocol"]
        notes.append(
            f"| `CrossVersion-levilamina-{v['id']}-windows-x86_64.zip` | {v['id']} | "
            f"{mc_of(p)}（协议 {p}） | {trans_text(p)} | {'✅' if p in ready else '✘'} |"
        )
    notes += [
        "",
        f"所有包的路由表都认识 {total_registered} 个客户端协议"
        "（1.20.7x ~ 1.26.60，完整对照表见仓库 docs/COMPATIBILITY.md）；"
        "未就绪的跨版本连接会被明确拒绝并写明日志，不会让坏数据进服。",
        "",
        "## 包里有什么（每个包相同）",
        "",
        "```",
        "CrossVersion/",
        "├── CrossVersion.dll        插件本体（对应你的服务器版本）",
        "├── manifest.json           LeviLamina 加载清单",
        "├── config.jsonc            配置（白名单/模式/语言，带中文注释）",
        "├── translation_rules.json  跨版本翻译规则（可自行增删翻译对）",
        "├── lang/                   中英文日志文案",
        "├── README.txt              本版本说明",
        "├── STATUS.txt              构建信息",
        "├── TargetConfig.h          本版本编译期注入的常量",
        "└── codecs/                 协议编解码模块 codec_v*.dll",
        "```",
        "",
        "## 已知边界（说实话）",
        "",
        "1. 已就绪的跨版本翻译目前只有 v898 ⇄ v944 一对（v898 由 v944 派生，包结构同源）；",
        "   其它版本对仍会拒绝，等差异分析逐对补齐",
        "2. 派生 codec 的包结构以基准分支为准，1.21.13x 专属字段差异待实测校准",
        "3. 本批 DLL 不接管 BDS 网络符号（hook 表待逐版本逆向补齐），仅做识别、分流与配置",
        "4. 激活码？没有。联网校验？没有。全程开源免费",
        "",
        "完整使用说明见仓库 README；技术细节与推进计划见 docs/。",
        "",
    ]
    (ROOT / "dist" / "RELEASE_NOTES.md").write_text("\n".join(notes), encoding="utf-8")

    print(f"[ok] docs/COMPATIBILITY.md（{len(versions)} 个包 x {total_registered} 个协议，{len(ready_pairs)} 条就绪规则）")
    print(f"[ok] dist/RELEASE_NOTES.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
