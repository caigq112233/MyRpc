#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

#include <google/protobuf/service.h>

#include "myrpc/status.hpp"

namespace myrpc {

class RpcController final : public google::protobuf::RpcController {
public:
    // timeout_ms 是整个客户端调用的等待预算，涵盖服务发现、连接、发送和收包。
    // 0 表示客户端无限等待；此时请求帧也会携带 timeout_ms = 0，由服务端采用自身上限。
    explicit RpcController(int timeout_ms = 3000);

    void Reset() override;
    bool Failed() const override;
    std::string ErrorText() const override;
    void StartCancel() override;
    bool IsCanceled() const override;
    void NotifyOnCancel(google::protobuf::Closure* callback) override;
    void SetFailed(const std::string& reason) override;

    // 供 Channel 或业务层检查调用是否已超过 deadline。
    [[nodiscard]] bool Expired() const;
    [[nodiscard]] bool HasDeadline() const;
    [[nodiscard]] std::chrono::steady_clock::time_point deadline() const;
    // 返回当前剩余等待时间。0 仅表示“无限等待”；已超时应先通过 Expired() 判断。
    [[nodiscard]] std::uint32_t RemainingTimeoutMs() const;
    [[nodiscard]] Status status() const;
    void SetStatus(Status status);

private:
    mutable std::mutex mutex_;
    Status status_;
    int timeout_ms_;
    std::chrono::steady_clock::time_point deadline_;
    std::atomic_bool canceled_{false};
    google::protobuf::Closure* on_cancel_{nullptr};  // Owned by protobuf caller.
};

}  // namespace myrpc
