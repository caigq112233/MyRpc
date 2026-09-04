#include "myrpc/frame.hpp"

#include <limits>

namespace myrpc {
namespace {

void AppendU16(std::string* output, std::uint16_t value) {
    // 协议规定所有多字节整数使用大端序，不能直接 memcpy 本机整数。
    output->push_back(static_cast<char>((value >> 8) & 0xff));
    output->push_back(static_cast<char>(value & 0xff));
}

void AppendU32(std::string* output, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        output->push_back(static_cast<char>((value >> shift) & 0xff));
    }
}

void AppendU64(std::string* output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output->push_back(static_cast<char>((value >> shift) & 0xff));
    }
}

std::uint16_t ReadU16(std::string_view bytes, std::size_t pos) {
    return (static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[pos])) << 8) |
           static_cast<unsigned char>(bytes[pos + 1]);
}

std::uint32_t ReadU32(std::string_view bytes, std::size_t pos) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value = (value << 8) | static_cast<unsigned char>(bytes[pos + i]);
    }
    return value;
}

std::uint64_t ReadU64(std::string_view bytes, std::size_t pos) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<unsigned char>(bytes[pos + i]);
    }
    return value;
}

Status ValidateLengths(const RpcFrame& frame) {
    if (frame.service.size() + frame.method.size() > FrameCodec::kMaxHeaderBytes) {
        return {StatusCode::kInvalidArgument, "RPC metadata exceeds maximum size"};
    }
    if (frame.payload.size() > FrameCodec::kMaxPayloadBytes) {
        return {StatusCode::kInvalidArgument, "RPC payload exceeds maximum size"};
    }
    if (frame.type != FrameType::kRequest && frame.type != FrameType::kResponse) {
        return {StatusCode::kInvalidArgument, "unknown RPC frame type"};
    }
    return Status::Ok();
}

}  // namespace

Status FrameCodec::Encode(const RpcFrame& frame, std::string* output) {
    if (output == nullptr) return {StatusCode::kInvalidArgument, "output is null"};
    Status status = ValidateLengths(frame);
    if (!status.ok()) return status;

    // 固定头后按 service、method、payload 的顺序拼接可变长字段。
    output->clear();
    output->reserve(kFixedHeaderSize + frame.service.size() + frame.method.size() +
                    frame.payload.size());
    AppendU32(output, kMagic);
    AppendU16(output, kVersion);
    AppendU16(output, static_cast<std::uint16_t>(frame.type));
    AppendU64(output, frame.request_id);
    AppendU32(output, frame.status_code);
    AppendU32(output, static_cast<std::uint32_t>(frame.service.size()));
    AppendU32(output, static_cast<std::uint32_t>(frame.method.size()));
    AppendU32(output, static_cast<std::uint32_t>(frame.payload.size()));
    output->append(frame.service);
    output->append(frame.method);
    output->append(frame.payload);
    return Status::Ok();
}

Status FrameCodec::DecodeOne(std::string_view input, RpcFrame* frame, std::size_t* consumed) {
    if (frame == nullptr || consumed == nullptr) {
        return {StatusCode::kInvalidArgument, "frame or consumed is null"};
    }
    *consumed = 0;
    if (input.size() < kFixedHeaderSize) return Status::Ok();

    if (ReadU32(input, 0) != kMagic || ReadU16(input, 4) != kVersion) {
        return {StatusCode::kProtocolError, "invalid RPC magic or version"};
    }
    const auto raw_type = ReadU16(input, 6);
    if (raw_type != static_cast<std::uint16_t>(FrameType::kRequest) &&
        raw_type != static_cast<std::uint16_t>(FrameType::kResponse)) {
        return {StatusCode::kProtocolError, "invalid RPC frame type"};
    }

    const std::uint32_t service_size = ReadU32(input, 20);
    const std::uint32_t method_size = ReadU32(input, 24);
    const std::uint32_t payload_size = ReadU32(input, 28);
    if (static_cast<std::size_t>(service_size) + static_cast<std::size_t>(method_size) >
            kMaxHeaderBytes ||
        payload_size > kMaxPayloadBytes) {
        return {StatusCode::kProtocolError, "RPC frame length exceeds limit"};
    }
    const std::size_t total_size = kFixedHeaderSize + service_size + method_size + payload_size;
    // TCP 不保证一次读取一帧。数据不足时不报错，交由上层保留字节等待下次读取。
    if (total_size < kFixedHeaderSize || input.size() < total_size) return Status::Ok();

    frame->type = static_cast<FrameType>(raw_type);
    frame->request_id = ReadU64(input, 8);
    frame->status_code = ReadU32(input, 16);
    std::size_t cursor = kFixedHeaderSize;
    frame->service.assign(input.data() + cursor, service_size);
    cursor += service_size;
    frame->method.assign(input.data() + cursor, method_size);
    cursor += method_size;
    frame->payload.assign(input.data() + cursor, payload_size);
    *consumed = total_size;
    return Status::Ok();
}

Status FrameParser::Append(std::string_view bytes, std::vector<RpcFrame>* frames) {
    if (frames == nullptr) return {StatusCode::kInvalidArgument, "frames is null"};
    if (buffer_.size() + bytes.size() > FrameCodec::kFixedHeaderSize +
                                       FrameCodec::kMaxHeaderBytes +
                                       FrameCodec::kMaxPayloadBytes) {
        return {StatusCode::kProtocolError, "incomplete RPC frame exceeds limit"};
    }
    buffer_.append(bytes.data(), bytes.size());
    // 一次 recv 可能包含多帧，持续解析直到剩余数据不足一帧。
    while (!buffer_.empty()) {
        RpcFrame frame;
        std::size_t consumed = 0;
        Status status = FrameCodec::DecodeOne(buffer_, &frame, &consumed);
        if (!status.ok()) return status;
        if (consumed == 0) break;
        frames->push_back(std::move(frame));
        buffer_.erase(0, consumed);
    }
    return Status::Ok();
}

}  // namespace myrpc
