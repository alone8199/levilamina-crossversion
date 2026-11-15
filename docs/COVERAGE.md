# 覆盖度与待补清单

本文件说明「理论兼容所有支持的版本」的准确边界，避免把框架就绪误读为协议翻译已完成。

## 三层覆盖度

| 层 | 状态 | 说明 |
| --- | --- | --- |
| 构建层（21 个 BDS 版本） | 完成 | `versions.json` + CI matrix，每个版本一个 zip；真机构建的 DLL 与 LeviLamina 版本一一对应 |
| 路由层（26 个协议号） | 完成 | `ProtocolTable.h` 编译期注入，所有登记协议都能识别、按配置放行/拒绝/分流 |
| 翻译规则引擎 | 完成 | `translation_rules.json` 驱动：逐对登记、status=ready 才放行、支持 deny/remap 逐包策略、未就绪一律拒绝 |
| 编解码层（codec） | 4 / 26 | 944、975、1001 来自开源分支；898（1.21.13x / 1.21.132）由 v944 派生重编；其余为路由表占位 |
| 翻译规则层 | 1 对（双向） | 898 ⇄ 944 header 策略就绪（v898 由 v944 派生、包结构同源）；其余 10 对在规则表按 planned 登记 |
| 网络接管层（hook 符号） | 1 / 21 | 仅 1.26.60 填了一条示例符号，且签名适配器未接线——hook 接线前，就绪的翻译对也无法实际生效 |

## 逐版本矩阵

| BDS | 协议 | codec | hook | 可加载 | 可接管网络 | 可跨版本翻译 |
| --- | --- | --- | --- | --- | --- | --- |
| 1.21.30 ~ 1.21.120 | 729 ~ 859 | planned | 待补 | 是 | 否 | 否 |
| 1.21.130 / 1.21.132 | 898 | ready（v944 派生） | 待补 | 是 | 否 | 规则就绪（⇄ v944），待 hook 接线 |
| 1.26.0 | 924 | planned | 待补 | 是 | 否 | 否 |
| 1.26.10 | 944 | ready | 待补 | 是 | 否 | 规则就绪（⇄ v898），待 hook 接线 |
| 1.26.20 | 975 | ready | 待补 | 是 | 否 | 否（规则 planned） |
| 1.26.30 | 1001 | ready | 待补 | 是 | 否 | 否（规则 planned） |
| 1.26.40 ~ 1.26.51 | 2168 ~ 2193 | planned | 待补 | 是 | 否 | 否 |
| 1.26.60 | 2225 | planned | 已填示例 | 是 | 否（签名适配器未接线） | 否 |

`可加载` = 插件能被 LeviLamina 加载，配置、语言、codec 扫描、路由表全部就绪并输出状态日志。

## 为什么必须按服务器版本单独构建

Glacie 的 Release 说明逐版绑定 BDS + LeviLamina（v3.10.3 = BDS 1.26.60.x + LeviLamina 26.60.4 + 协议 2225），其 DLL 内可见 BDS 内部符号（如 `?runDedicatedServerLoop@DedicatedServer@@...`）。插件 hook 的是 BDS 内部函数，符号名与签名逐版本变化，因此一个 DLL 只能对应一个服务器版本。这与「一个插件支持多个客户端协议」并不矛盾：客户端差异由运行时 codec 解决，服务端差异由构建期绑定解决。

## 待补 1：hook 符号表

每个版本需要至少三个点：

| 点 | 用途 |
| --- | --- |
| 收包点 | 把客户端原始字节交给 `ProtocolRouter::handleInbound` |
| 发包点 | 把服务端原始字节交给 `ProtocolRouter::handleOutbound` |
| Login / 握手点 | 读出 network protocol version，调用 `ProtocolRouter::onClientHello` |

填法：在 `versions.json` 对应版本的 `hookSymbols` 里加键，运行 `scripts/generate_matrix.py` 重新生成 `TargetConfig.h`，再在 `src/NetworkHooks.cpp` 里按该版本的函数签名写 `LL_AUTO_TYPED_INSTANCE_HOOK`。

## 待补 2：协议定义与翻译规则

开源 Protocol 仓库的分支情况（`git ls-remote` 实测只有三个分支）：

| 分支 | 协议 | 本工程状态 |
| --- | --- | --- |
| `v944` | 944 | 已接入，包头级编解码 |
| `v944`（派生） | 898 | 已接入：改协议号 / `abi_v898` 重编，1.21.13x 专属字段差异待实测校准 |
| `v975` | 975 | 已接入，包头级编解码 |
| `main` | 1001 | 已接入，含字段级 `IPacket` 模型 |

翻译规则引擎已上线（`src/CrossVersion.cpp` 的 `TranslationRules` + 插件目录的 `translation_rules.json`）：

- 规则按「源协议 -> 目标协议」逐对登记，`status=ready` 且两侧 codec 已加载才放行；
- 支持逐包策略：`deny`（丢弃并写日志）、`remap`（包 ID 重映射）、`pass`（原样）；
- `strategy=header` 包头级（当前 898 ⇄ 944 用这个）；`strategy=model` 字段级（需两侧 codec 开 `CV_ENABLE_FULL_PACKET_MODEL`，目前仅 1001 分支支持）；
- 未登记 / planned 的翻译对一律拒绝，不会静默放行坏数据。

首批就绪规则：**898 ⇄ 944（header 策略）**——v898 codec 由 v944 分支派生，两者包结构同源，header 级翻译成立。

其余协议号（1.21.11x ~ 1.21.12x = 844 / 859 / 860，以及 1.26.60 = 2225 等）在开源库里不存在，补齐路径：

1. 准备该协议的包定义（约 230 个包类）与 `Version.hpp`，或从最近的开源分支派生（参照 898 的做法，见 `codec_registry.json` 的 `fidelity: derived`）；
2. 在 `codec_registry.json` 把 `branch` / `abi` 填好、`status` 改为 `ready`；
3. 在 `translation_rules.json` 登记与目标服务端协议的规则对（先 planned 后 ready，用真实 diff 填 `deny` / `remap`）；
4. 重新运行 `generate_matrix.py` 与 `build_codecs.ps1`；
5. 用真实客户端做回归（登录、区块、方块、物品、移动、声音）。

每补充一个协议版本是「一个协议定义 + 与服务端协议之间的规则对」，工作量随版本数线性增长，规则对数随版本数组合增长。当前顺序：先补 944 ⇄ 975 ⇄ 1001 的差异清单（相邻分支，差异最小），再向 844 / 859 / 860 与 2168+ 段扩展。

## 本地自检

```bash
python3 scripts/verify_layout.py
```

校验 JSON、版本/协议号一致性、生成头、源码 include、CI 矩阵，不需要 Windows 工具链。
