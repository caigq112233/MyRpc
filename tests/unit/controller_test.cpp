#include <cassert>
#include <chrono>
#include <thread>

#include "myrpc/controller.hpp"

namespace {

class CancelClosure final : public google::protobuf::Closure {
public:
    void Run() override { ++runs; }
    int runs{0};
};

}  // namespace

int main() {
    myrpc::RpcController controller(5);
    assert(!controller.Failed());
    assert(!controller.IsCanceled());

    controller.SetStatus({myrpc::StatusCode::kUnavailable, "provider unavailable"});
    assert(controller.Failed());
    assert(controller.status().code() == myrpc::StatusCode::kUnavailable);
    assert(controller.ErrorText() == "provider unavailable");

    controller.Reset();
    assert(!controller.Failed());
    assert(!controller.Expired());

    CancelClosure callback;
    controller.NotifyOnCancel(&callback);
    controller.StartCancel();
    assert(controller.IsCanceled());
    assert(callback.runs == 1);

    controller.Reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(controller.Expired());

    myrpc::RpcController unlimited(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(!unlimited.HasDeadline());
    assert(!unlimited.Expired());
    assert(unlimited.RemainingTimeoutMs() == 0);
}
