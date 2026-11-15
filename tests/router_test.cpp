// 路由层自测：不依赖 BDS / LeviLamina / Windows，CI 的 ubuntu job 直接跑。
// 验证目标版本的注入头（TargetConfig.h / ProtocolTable.h）与配置解析、路由决策正确。
#include "CrossVersion.h"

#include <cstdlib>
#include <iostream>
#include <string>

#ifndef CV_SOURCE_DIR
#define CV_SOURCE_DIR "."
#endif

namespace {

int gFailures = 0;

void check(bool condition, const std::string& name) {
    std::cout << (condition ? "[ PASS ] " : "[ FAIL ] ") << name << '\n';
    if (!condition) ++gFailures;
}

} // namespace

int main() {
    using namespace cv;

    std::cout << "CrossVersion router self-test\n"
              << "  BDS             : " << kBdsVersion << '\n'
              << "  server protocol : " << kServerProtocol << '\n'
              << "  registered      : " << kAllProtocols.size() << " protocols\n"
              << "  ready codecs    : " << kReadyProtocols.size() << '\n';

    // 注入头必须自洽：本构建的服务端协议一定在路由表里。
    check(isKnownProtocol(kServerProtocol), "server protocol is registered");
    check(kAllProtocols.size() >= 20, "routing table covers the supported range");
    check(kBdsVersion[0] != '\0', "target BDS version is injected");

    // jsonc 配置解析（含注释、allowedProtocols = all）。
    const std::string configPath = std::string(CV_SOURCE_DIR) + "/config/config.jsonc";
    const bool configLoaded = ConfigStore::instance().load(configPath);
    check(configLoaded, "config.jsonc parses (JSONC with comments)");

    // 翻译规则表：加载、方向性、逐对状态。
    const bool rulesLoaded = TranslationRules::instance().load(
        std::string(CV_SOURCE_DIR) + "/translation_rules.json");
    check(rulesLoaded, "translation_rules.json parses (JSONC with comments)");
    check(TranslationRules::instance().pairCount() >= 12, "rule table has all ready-protocol pairs");
    check(TranslationRules::instance().readyCount() >= 2, "rule table has ready pairs (898 <-> 944)");
    check(TranslationRules::instance().find(898u, 944u) != nullptr, "898 -> 944 rule registered");
    check(TranslationRules::instance().find(944u, 898u) != nullptr, "944 -> 898 rule registered (symmetric)");
    check(TranslationRules::instance().find(898u, 975u) != nullptr
              && !TranslationRules::instance().find(898u, 975u)->ready,
          "898 -> 975 rule registered as planned (not ready)");
    check(TranslationRules::instance().find(662u, 944u) == nullptr,
          "unregistered pair has no rule");
    check(TranslationRules::instance().policyFor(898u, 944u, 4u) == PacketPolicy::Pass,
          "header strategy passes packets by default");
    check(TranslationRules::instance().policyFor(898u, 975u, 4u) == PacketPolicy::Deny,
          "planned pair denies packets (no silent passthrough)");
    check(TranslationRules::instance().policyFor(898u, 944u, 1u) == PacketPolicy::Pass,
          "policy falls back to pass for unknown packet ids");
    check(TranslationPipeline::instance().hasRule(898u, 944u), "pipeline sees ready rule");
    check(!TranslationPipeline::instance().hasRule(898u, 975u), "pipeline rejects planned rule");

    // 路由决策。
    const auto same = ProtocolRouter::instance().onClientHello(1u, kServerProtocol);
    check(same.decision == RouteDecision::Passthrough, "same protocol -> passthrough");

    const auto unknown = ProtocolRouter::instance().onClientHello(2u, 999999u);
    check(unknown.decision == RouteDecision::Reject, "unknown protocol -> reject");

    std::uint32_t other = 0;
    for (auto p : kAllProtocols) {
        if (p != kServerProtocol) { other = p; break; }
    }
    if (other != 0) {
        const auto cross = ProtocolRouter::instance().onClientHello(3u, other);
        // 规则表为空 / codec 未加载时，跨版本连接必须被明确拒绝，不能静默进服。
        check(cross.decision == RouteDecision::Reject,
              "unimplemented cross-version pair -> reject (no silent passthrough)");
    }
    check(ProtocolRouter::instance().session(1u).has_value(), "session is tracked by guid");
    ProtocolRouter::instance().onDisconnect(1u);
    check(!ProtocolRouter::instance().session(1u).has_value(), "session removed on disconnect");

    const std::string status = Plugin::instance().status();
    check(status.find(kBdsVersion) != std::string::npos, "status reports target BDS version");
    check(status.find(std::to_string(kServerProtocol)) != std::string::npos,
          "status reports server protocol");

    std::cout << (gFailures == 0 ? "\nALL TESTS PASSED\n" : "\nTESTS FAILED\n");
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
