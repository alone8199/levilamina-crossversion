# codec_v*.dll

每个 `codec_v<协议号>.dll` 是一个协议版本的编解码器，由 [SculkCatalystMC/Protocol](https://github.com/SculkCatalystMC/Protocol) 的对应分支编译而来。主插件在运行时按客户端协议号动态加载它们，互不冲突（各分支有独立的 `abi_vXXXX` 内联 ABI 命名空间）。

## 构建

在 Windows + MSVC 环境下执行：

```powershell
./scripts/build_codecs.ps1
```

脚本按 `codec_registry.json` 中 `status=ready` 的条目逐个执行：

| 协议 | Protocol 分支 | ABI 命名空间 | 字段级模型 |
| --- | --- | --- | --- |
| 944 | `v944` | `abi_v944` | 关（分支 API 与 main 不同） |
| 975 | `v975` | `abi_v975` | 关 |
| 1001 | `main` | `abi_v1001` | 开（`CV_ENABLE_FULL_PACKET_MODEL=ON`） |

单个分支手工构建：

```powershell
git clone --depth 1 -b main https://github.com/SculkCatalystMC/Protocol.git vendor/protocol/v1001
cmake -S codec -B build/codecs/v1001 -A x64 `
  -DPROTOCOL_SOURCE_DIR=vendor/protocol/v1001 `
  -DCV_PROTOCOL_VERSION=1001 -DCV_ABI_NS=abi_v1001 -DCV_BRANCH=main `
  -DCV_ENABLE_FULL_PACKET_MODEL=ON
cmake --build build/codecs/v1001 --config Release
```

`static_assert(SCULK_NETWORK_PROTOCOL_VERSION == CV_PROTOCOL_VERSION)` 会阻止分支与协议号配错。

## C ABI

见 `src/CodecApi.h`。主插件通过 `GetProcAddress("cv_get_api")` 拿到 `CvCodecApi` 函数表，不链接 codec，因此新增协议版本不需要重新编译主插件。

- `decode` / `encode`：包头级（varint 头 + 子客户端 ID + 原始 payload），三个分支通用，不依赖易变的 `IPacket` API。
- `decodeFull` / `encodeFull`：字段级 `IPacket` 模型，仅当以 `CV_ENABLE_FULL_PACKET_MODEL=ON` 编译时可用，否则为 `nullptr`。

`CvPacket::handle` 保存字段级模型指针，由 `releasePacket` 在同一个 codec DLL 内释放，不跨 DLL 边界析构。

## 新增一个协议版本

1. 为目标协议号准备一份 Protocol 分支（包定义 + `Version.hpp` 里的协议号）。
2. 在 `codec_registry.json` 把该条目的 `branch` / `abi` 填好、`status` 改为 `ready`。
3. 重新运行 `scripts/generate_matrix.py` 与 `scripts/build_codecs.ps1`。
4. 在 `TranslationPipeline` 中登记 `客户端协议 -> 服务端协议` 的翻译规则；未登记时按 `config.jsonc` 的 `mode` 回退。

注意：开源 Protocol 仓库只提供 v944 / v975 / main 三个分支。`codec_registry.json` 里其余协议号是**路由表占位**，需要自行补齐包定义（每个版本约 230 个包类）后才能变成 `ready`。
