// LeviLamina 插件入口。
// 官方插件模板用 DllMain + LL_AUTO_* hook 注册生命周期；这里在 PROCESS_ATTACH
// 完成初始化。生产环境若遇到 loader lock 相关问题，可把 initialize() 迁到
// LeviLamina 的 mod 启动回调（与 LL_AUTO_* 生命周期一致）。
#include "CrossVersion.h"

#include "ll/api/Logger.h"

#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
ll::Logger gLogger("CrossVersion");
} // namespace

namespace cv {

void logInfo(const std::string& message) { gLogger.info("{}", message); }
void logWarn(const std::string& message) { gLogger.warn("{}", message); }
void logError(const std::string& message) { gLogger.error("{}", message); }

} // namespace cv

#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        cv::Plugin::instance().initialize();
        cv::hooks::install();
    } else if (reason == DLL_PROCESS_DETACH) {
        cv::Plugin::instance().shutdown();
    }
    return TRUE;
}
#endif
