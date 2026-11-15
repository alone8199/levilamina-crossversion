// Standalone 构建用的 ll::Logger 兼容头。
//
// 只有 CMake / CI 的 CV_STANDALONE_BUILD 路径会 include 到本文件（compat/ 加入
// include path）。xmake 真机构建使用 LeviLamina SDK 自带的 ll/api/Logger.h，
// 不会走到这里，两条构建路径互不干扰。
//
// 目的：让插件核心（配置、路由表、codec 加载、翻译链、会话管理）在没有
// LeviLamina SDK 和 BDS 符号的机器上也能被编译、链接、验证，产出的 DLL 是
// passthrough 骨架，不含任何 BDS hook。
#pragma once

#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ll {

class Logger {
public:
    explicit Logger(std::string name) : mName(std::move(name)) {}

    template <typename... Args>
    void info(const std::string& fmt, Args&&... args) const { write("info", fmt, std::forward<Args>(args)...); }

    template <typename... Args>
    void warn(const std::string& fmt, Args&&... args) const { write("warn", fmt, std::forward<Args>(args)...); }

    template <typename... Args>
    void error(const std::string& fmt, Args&&... args) const { write("error", fmt, std::forward<Args>(args)...); }

    template <typename... Args>
    void debug(const std::string& fmt, Args&&... args) const { write("debug", fmt, std::forward<Args>(args)...); }

private:
    template <typename T>
    static std::string stringify(T&& value) {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }

    template <typename... Args>
    void write(const char* level, std::string text, Args&&... args) const {
        const std::vector<std::string> values{stringify(std::forward<Args>(args))...};
        std::size_t cursor = 0;
        for (const auto& value : values) {
            const auto pos = text.find("{}", cursor);
            if (pos == std::string::npos) break;
            text.replace(pos, 2, value);
            cursor = pos + value.size();
        }
        const std::string line = "[" + mName + "] [" + level + "] " + text;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            std::cout << line << std::endl;
        }
#ifdef _WIN32
        OutputDebugStringA((line + "\n").c_str());
#endif
    }

    std::string         mName{};
    mutable std::mutex  mMutex{};
};

} // namespace ll
