#pragma once

#include <cstdint>
#include <string>

#include "myrpc/status.hpp"

namespace myrpc {

struct Endpoint {
    std::string host;
    std::uint16_t port{};

    [[nodiscard]] std::string ToString() const;
    [[nodiscard]] bool valid() const { return !host.empty() && port != 0; }

    // 将“host:port”转换为 Endpoint，并检查端口范围。
    static Status Parse(const std::string& value, Endpoint* endpoint);
};

}  // namespace myrpc
