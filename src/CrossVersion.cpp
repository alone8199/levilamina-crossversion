#include "CrossVersion.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace cv {
namespace {

using Json = nlohmann::json;

Json readJson(const std::filesystem::path& path);

// ------------------------------------------------------------ 本地化

class Localization {
public:
    static Localization& instance() {
        static Localization value;
        return value;
    }

    void load(const std::filesystem::path& directory, const std::string& language) {
        std::lock_guard<std::mutex> lock(mMutex);
        mMessages.clear();
        const auto path = directory / "lang" / (language + ".json");
        Json root = readJson(path);
        if (root.is_object()) {
            for (auto it = root.begin(); it != root.end(); ++it) {
                if (it.value().is_string()) mMessages[it.key()] = it.value().get<std::string>();
            }
        }
    }

    std::string get(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mMessages.find(key);
        return it == mMessages.end() ? key : it->second;
    }

private:
    mutable std::mutex                          mMutex{};
    std::unordered_map<std::string, std::string> mMessages{};
};

Json readJson(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return Json{};
    std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    // config.jsonc 允许 // 与 /* */ 注释，先做一次不误解字符串字面量的剥离。
    std::string clean;
    clean.reserve(text.size());
    bool inString = false, inLine = false, inBlock = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const char n = (i + 1 < text.size()) ? text[i + 1] : '\0';
        if (inLine) {
            if (c == '\n') { inLine = false; clean.push_back(c); }
            continue;
        }
        if (inBlock) {
            if (c == '*' && n == '/') { inBlock = false; ++i; }
            continue;
        }
        if (inString) {
            clean.push_back(c);
            if (c == '\\' && n != '\0') { clean.push_back(n); ++i; }
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; clean.push_back(c); continue; }
        if (c == '/' && n == '/') { inLine = true; ++i; continue; }
        if (c == '/' && n == '*') { inBlock = true; ++i; continue; }
        clean.push_back(c);
    }
    Json parsed = Json::parse(clean, nullptr, false);
    return parsed.is_discarded() ? Json{} : parsed;
}

std::filesystem::path moduleDirectory() {
#ifdef _WIN32
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&moduleDirectory),
            &module
        )) {
        return std::filesystem::current_path();
    }
    char buffer[MAX_PATH]{};
    GetModuleFileNameA(module, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

std::string joinProtocols(const std::vector<std::uint32_t>& values) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) oss << ", ";
        oss << values[i];
    }
    return oss.str();
}

} // namespace

// ---------------------------------------------------------------- 本地化入口

std::string tr(const std::string& key, const std::vector<std::string>& args) {
    std::string pattern = Localization::instance().get(key);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string token = "{" + std::to_string(i) + "}";
        std::size_t       pos   = 0;
        while ((pos = pattern.find(token, pos)) != std::string::npos) {
            pattern.replace(pos, token.size(), args[i]);
            pos += args[i].size();
        }
    }
    return pattern;
}

// ---------------------------------------------------------------- 配置

ConfigStore& ConfigStore::instance() {
    static ConfigStore value;
    return value;
}

bool ConfigStore::load(const std::filesystem::path& path) {
    Json root = readJson(path);
    if (!root.is_object()) return false;

    std::lock_guard<std::mutex> lock(mMutex);
    if (root.contains("mode") && root["mode"] == "strict") mConfig.mode = RouteMode::Strict;
    else mConfig.mode = RouteMode::Passthrough;

    if (root.contains("codecDirectory") && root["codecDirectory"].is_string()) {
        mConfig.codecDirectory = root["codecDirectory"].get<std::string>();
    }
    if (root.contains("language") && root["language"].is_string()) {
        mConfig.language = root["language"].get<std::string>();
    }
    if (root.contains("logSessions") && root["logSessions"].is_boolean()) {
        mConfig.logSessions = root["logSessions"].get<bool>();
    }
    if (root.contains("features") && root["features"].is_object()) {
        const auto& features = root["features"];
        if (features.contains("translateLogin") && features["translateLogin"].is_boolean()) {
            mConfig.translateLogin = features["translateLogin"].get<bool>();
        }
        if (features.contains("translatePackets") && features["translatePackets"].is_boolean()) {
            mConfig.translatePackets = features["translatePackets"].get<bool>();
        }
    }
    if (root.contains("allowedProtocols")) {
        const auto& allowed = root["allowedProtocols"];
        if (allowed.is_string() && allowed.get<std::string>() == "all") {
            mConfig.allowAll = true;
            mConfig.allowed.clear();
        } else if (allowed.is_array()) {
            mConfig.allowAll = false;
            mConfig.allowed.clear();
            for (const auto& item : allowed) mConfig.allowed.insert(item.get<std::uint32_t>());
        }
    }
    if (root.contains("deniedProtocols") && root["deniedProtocols"].is_array()) {
        mConfig.denied.clear();
        for (const auto& item : root["deniedProtocols"]) mConfig.denied.insert(item.get<std::uint32_t>());
    }
    return true;
}

// ---------------------------------------------------------------- codec

CodecManager& CodecManager::instance() {
    static CodecManager value;
    return value;
}

std::size_t CodecManager::loadAll(const std::filesystem::path& directory) {
    std::lock_guard<std::mutex> lock(mMutex);
    unloadAllLocked();

    std::error_code ec;
    if (!std::filesystem::exists(directory, ec)) {
        logWarn(formatText("Codec directory: {0}", directory.string()));
        return 0;
    }

#ifdef _WIN32
    using GetApiFn = const CvCodecApi* (*)();
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) break;
        const auto path = entry.path();
        const auto name = path.filename().string();
        if (name.size() < 12 || name.rfind("codec_v", 0) != 0 || path.extension() != ".dll") continue;

#ifdef _WIN32
        HMODULE module = LoadLibraryA(path.string().c_str());
        if (!module) continue;
        auto getApi = reinterpret_cast<GetApiFn>(GetProcAddress(module, "cv_get_api"));
        if (!getApi) { FreeLibrary(module); continue; }
        const CvCodecApi* api = getApi();
        if (!api || api->apiVersion != 1u || !api->decode || !api->encode) {
            FreeLibrary(module);
            continue;
        }
        LoadedCodec codec{};
        codec.protocol  = api->protocolVersion;
        codec.path      = path.string();
        codec.module    = module;
        codec.api       = api;
        codec.session   = api->createSession ? api->createSession() : nullptr;
        codec.fullModel = api->decodeFull && api->encodeFull;
        mCodecs[codec.protocol] = codec;
        logInfo(formatText("Codec v{0} loaded", codec.protocol) + (codec.fullModel ? " (full model)" : ""));
#endif
    }
#endif
    return mCodecs.size();
}

const LoadedCodec* CodecManager::find(std::uint32_t protocol) const {
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mCodecs.find(protocol);
    return it == mCodecs.end() ? nullptr : &it->second;
}

std::vector<std::uint32_t> CodecManager::loadedProtocols() const {
    std::lock_guard<std::mutex> lock(mMutex);
    std::vector<std::uint32_t> result;
    result.reserve(mCodecs.size());
    for (const auto& [protocol, codec] : mCodecs) result.push_back(protocol);
    std::sort(result.begin(), result.end());
    return result;
}

void CodecManager::unloadAll() {
    std::lock_guard<std::mutex> lock(mMutex);
    unloadAllLocked();
}

void CodecManager::unloadAllLocked() {
#ifdef _WIN32
    for (auto& [protocol, codec] : mCodecs) {
        if (codec.api && codec.api->destroySession && codec.session) codec.api->destroySession(codec.session);
        if (codec.module) FreeLibrary(static_cast<HMODULE>(codec.module));
    }
#endif
    mCodecs.clear();
}

// ---------------------------------------------------------------- 翻译规则表

TranslationRules& TranslationRules::instance() {
    static TranslationRules value;
    return value;
}

TranslationPair* TranslationRules::pairRef(std::uint32_t from, std::uint32_t to) {
    const std::uint64_t key = (static_cast<std::uint64_t>(from) << 32) | to;
    auto                it  = mPairs.find(key);
    return it == mPairs.end() ? nullptr : &it->second;
}

bool TranslationRules::load(const std::filesystem::path& path) {
    Json root = readJson(path);
    if (!root.is_object() || !root.contains("pairs") || !root["pairs"].is_array()) {
        logWarn("translation_rules.json not found or invalid; cross-version translation disabled");
        return false;
    }

    std::lock_guard<std::mutex> lock(mMutex);
    mPairs.clear();
    for (const auto& item : root["pairs"]) {
        if (!item.is_object() || !item.contains("from") || !item.contains("to")) continue;
        TranslationPair pair{};
        pair.from     = item["from"].get<std::uint32_t>();
        pair.to       = item["to"].get<std::uint32_t>();
        pair.ready    = item.contains("status") && item["status"] == "ready";
        if (item.contains("strategy") && item["strategy"].is_string()) pair.strategy = item["strategy"].get<std::string>();
        if (item.contains("fidelity") && item["fidelity"].is_string()) pair.fidelity = item["fidelity"].get<std::string>();
        if (item.contains("note") && item["note"].is_string())         pair.note     = item["note"].get<std::string>();
        if (item.contains("packets") && item["packets"].is_object()) {
            const auto& packets = item["packets"];
            if (packets.contains("deny") && packets["deny"].is_array()) {
                for (const auto& id : packets["deny"]) pair.deny.insert(id.get<std::uint32_t>());
            }
            if (packets.contains("remap") && packets["remap"].is_object()) {
                for (auto it = packets["remap"].begin(); it != packets["remap"].end(); ++it) {
                    pair.remap[static_cast<std::uint32_t>(std::stoul(it.key()))] = it.value().get<std::uint32_t>();
                }
            }
        }
        mPairs[(static_cast<std::uint64_t>(pair.from) << 32) | pair.to] = std::move(pair);
    }
    return true;
}

std::size_t TranslationRules::pairCount() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mPairs.size();
}

std::size_t TranslationRules::readyCount() const {
    std::lock_guard<std::mutex> lock(mMutex);
    std::size_t count = 0;
    for (const auto& [key, pair] : mPairs) {
        if (pair.ready) ++count;
    }
    return count;
}

const TranslationPair* TranslationRules::find(std::uint32_t from, std::uint32_t to) const {
    std::lock_guard<std::mutex> lock(mMutex);
    const std::uint64_t key = (static_cast<std::uint64_t>(from) << 32) | to;
    auto                it  = mPairs.find(key);
    return it == mPairs.end() ? nullptr : &it->second;
}

PacketPolicy TranslationRules::policyFor(std::uint32_t from, std::uint32_t to, std::uint32_t packetId) const {
    std::lock_guard<std::mutex> lock(mMutex);
    const std::uint64_t key = (static_cast<std::uint64_t>(from) << 32) | to;
    auto                it  = mPairs.find(key);
    if (it == mPairs.end()) return PacketPolicy::NoRule;
    const TranslationPair& pair = it->second;
    if (!pair.ready) return PacketPolicy::Deny;
    if (pair.deny.count(packetId) != 0) return PacketPolicy::Deny;
    if (pair.remap.count(packetId) != 0) return PacketPolicy::Remap;
    return PacketPolicy::Pass;
}

// ---------------------------------------------------------------- 翻译链

TranslationPipeline& TranslationPipeline::instance() {
    static TranslationPipeline value;
    return value;
}

bool TranslationPipeline::hasRule(std::uint32_t clientProtocol, std::uint32_t serverProtocol) const {
    if (clientProtocol == serverProtocol) return true;
    // 规则按「客户端协议 -> 服务端协议」登记在 translation_rules.json，
    // 只有 status=ready 的翻译对可用；未登记时由路由层按 mode 回退到拒绝。
    const TranslationPair* pair = TranslationRules::instance().find(clientProtocol, serverProtocol);
    return pair != nullptr && pair->ready;
}

std::vector<std::uint8_t> TranslationPipeline::process(
    std::uint32_t       fromProtocol,
    std::uint32_t       toProtocol,
    const std::uint8_t* data,
    std::size_t         size,
    bool&               ok
) {
    ok = false;
    if (!data || size == 0) return {};

    const LoadedCodec* from = CodecManager::instance().find(fromProtocol);
    const LoadedCodec* to   = CodecManager::instance().find(toProtocol);
    if (!from || !to) {
        if (fromProtocol != toProtocol) {
            // 跨协议但 codec 缺失：宁可丢弃也不能把 A 版本字节原样发给 B 版本。
            logWarn(formatText("Translation v{0}->v{1}: codec missing, packet dropped", fromProtocol, toProtocol));
            return {};
        }
        return std::vector<std::uint8_t>(data, data + size);
    }

    const TranslationPair* rule = nullptr;
    if (fromProtocol != toProtocol) {
        rule = TranslationRules::instance().find(fromProtocol, toProtocol);
        if (!rule || !rule->ready) {
            logWarn(formatText("Translation v{0}->v{1}: no ready rule, packet dropped", fromProtocol, toProtocol));
            return {};
        }
    }

    // 字段级模型目前只在同一个 codec 内成对使用：跨 codec 的 IPacket 属于不同 DLL，
    // 需要先引入统一中间表示（IR）才能互转。跨版本翻译走包头级 + 规则表，
    // model 策略在两侧 codec 均以 CV_ENABLE_FULL_PACKET_MODEL=ON 编译后启用。
    const bool useFull = from->fullModel && to->fullModel && from->api->decodeFull
                         && (from->protocol == to->protocol);
    CvPacket packet{};
    const std::int32_t decodeResult = useFull
        ? from->api->decodeFull(from->session, data, size, &packet)
        : from->api->decode(from->session, data, size, &packet);
    if (decodeResult != 0) {
        logWarn(formatText("Packet decode failed for protocol {0}", fromProtocol));
        return {};
    }

    // 单包策略：deny 丢弃、remap 重写包 ID、pass 原样。
    if (rule) {
        const PacketPolicy policy = TranslationRules::instance().policyFor(fromProtocol, toProtocol, packet.packetId);
        if (policy == PacketPolicy::Deny) {
            if (from->api->releasePacket) from->api->releasePacket(&packet);
            logWarn(formatText("Packet {0} denied by rule v{1}->v{2}, dropped", packet.packetId, fromProtocol, toProtocol));
            ok = true;
            return {};
        }
        if (policy == PacketPolicy::Remap) {
            auto it = rule->remap.find(packet.packetId);
            if (it != rule->remap.end()) packet.packetId = it->second;
        }
    }

    packet.protocolVersion = toProtocol;

    std::uint8_t* output = nullptr;
    std::size_t   outputSize = 0;
    const std::int32_t encodeResult = (useFull && to->api->encodeFull && packet.handle)
        ? to->api->encodeFull(to->session, &packet, &output, &outputSize)
        : to->api->encode(to->session, &packet, &output, &outputSize);

    if (from->api->releasePacket) from->api->releasePacket(&packet);
    if (encodeResult != 0 || !output) {
        logWarn(formatText("Packet encode failed for protocol {0}", toProtocol));
        return {};
    }

    std::vector<std::uint8_t> result(output, output + outputSize);
    if (to->api->freeBuffer) to->api->freeBuffer(output);
    ok = true;
    return result;
}

// ---------------------------------------------------------------- 路由

ProtocolRouter& ProtocolRouter::instance() {
    static ProtocolRouter value;
    return value;
}

RouteResult ProtocolRouter::onClientHello(std::uint64_t guid, std::uint32_t clientProtocol) {
    const Config& config = ConfigStore::instance().get();
    RouteResult   result;
    result.clientProtocol = clientProtocol;

    if (!isKnownProtocol(clientProtocol)) {
        result.reason = formatText("Network protocol {0} is not supported", clientProtocol);
        result.decision = RouteDecision::Reject;
    } else if (config.denied.count(clientProtocol) != 0 || (!config.allowAll && config.allowed.count(clientProtocol) == 0)) {
        result.reason = formatText("Network protocol {0} is denied by config", clientProtocol);
        result.decision = RouteDecision::Reject;
    } else if (clientProtocol == kServerProtocol) {
        result.decision = RouteDecision::Passthrough;
        result.reason   = "same protocol";
    } else if (TranslationPipeline::instance().hasRule(clientProtocol, kServerProtocol)
               && CodecManager::instance().find(clientProtocol)
               && CodecManager::instance().find(kServerProtocol)) {
        result.decision = RouteDecision::Translate;
        result.reason   = "rule + codecs available";
    } else if (config.mode == RouteMode::Strict) {
        result.decision = RouteDecision::Reject;
        result.reason   = formatText("Translation pair v{0}->v{1} is not implemented", clientProtocol, kServerProtocol);
    } else {
        result.decision = RouteDecision::Reject;
        result.reason   = formatText("Translation pair v{0}->v{1} is not implemented", clientProtocol, kServerProtocol);
    }

    {
        std::lock_guard<std::mutex> lock(mMutex);
        mSessions[guid] = SessionInfo{guid, clientProtocol, result.decision};
    }

    if (config.logSessions) {
        logInfo(formatText("Route: client v{0} -> server v{1} ({2})", clientProtocol, kServerProtocol, result.reason));
    }
    if (result.decision == RouteDecision::Reject) logWarn(result.reason);
    return result;
}

std::optional<SessionInfo> ProtocolRouter::session(std::uint64_t guid) const {
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mSessions.find(guid);
    if (it == mSessions.end()) return std::nullopt;
    return it->second;
}

void ProtocolRouter::onDisconnect(std::uint64_t guid) {
    std::lock_guard<std::mutex> lock(mMutex);
    mSessions.erase(guid);
}

std::vector<std::uint8_t> ProtocolRouter::handleInbound(std::uint64_t guid, const std::uint8_t* data, std::size_t size) {
    auto info = session(guid);
    if (!info) return {};
    if (info->decision == RouteDecision::Reject) return {};
    if (info->decision == RouteDecision::Passthrough) return std::vector<std::uint8_t>(data, data + size);
    bool ok = false;
    return TranslationPipeline::instance().process(info->clientProtocol, kServerProtocol, data, size, ok);
}

std::vector<std::uint8_t> ProtocolRouter::handleOutbound(std::uint64_t guid, const std::uint8_t* data, std::size_t size) {
    auto info = session(guid);
    if (!info) return {};
    if (info->decision == RouteDecision::Reject) return {};
    if (info->decision == RouteDecision::Passthrough) return std::vector<std::uint8_t>(data, data + size);
    bool ok = false;
    return TranslationPipeline::instance().process(kServerProtocol, info->clientProtocol, data, size, ok);
}

// ---------------------------------------------------------------- 插件

Plugin& Plugin::instance() {
    static Plugin value;
    return value;
}

bool Plugin::initialize() {
    if (mReady) return true;
    mDirectory = moduleDirectory();

    if (!ConfigStore::instance().load(mDirectory / "config.jsonc")) {
        logWarn("config.jsonc not found or invalid, using defaults");
    }
    Localization::instance().load(mDirectory, ConfigStore::instance().get().language);

    const auto codecDir = mDirectory / ConfigStore::instance().get().codecDirectory;
    const auto count = CodecManager::instance().loadAll(codecDir);
    TranslationRules::instance().load(mDirectory / "translation_rules.json");

    logInfo(formatText("CrossVersion loaded! Target BDS {0}, server protocol {1}", std::string(kBdsVersion), kServerProtocol));
    logInfo(formatText("Registered {0} client protocols", kAllProtocols.size()));
    logInfo(formatText("Codec directory: {0}", codecDir.string()));
    logInfo(formatText("Translation rules: {0} pairs, {1} ready",
                   TranslationRules::instance().pairCount(), TranslationRules::instance().readyCount()));
    logInfo(formatText("Supported client protocols: {0}",
                   joinProtocols(std::vector<std::uint32_t>(kAllProtocols.begin(), kAllProtocols.end()))));

    if (kReadyProtocols.empty()) {
        logWarn("No codec compiled for any protocol; passthrough-only build");
    } else if (count == 0) {
        logWarn("No codec_v*.dll found in codec directory");
    }

    mReady = true;
    return true;
}

void Plugin::shutdown() {
    if (!mReady) return;
    CodecManager::instance().unloadAll();
    mReady = false;
}

bool Plugin::reload() {
    ConfigStore::instance().load(mDirectory / "config.jsonc");
    Localization::instance().load(mDirectory, ConfigStore::instance().get().language);
    TranslationRules::instance().load(mDirectory / "translation_rules.json");
    CodecManager::instance().loadAll(mDirectory / ConfigStore::instance().get().codecDirectory);
    logInfo(tr("Configuration reloaded"));
    return true;
}

std::string Plugin::status() const {
    const auto loaded = CodecManager::instance().loadedProtocols();
    std::ostringstream oss;
    oss << "CrossVersion / BDS " << kBdsVersion << " / server protocol " << kServerProtocol << "\n"
        << "registered protocols: " << kAllProtocols.size() << "\n"
        << "codecs ready in registry: " << kReadyProtocols.size() << "\n"
        << "codecs loaded: " << (loaded.empty() ? "none" : joinProtocols(loaded)) << "\n"
        << "translation rules: " << TranslationRules::instance().pairCount()
        << " pairs (" << TranslationRules::instance().readyCount() << " ready)\n"
        << "hook symbols: " << (kHasAnyHook ? "present" : "not filled (network interception disabled)") << "\n"
        << "mode: " << (ConfigStore::instance().get().mode == RouteMode::Strict ? "strict" : "passthrough");
    return oss.str();
}

} // namespace cv
