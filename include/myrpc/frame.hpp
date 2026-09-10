#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "myrpc/status.hpp"

namespace myrpc
{

    enum class FrameType : std::uint16_t
    {
        kRequest = 1,
        kResponse = 2,
        kCancel = 3
    };

    // 一次 RPC 在 TCP 字节流中的逻辑帧。payload 保存序列化后的 Protobuf 数据。
    struct RpcFrame
    {
        FrameType type{FrameType::kRequest};
        std::uint64_t request_id{};
        std::uint32_t status_code{};
        // 仅请求帧使用：客户端仍愿意等待的相对时长。0 表示使用服务端默认上限。
        std::uint32_t timeout_ms{};
        std::string service;
        std::string method;
        std::string payload;
    };

    class FrameCodec
    {
    public:
        static constexpr std::uint32_t kMagic = 0x4d525043; // MRPC
        static constexpr std::uint16_t kVersion = 2;
        static constexpr std::size_t kFixedHeaderSize = 36;
        static constexpr std::size_t kMaxHeaderBytes = 8 * 1024;
        static constexpr std::size_t kMaxPayloadBytes = 16 * 1024 * 1024;

        // 编码为网络字节序。该函数不负责发送网络数据。
        static Status Encode(const RpcFrame &frame, std::string *output);
        // 只在 input 包含一帧完整数据时设置 consumed；consumed 为 0 表示半包。
        static Status DecodeOne(std::string_view input, RpcFrame *frame, std::size_t *consumed);
        // 当固定头中的 magic/version 已经可信且 request_id 已完整到达时，供服务端在协议错误场景返回带关联 id 的错误响应。
        static bool TryGetRequestId(std::string_view input, std::uint64_t *request_id);
    };

    // 客户端接收端的累积解析器：跨多次 recv 保存不完整数据，并输出所有完整帧。
    class FrameParser
    {
    public:
        Status Append(std::string_view bytes, std::vector<RpcFrame> *frames);
        [[nodiscard]] std::size_t buffered_bytes() const { return buffer_.size(); }

    private:
        std::string buffer_;
    };

} // namespace myrpc
