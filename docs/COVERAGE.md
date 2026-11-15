# 覆盖度与待补清单

本文件说明「理论兼容所有支持的版本」的准确边界，避免把框架就绪误读为协议翻译已完成。

## 三层覆盖度

| 层 | 状态 | 说明 |
| --- | --- | --- |
| 构建层（20 个 BDS 版本） | 完成 | `versions.json` + CI matrix，每个版本一个 zip，插件 DLL 与 LeviLamina 版本一一对应 |
| 路由层（26 个协议号） | 完成 | `ProtocolTable.h` 编译期注入，所有登记协议都能识别、按配置放行/拒绝/分流 |
| 编解码层（codec） | 3 / 26 | 944、975、1001 有可编译 codec；其余为路由表占位 |
| 翻译规则层 | 0 对 | `TranslationPipeline::hasRule` 当前只对同协议返回 true，跨版本字段级规则为空 |
| 网络接管层（hook 符号） | 1 / 20 | 仅 1.26.60 填了一条示例符号，且签名适配器未接线 |

## 逐版本矩阵

| BDS | 协议 | codec | hook | 可加载 | 可接管网络 | 可跨版本翻译 |
| --- | --- | --- | --- | --- | --- | --- |
| 1.21.30 ~ 1.21.130 | 729 ~ 898 | planned | 待补 | 是 | 否 | 否 |
| 1.26.0 | 924 | planned | 待补 | 是 | 否 | 否 |
| 1.26.10 | 944 | ready | 待补 | 是 | 否 | 否（规则未登记） |
| 1.26.20 | 975 | ready | 待补 | 是 | 否 | 否（规则未登记） |
| 1.26.30 | 1001 | ready | 待补 | 是 | 否 | 否（规则未登记） |
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

开源 Protocol 仓库的分支情况：

| 分支 | 协议 | 本工程状态 |
| --- | --- | --- |
| `v944` | 944 | 已接入，包头级编解码 |
| `v975` | 975 | 已接入，包头级编解码 |
| `main` | 1001 | 已接入，含字段级 `IPacket` 模型 |

其余协议号（含需求举例的 1.21.11x ~ 1.21.12x = 844 / 859 / 860，以及 1.26.60 = 2225）在开源库里不存在，需要：

1. 准备该协议的包定义（约 230 个包类）与 `Version.hpp`；
2. 在 `codec_registry.json` 把 `branch` / `abi` 填好、`status` 改为 `ready`；
3. 重新运行 `generate_matrix.py` 与 `build_codecs.ps1`；
4. 在 `TranslationPipeline` 登记 `客户端协议 → 服务端协议` 的规则：包 ID 重映射、字段增删兼容、物品/方块注册表映射、旧客户端 VirtualContent / VirtualActor 回退、登录鉴权差异；
5. 用真实客户端做回归（登录、区块、方块、物品、移动、声音）。

每补充一个协议版本是「一个协议定义 + 与服务端协议之间的规则对」，工作量随版本数线性增长，规则对数随版本数组合增长。建议先做 944 ↔ 975 ↔ 1001 三个同族协议，再向 1.21.x 与 2168+ 段扩展。

## 本地自检

```bash
python3 scripts/verify_layout.py
```

校验 JSON、版本/协议号一致性、生成头、源码 include、CI 矩阵，不需要 Windows 工具链。
