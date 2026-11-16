// codec_v<协议号>.dll 的实现。由 codec/CMakeLists.txt 针对每个 Protocol 分支单独编译。
//
// 稳定的部分：Bedrock 包头（varint）解析与重组。三个分支的 API 会漂移
// （v944/v975 用 readNoHeader，main 用 read），所以包头级编解码不依赖这些易变 API。
// 字段级模型（IPacket / MinecraftPackets）仅在 CV_ENABLE_FULL_PACKET_MODEL=ON 时启用，
// 目前只在 main(v1001) 分支上验证过；接入点已留在 decodeFull / encodeFull。
#include "CodecApi.h"

// sculk/protocol/Version.hpp 只在 main(=1001) 分支存在；v944 / v975 / 派生分支没有。
// 没有 Version.hpp 的分支用 CI 注入的 CV_MC_VERSION 字符串。
#ifdef CV_HAS_VERSION_HPP
#include "sculk/protocol/Version.hpp"
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifndef CV_PROTOCOL_VERSION
#define CV_PROTOCOL_VERSION 1001
#endif
#ifndef CV_ABI_NS
#define CV_ABI_NS "abi_v1001"
#endif
#ifndef CV_MC_VERSION
#define CV_MC_VERSION "unknown"
#endif

#ifdef CV_HAS_VERSION_HPP
static_assert(
    SCULK_NETWORK_PROTOCOL_VERSION == CV_PROTOCOL_VERSION,
    "Protocol branch does not match CV_PROTOCOL_VERSION"
);
#endif

#ifdef CV_ENABLE_FULL_PACKET_MODEL
#include "sculk/protocol/codec/MinecraftPackets.hpp"
#include "sculk/protocol/codec/packet/IPacket.hpp"
#include "sculk/protocol/utility/BinaryStream.hpp"
#include <memory>
#include <span>
#endif

namespace {

struct CvSession {
    std::vector<std::uint8_t> scratch;
};

bool readVarInt(const std::uint8_t* data, std::size_t size, std::size_t& offset, std::uint32_t& out) noexcept {
    std::uint32_t value = 0;
    for (int i = 0; i < 5; ++i) {
        if (offset >= size) return false;
        const std::uint8_t byte = data[offset++];
        value |= static_cast<std::uint32_t>(byte & 0x7F) << (7 * i);
        if ((byte & 0x80) == 0) {
            out = value;
            return true;
        }
    }
    return false;
}

void writeVarInt(std::vector<std::uint8_t>& buffer, std::uint32_t value) {
    while (value >= 0x80) {
        buffer.push_back(static_cast<std::uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buffer.push_back(static_cast<std::uint8_t>(value));
}

std::uint8_t* copyOut(const std::vector<std::uint8_t>& buffer, std::size_t* outSize) {
    auto* memory = static_cast<std::uint8_t*>(std::malloc(buffer.size()));
    if (!memory) return nullptr;
    if (!buffer.empty()) std::memcpy(memory, buffer.data(), buffer.size());
    *outSize = buffer.size();
    return memory;
}

void* createSessionImpl() {
    return new CvSession{};
}

void destroySessionImpl(void* session) {
    delete static_cast<CvSession*>(session);
}

std::int32_t decodeImpl(void*, const std::uint8_t* data, std::size_t size, CvPacket* out) {
    if (!data || !out || size == 0) return -1;

    std::size_t   offset = 0;
    std::uint32_t header = 0;
    if (!readVarInt(data, size, offset, header)) return -2;

    std::memset(out, 0, sizeof(CvPacket));
    out->protocolVersion    = CV_PROTOCOL_VERSION;
    out->packetId           = header & 0x3FF;
    out->senderSubClientId  = static_cast<std::uint8_t>((header >> 10) & 3);
    out->targetSubClientId  = static_cast<std::uint8_t>((header >> 12) & 3);
    out->payload            = data + offset;
    out->payloadSize        = size - offset;
    out->handle             = nullptr;
    return 0;
}

std::int32_t encodeImpl(void*, const CvPacket* in, std::uint8_t** outData, std::size_t* outSize) {
    if (!in || !outData || !outSize) return -1;

    const std::uint32_t header =
        (in->packetId & 0x3FF)
        | (static_cast<std::uint32_t>(in->senderSubClientId & 3) << 10)
        | (static_cast<std::uint32_t>(in->targetSubClientId & 3) << 12);

    std::vector<std::uint8_t> buffer;
    buffer.reserve(5 + in->payloadSize);
    writeVarInt(buffer, header);
    if (in->payload && in->payloadSize > 0) {
        buffer.insert(buffer.end(), in->payload, in->payload + in->payloadSize);
    }

    auto* memory = copyOut(buffer, outSize);
    if (!memory) return -2;
    *outData = memory;
    return 0;
}

void freeBufferImpl(void* buffer) {
    std::free(buffer);
}

#ifdef CV_ENABLE_FULL_PACKET_MODEL
std::int32_t decodeFullImpl(void*, const std::uint8_t* data, std::size_t size, CvPacket* out) {
    namespace sp = sculk::protocol;

    auto buffer = std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), size);
    auto result = sp::MinecraftPackets::readAndCreatePacketFromBuffer(buffer);
    if (!result) return -10;

    auto* packet = result->release();
    std::memset(out, 0, sizeof(CvPacket));
    out->protocolVersion    = CV_PROTOCOL_VERSION;
    out->packetId           = static_cast<std::uint32_t>(packet->getId());
    out->senderSubClientId  = packet->mSenderSubClientId;
    out->targetSubClientId  = packet->mTargetSubClientId;
    out->payload            = data;
    out->payloadSize        = size;
    out->handle             = packet;
    return 0;
}

std::int32_t encodeFullImpl(void*, const CvPacket* in, std::uint8_t** outData, std::size_t* outSize) {
    namespace sp = sculk::protocol;
    if (!in || !in->handle || !outData || !outSize) return -20;

    std::vector<std::byte> buffer;
    auto stream = sp::BinaryStream{buffer};
    auto* packet = static_cast<sp::IPacket*>(in->handle);
    packet->writeWithHeader(stream);

    auto* memory = static_cast<std::uint8_t*>(std::malloc(buffer.size()));
    if (!memory) return -21;
    if (!buffer.empty()) std::memcpy(memory, buffer.data(), buffer.size());
    *outData = memory;
    *outSize = buffer.size();
    return 0;
}
#endif

void releasePacketImpl(CvPacket* packet) {
    if (!packet || !packet->handle) return;
#ifdef CV_ENABLE_FULL_PACKET_MODEL
    delete static_cast<sculk::protocol::IPacket*>(packet->handle);
#endif
    packet->handle = nullptr;
}

const CvCodecApi kApi = {
    1u,
    static_cast<std::uint32_t>(CV_PROTOCOL_VERSION),
#ifdef CV_HAS_VERSION_HPP
    SCULK_MINECRAFT_VERSION,
#else
    CV_MC_VERSION,
#endif
    CV_ABI_NS,
    &createSessionImpl,
    &destroySessionImpl,
    &decodeImpl,
    &encodeImpl,
    &freeBufferImpl,
    &releasePacketImpl,
#ifdef CV_ENABLE_FULL_PACKET_MODEL
    &decodeFullImpl,
    &encodeFullImpl,
#else
    nullptr,
    nullptr,
#endif
};

} // namespace

CV_API const CvCodecApi* cv_get_api() {
    return &kApi;
}
