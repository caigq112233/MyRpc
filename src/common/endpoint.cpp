#include "myrpc/endpoint.hpp"

#include <charconv>

namespace myrpc {

std::string Endpoint::ToString() const {
    return host + ":" + std::to_string(port);
}

Status Endpoint::Parse(const std::string& value, Endpoint* endpoint) {
    const auto separator = value.rfind(':');
    if (endpoint == nullptr || separator == std::string::npos || separator == 0 ||
        separator == value.size() - 1) {
        return {StatusCode::kInvalidArgument, "endpoint must be host:port"};
    }
    unsigned int port = 0;
    const char* first = value.data() + separator + 1;
    const char* last = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(first, last, port);
    if (ec != std::errc{} || ptr != last || port == 0 || port > 65535) {
        return {StatusCode::kInvalidArgument, "endpoint port is invalid"};
    }
    endpoint->host = value.substr(0, separator);
    endpoint->port = static_cast<std::uint16_t>(port);
    return Status::Ok();
}

}  // namespace myrpc
