#include "myrpc/controller.hpp"

namespace myrpc {

RpcController::RpcController(std::chrono::milliseconds timeout) {
    deadline_ = std::chrono::steady_clock::now() + timeout;
}

void RpcController::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = Status::Ok();
    canceled_.store(false);
    on_cancel_ = nullptr;
}

bool RpcController::Failed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !status_.ok();
}

std::string RpcController::ErrorText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.message();
}

void RpcController::StartCancel() {
    canceled_.store(true);
    google::protobuf::Closure* callback = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = on_cancel_;
    }
    if (callback != nullptr) callback->Run();
}

bool RpcController::IsCanceled() const { return canceled_.load(); }

void RpcController::NotifyOnCancel(google::protobuf::Closure* callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_cancel_ = callback;
}

void RpcController::SetFailed(const std::string& reason) {
    SetStatus({StatusCode::kInternal, reason});
}

bool RpcController::Expired() const {
    return std::chrono::steady_clock::now() >= deadline_;
}

Status RpcController::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void RpcController::SetStatus(Status status) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = std::move(status);
}

}  // namespace myrpc
