#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "myrpc/status.hpp"

namespace myrpc {

enum class FrameType : std::uint16_t { kRequest = 1, kResponse = 2 };

// 一次 RPC 在 TCP 字节流中的逻辑帧。payload 保存序列化后的 Protobuf 数据。
struct RpcFrame {
    FrameType type{FrameType::kRequest};
    std::uint64_t request_id{};
    std::uint32_t status_code{};
    std::string service;
    std::string method;
    std::string payload;
};

class FrameCodec {
public:
    static constexpr std::uint32_t kMagic = 0x4d525043;  // MRPC
    static constexpr std::uint16_t kVersion = 1;
    static constexpr std::size_t kFixedHeaderSize = 32;
    static constexpr std::size_t kMaxHeaderBytes = 8 * 1024;
    static constexpr std::size_t kMaxPayloadBytes = 16 * 1024 * 1024;

    // 编码为网络字节序。该函数不负责发送网络数据。
    static Status Encode(const RpcFrame& frame, std::string* output);
    // 只在 input 包含一帧完整数据时设置 consumed；consumed 为 0 表示半包。
    static Status DecodeOne(std::string_view input, RpcFrame* frame, std::size_t* consumed);
};

// 客户端接收端的累积解析器：跨多次 recv 保存不完整数据，并输出所有完整帧。
class FrameParser {
public:
    Status Append(std::string_view bytes, std::vector<RpcFrame>* frames);
    [[nodiscard]] std::size_t buffered_bytes() const { return buffer_.size(); }

private:
    std::string buffer_;
};

}  // namespace myrpc
