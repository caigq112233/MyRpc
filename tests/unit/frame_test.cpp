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

    std::string wire;
    assert(myrpc::FrameCodec::Encode(request, &wire).ok());

    myrpc::FrameParser parser;
    std::vector<myrpc::RpcFrame> frames;
    assert(parser.Append(std::string_view(wire.data(), 7), &frames).ok());
    assert(frames.empty());
    assert(parser.Append(std::string_view(wire.data() + 7, wire.size() - 7), &frames).ok());
    assert(frames.size() == 1);
    assert(frames[0].request_id == 42);
    assert(frames[0].payload == request.payload);
}
