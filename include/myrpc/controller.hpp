#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

#include <google/protobuf/service.h>

#include "myrpc/status.hpp"

namespace myrpc {

class RpcController final : public google::protobuf::RpcController {
public:
    explicit RpcController(std::chrono::milliseconds timeout = std::chrono::seconds(3));

    void Reset() override;
    bool Failed() const override;
    std::string ErrorText() const override;
    void StartCancel() override;
    bool IsCanceled() const override;
    void NotifyOnCancel(google::protobuf::Closure* callback) override;
    void SetFailed(const std::string& reason) override;

    // 供 Channel 或业务层检查调用是否已超过 deadline。
    [[nodiscard]] bool Expired() const;
    [[nodiscard]] Status status() const;
    void SetStatus(Status status);

private:
    mutable std::mutex mutex_;
    Status status_;
    std::chrono::steady_clock::time_point deadline_;
    std::atomic_bool canceled_{false};
    google::protobuf::Closure* on_cancel_{nullptr};  // Owned by protobuf caller.
};

}  // namespace myrpc
