#include <cassert>
#include <string>
#include <vector>

#include "myrpc/frame.hpp"

int main() {
    myrpc::RpcFrame request;
    request.type = myrpc::FrameType::kRequest;
    request.request_id = 42;
    request.service = "UserService";
    request.method = "Login";
    request.payload = std::string("a\\0b", 3);
    request.timeout_ms = 1500;

    std::string wire;
    assert(myrpc::FrameCodec::Encode(request, &wire).ok());

    myrpc::FrameParser parser;
    std::vector<myrpc::RpcFrame> frames;
    assert(parser.Append(std::string_view(wire.data(), 7), &frames).ok());
    assert(frames.empty());
    assert(parser.Append(std::string_view(wire.data() + 7, wire.size() - 7), &frames).ok());
    assert(frames.size() == 1);
    assert(frames[0].request_id == 42);
    assert(frames[0].timeout_ms == 1500);
    assert(frames[0].payload == request.payload);

    // 一次读取包含两帧时，解析器必须完整拆出两条逻辑消息。
    myrpc::FrameParser sticky_parser;
    std::vector<myrpc::RpcFrame> sticky_frames;
    const std::string sticky_wire = wire + wire;
    assert(sticky_parser.Append(sticky_wire, &sticky_frames).ok());
    assert(sticky_frames.size() == 2);

    // 请求帧不允许携带响应状态码。
    request.status_code = 1;
    assert(!myrpc::FrameCodec::Encode(request, &wire).ok());

    myrpc::RpcFrame cancel;
    cancel.type = myrpc::FrameType::kCancel;
    cancel.request_id = 42;
    assert(myrpc::FrameCodec::Encode(cancel, &wire).ok());
    myrpc::RpcFrame decoded_cancel;
    std::size_t consumed = 0;
    assert(myrpc::FrameCodec::DecodeOne(wire, &decoded_cancel, &consumed).ok());
    assert(consumed == wire.size());
    assert(decoded_cancel.type == myrpc::FrameType::kCancel);
}
