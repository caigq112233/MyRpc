#pragma once

#include <string>
#include <utility>

namespace myrpc {

enum class StatusCode {
    kOk = 0,
    kInvalidArgument,
    kUnavailable,
    kDeadlineExceeded,
    kProtocolError,
    kInternal,
};

// 框架内部统一使用的错误返回值，避免直接抛出异常穿透 RPC 网络边界。
class Status {
public:
    Status() = default;
    Status(StatusCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    [[nodiscard]] bool ok() const { return code_ == StatusCode::kOk; }
    [[nodiscard]] StatusCode code() const { return code_; }
    [[nodiscard]] const std::string& message() const { return message_; }

    static Status Ok() { return {}; }

private:
    StatusCode code_{StatusCode::kOk};
    std::string message_;
};

}  // namespace myrpc
