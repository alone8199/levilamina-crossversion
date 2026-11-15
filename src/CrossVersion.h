// CrossVersion 单插件核心声明。
// 一个 DLL 面向一个 BDS 版本；协议差异靠运行时加载的 codec_v*.dll 与路由表解决。
#pragma once

#include "CodecApi.h"
#include "generated/ProtocolTable.h"
#include "generated/TargetConfig.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cv {

// ---------------------------------------------------------------- 日志与本地化

void logInfo(const std::string& message);
void logWarn(const std::string& message);
void logError(const std::string& message);

std::string tr(const std::string& key, const std::vector<std::string>& args = {});

template <typename T>
std::string toString(T&& value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

template <typename... Args>
// 不使用 `format` 这个名字：MSVC 的 std::format 会通过 ADL 被带进候选集，
// 在参数含 std::string / 整数时造成 C2668 二义性。
std::string formatText(const std::string& key, Args&&... args) {
    return tr(key, std::vector<std::string>{toString(args)...});
}

// ---------------------------------------------------------------- 配置

enum class RouteMode { Strict, Passthrough };

struct Config {
    RouteMode                       mode{RouteMode::Passthrough};
    std::string                     codecDirectory{kCodecDir};
    std::string                     language{"zh_CN"};
    bool                            logSessions{true};
    bool                            translateLogin{true};
    bool                            translatePackets{true};
    bool                            allowAll{true};
    std::unordered_set<std::uint32_t> allowed{};
    std::unordered_set<std::uint32_t> denied{};
};

class ConfigStore {
public:
    static ConfigStore& instance();

    bool load(const std::filesystem::path& path);
    const Config& get() const { return mConfig; }
    Config&       mutableGet() { return mConfig; }

private:
    Config      mConfig{};
    mutable std::mutex mMutex{};
};

// ---------------------------------------------------------------- codec

struct LoadedCodec {
    std::uint32_t       protocol{0};
    std::string         path{};
    void*               module{nullptr};
    const CvCodecApi*   api{nullptr};
    void*               session{nullptr};
    bool                fullModel{false};
};

class CodecManager {
public:
    static CodecManager& instance();

    std::size_t loadAll(const std::filesystem::path& directory);
    const LoadedCodec*  find(std::uint32_t protocol) const;
    std::vector<std::uint32_t> loadedProtocols() const;
    void unloadAll();

private:
    void unloadAllLocked();
    mutable std::mutex                          mMutex{};
    std::unordered_map<std::uint32_t, LoadedCodec> mCodecs{};
};

// ---------------------------------------------------------------- 翻译规则表
// 数据源：插件目录下的 translation_rules.json（与 codec_registry.json 同级）。
// 路由层只对 status=ready 且两侧 codec 均已加载的翻译对放行。

enum class PacketPolicy { Pass, Remap, Deny, NoRule };

struct TranslationPair {
    std::uint32_t from{0};
    std::uint32_t to{0};
    bool          ready{false};
    std::string   strategy{"header"}; // header | model
    std::string   fidelity{};
    std::string   note{};
    std::unordered_set<std::uint32_t> deny{};
    std::unordered_map<std::uint32_t, std::uint32_t> remap{};
};

class TranslationRules {
public:
    static TranslationRules& instance();

    bool load(const std::filesystem::path& path);

    std::size_t pairCount() const;
    std::size_t readyCount() const;

    // 精确查找某方向的翻译对；不存在返回 nullptr。
    const TranslationPair* find(std::uint32_t from, std::uint32_t to) const;

    // 单包策略：Deny 丢弃并写日志；Remap 重写包 ID；Pass 原样；NoRule 未登记。
    PacketPolicy policyFor(std::uint32_t from, std::uint32_t to, std::uint32_t packetId) const;

private:
    TranslationPair*       pairRef(std::uint32_t from, std::uint32_t to);
    mutable std::mutex     mMutex{};
    std::unordered_map<std::uint64_t, TranslationPair> mPairs{};
};

// ---------------------------------------------------------------- 路由

enum class RouteDecision { Translate, Passthrough, Reject };

struct RouteResult {
    RouteDecision decision{RouteDecision::Reject};
    std::uint32_t clientProtocol{0};
    std::uint32_t serverProtocol{kServerProtocol};
    std::string   reason{};
};

struct SessionInfo {
    std::uint64_t guid{0};
    std::uint32_t clientProtocol{0};
    RouteDecision decision{RouteDecision::Reject};
};

class TranslationPipeline {
public:
    static TranslationPipeline& instance();

    // 翻译规则是否可用。规则按「客户端协议 -> 服务端协议」登记；
    // 未登记时返回 false，由路由层按 mode 回退到直通或拒绝。
    bool hasRule(std::uint32_t clientProtocol, std::uint32_t serverProtocol) const;

    // 把 data 从 fromProtocol 的包转换成 toProtocol 的包。
    std::vector<std::uint8_t> process(
        std::uint32_t fromProtocol,
        std::uint32_t toProtocol,
        const std::uint8_t* data,
        std::size_t size,
        bool& ok
    );
};

class ProtocolRouter {
public:
    static ProtocolRouter& instance();

    RouteResult onClientHello(std::uint64_t guid, std::uint32_t clientProtocol);
    std::optional<SessionInfo> session(std::uint64_t guid) const;
    void onDisconnect(std::uint64_t guid);

    // 网络 hook 接入点：返回需要交给 BDS / 客户端的字节；空表示丢弃该包。
    std::vector<std::uint8_t> handleInbound(std::uint64_t guid, const std::uint8_t* data, std::size_t size);
    std::vector<std::uint8_t> handleOutbound(std::uint64_t guid, const std::uint8_t* data, std::size_t size);

private:
    mutable std::mutex                          mMutex{};
    std::unordered_map<std::uint64_t, SessionInfo> mSessions{};
};

// ---------------------------------------------------------------- 插件

class Plugin {
public:
    static Plugin& instance();

    bool initialize();
    void shutdown();
    bool reload();
    std::string status() const;

    const std::filesystem::path& pluginDirectory() const { return mDirectory; }

private:
    std::filesystem::path mDirectory{};
    bool                  mReady{false};
};

namespace hooks {
// 按 versions.json 的 hookSymbols 接管 BDS 网络收发；返回是否成功安装。
bool install();
} // namespace hooks

} // namespace cv
