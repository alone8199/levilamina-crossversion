// CrossVersion 主插件与 codec_v*.dll 之间的 C ABI 契约。
// 两侧共用本头文件；主插件通过 GetProcAddress("cv_get_api") 取得函数表，不链接 codec。
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32) && defined(CV_CODEC_EXPORTS)
#define CV_API extern "C" __declspec(dllexport)
#else
#define CV_API extern "C"
#endif

// 一个已解码的 Bedrock 数据包。payload 不含 varint 包头。
struct CvPacket {
    std::uint32_t       protocolVersion;
    std::uint32_t       packetId;
    std::uint8_t        senderSubClientId;
    std::uint8_t        targetSubClientId;
    const std::uint8_t* payload;
    std::size_t         payloadSize;
    void*               handle; // 字段级模型句柄（IPacket*），可为 nullptr
};

// 返回值约定：0 = 成功，负数 = 错误码。
struct CvCodecApi {
    std::uint32_t apiVersion;
    std::uint32_t protocolVersion;
    const char*   minecraftVersion;
    const char*   abiNamespace;

    void* (*createSession)();
    void (*destroySession)(void* session);

    // 包头级解码/编码：三个协议分支通用，稳定可用。
    std::int32_t (*decode)(void* session, const std::uint8_t* data, std::size_t size, CvPacket* out);
    std::int32_t (*encode)(void* session, const CvPacket* in, std::uint8_t** outData, std::size_t* outSize);

    void (*freeBuffer)(void* buffer);
    void (*releasePacket)(CvPacket* packet);

    // 字段级模型：仅当 codec 以 CV_ENABLE_FULL_PACKET_MODEL=ON 编译时可用，否则为 nullptr。
    std::int32_t (*decodeFull)(void* session, const std::uint8_t* data, std::size_t size, CvPacket* out);
    std::int32_t (*encodeFull)(void* session, const CvPacket* in, std::uint8_t** outData, std::size_t* outSize);
};

CV_API const CvCodecApi* cv_get_api();
