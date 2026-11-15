// 自动生成，请勿手改。来源: codec_registry.json
#pragma once
#include <array>
#include <cstdint>

namespace cv {
inline constexpr std::array<std::uint32_t, 26> kAllProtocols = {
    662u, 671u, 685u, 712u, 729u, 748u, 766u, 776u, 786u, 800u, 818u, 819u, 827u, 844u, 859u, 860u, 898u, 924u, 944u, 975u, 1001u, 2168u, 2169u, 2192u, 2193u, 2225u
};
inline constexpr std::array<std::uint32_t, 3> kReadyProtocols = {
    944u, 975u, 1001u
};

inline constexpr bool isKnownProtocol(std::uint32_t p) noexcept {
    for (auto v : kAllProtocols) { if (v == p) return true; }
    return false;
}

} // namespace cv
