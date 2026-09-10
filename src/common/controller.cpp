#include "myrpc/controller.hpp"

#include <algorithm>
#include <limits>

namespace myrpc
{

    RpcController::RpcController(int timeout_ms)
    {
        timeout_ms_ = std::max(0, timeout_ms);
        deadline_ = timeout_ms_ == 0
                        ? std::chrono::steady_clock::time_point::max()
                        : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
    }

    void RpcController::Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = Status::Ok();
        deadline_ = timeout_ms_ == 0
                        ? std::chrono::steady_clock::time_point::max()
                        : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
        canceled_.store(false);
        on_cancel_ = nullptr;
    }

    bool RpcController::Failed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return !status_.ok();
    }

    std::string RpcController::ErrorText() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_.message();
    }

    void RpcController::StartCancel()
    {
        canceled_.store(true);
        google::protobuf::Closure *callback = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = on_cancel_;
        }
        if (callback != nullptr)
            callback->Run();
    }

    bool RpcController::IsCanceled() const { return canceled_.load(); }

    void RpcController::NotifyOnCancel(google::protobuf::Closure *callback)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        on_cancel_ = callback;
    }

    void RpcController::SetFailed(const std::string &reason)
    {
        SetStatus({StatusCode::kInternal, reason});
    }

    bool RpcController::Expired() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return deadline_ != std::chrono::steady_clock::time_point::max() &&
               std::chrono::steady_clock::now() >= deadline_;
    }

    bool RpcController::HasDeadline() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return deadline_ != std::chrono::steady_clock::time_point::max();
    }

    std::chrono::steady_clock::time_point RpcController::deadline() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return deadline_;
    }

    std::uint32_t RpcController::RemainingTimeoutMs() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (deadline_ == std::chrono::steady_clock::time_point::max())
            return 0;
        const auto remaining = deadline_ - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero())
            return 0;
        // 统一向下取整：还剩不足 1 ms 时传 1，而不是错误地传成“无限等待”。
        const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
        const auto count = std::max<std::int64_t>(1, milliseconds);
        return static_cast<std::uint32_t>(std::min<std::int64_t>(
                count,
                std::numeric_limits<std::uint32_t>::max()));
    }

    Status RpcController::status() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }

    void RpcController::SetStatus(Status status)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = std::move(status);
    }

} // namespace myrpc
