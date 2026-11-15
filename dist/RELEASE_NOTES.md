# CrossVersion v0.2.0

LeviLamina 基岩版跨版本兼容插件。本版新增：1.21.132 发行包、翻译规则引擎、首批跨版本翻译规则（v898 ⇄ v944）与 898/944/975/1001 四个协议编解码模块。

## 怎么选包

按你的 **BDS 服务器版本**下载对应 zip，解压得到 `CrossVersion` 文件夹，放进服务器的 `plugins` 目录，重启即可。不同服务器版本的包**不能混用**。

## 每个包支持什么

| 下载包 | 服务器版本 | ✅ 直连客户端 | 🔁 翻译就绪 | 自带编解码模块 |
| --- | --- | --- | --- | --- |
| `CrossVersion-levilamina-1.21.30-windows-x86_64.zip` | 1.21.30 | 1.21.3x（协议 729） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.21.40-windows-x86_64.zip` | 1.21.40 | 1.21.4x（协议 748） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.21.50-windows-x86_64.zip` | 1.21.50 | 1.21.5x（协议 766） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.21.60-windows-x86_64.zip` | 1.21.60 | 1.21.6x（协议 776） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.21.70-windows-x86_64.zip` | 1.21.70 | 1.21.7x（协议 786） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.21.80-windows-x86_64.zip` | 1.21.80 | 1.21.8x（协议 800） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.21.90-windows-x86_64.zip` | 1.21.90 | 1.21.9x（协议 818） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.21.100-windows-x86_64.zip` | 1.21.100 | 1.21.10x（协议 827） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.21.110-windows-x86_64.zip` | 1.21.110 | 1.21.11x（协议 844） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.21.120-windows-x86_64.zip` | 1.21.120 | 1.21.12x（协议 859） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.21.130-windows-x86_64.zip` | 1.21.130 | 1.21.13x / 1.21.132（协议 898） | 进服: v944；出服: v944 | ✅ |
| `CrossVersion-levilamina-1.21.132-windows-x86_64.zip` | 1.21.132 | 1.21.13x / 1.21.132（协议 898） | 进服: v944；出服: v944 | ✅ |
| `CrossVersion-levilamina-1.26.0-windows-x86_64.zip` | 1.26.0 | 1.26.x（协议 924） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.26.10-windows-x86_64.zip` | 1.26.10 | 1.26.1x（协议 944） | 进服: v898；出服: v898 | ✅ |
| `CrossVersion-levilamina-1.26.20-windows-x86_64.zip` | 1.26.20 | 1.26.2x（协议 975） | 暂无 | ✅ |
| `CrossVersion-levilamina-1.26.30-windows-x86_64.zip` | 1.26.30 | 1.26.3x / 26.30（协议 1001） | 暂无 | ✅ |
| `CrossVersion-levilamina-1.26.40-windows-x86_64.zip` | 1.26.40 | 1.26.40-1.26.44（协议 2168） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.26.45-windows-x86_64.zip` | 1.26.45 | 1.26.45（协议 2169） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.26.50-windows-x86_64.zip` | 1.26.50 | 1.26.50（协议 2192） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.26.51-windows-x86_64.zip` | 1.26.51 | 1.26.51（协议 2193） | 暂无 | ✘ |
| `CrossVersion-levilamina-1.26.60-windows-x86_64.zip` | 1.26.60 | 1.26.60（协议 2225） | 暂无 | ✘ |

所有包的路由表都认识 26 个客户端协议（1.20.7x ~ 1.26.60，完整对照表见仓库 docs/COMPATIBILITY.md）；未就绪的跨版本连接会被明确拒绝并写明日志，不会让坏数据进服。

## 包里有什么（每个包相同）

```
CrossVersion/
├── CrossVersion.dll        插件本体（对应你的服务器版本）
├── manifest.json           LeviLamina 加载清单
├── config.jsonc            配置（白名单/模式/语言，带中文注释）
├── translation_rules.json  跨版本翻译规则（可自行增删翻译对）
├── lang/                   中英文日志文案
├── README.txt              本版本说明
├── STATUS.txt              构建信息
├── TargetConfig.h          本版本编译期注入的常量
└── codecs/                 协议编解码模块 codec_v*.dll
```

## 已知边界（说实话）

1. 已就绪的跨版本翻译目前只有 v898 ⇄ v944 一对（v898 由 v944 派生，包结构同源）；
   其它版本对仍会拒绝，等差异分析逐对补齐
2. 派生 codec 的包结构以基准分支为准，1.21.13x 专属字段差异待实测校准
3. 本批 DLL 不接管 BDS 网络符号（hook 表待逐版本逆向补齐），仅做识别、分流与配置
4. 激活码？没有。联网校验？没有。全程开源免费

完整使用说明见仓库 README；技术细节与推进计划见 docs/。
