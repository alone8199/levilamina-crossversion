// BDS 网络接管点。
//
// 这是整个工程里唯一必须逐版本逆向的部分：不同 BDS 版本的 RakNet 收发符号和
// 函数签名都不同，所以符号表放在 versions.json 的 hookSymbols，由
// scripts/generate_matrix.py 注入 src/generated/TargetConfig.h。
//
// 当前状态：框架与注入点已就绪。填好符号表后，在此用 LL_AUTO_TYPED_INSTANCE_HOOK
// 挂接收发函数，并把 cv::ProtocolRouter::handleInbound / handleOutbound 接进去。
// 未填符号的版本仍可编译出可加载的 DLL（配置、路由表、codec 全部可用），
// 只是不做网络接管 —— 这正是 dist/COVERAGE.md 里 "hook 符号: 待补" 的含义。
#include "CrossVersion.h"

namespace cv::hooks {

bool install() {
    if constexpr (!kHasAnyHook) {
        logWarn(tr("Hook symbols for this BDS version are not filled in; network interception is disabled"));
        return false;
    } else {
        // 符号已存在，但函数签名需要该 BDS 版本的类型声明。
        // 接入步骤：
        //   1. 在 versions.json 补齐 hookSymbols（收包 / 发包 / Login 解析点）；
        //   2. 用 LL_AUTO_TYPED_INSTANCE_HOOK 按对应签名挂接；
        //   3. 收包 -> ProtocolRouter::handleInbound(guid, data, size)
        //      发包 -> ProtocolRouter::handleOutbound(guid, data, size)
        //   4. Login 包里读出 network protocol version ->
        //      ProtocolRouter::onClientHello(guid, protocol)。
        logWarn("Hook symbols are present but the signature adapter is not wired yet");
        return false;
    }
}

} // namespace cv::hooks
