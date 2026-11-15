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
std::string format(const std::string& key, Args&&... args) {
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
