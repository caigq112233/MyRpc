#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>

#include "myrpc/channel.hpp"
#include "myrpc/controller.hpp"
#include "myrpc/zookeeper_registry.hpp"
#include "user.pb.h"

struct CallState
{
    std::mutex mutex;
    std::condition_variable completed;
    bool finished{false};

    // 这些对象必须活到异步 RPC 回调结束。
    std::shared_ptr<myrpc::RpcController> controller;
    std::shared_ptr<example::EchoRequest> request;
    std::shared_ptr<example::EchoResponse> response;
};

class EchoDone final : public google::protobuf::Closure
{
public:
    explicit EchoDone(std::shared_ptr<CallState> state)
        : state_(std::move(state)) {}

    void Run() override
    {
        if (state_->controller->Failed())
        {
            std::cerr << "RPC failed: "
                      << state_->controller->ErrorText() << '\n';
        }
        else
        {
            std::cout << "RPC response: "
                      << state_->response->message() << '\n';
        }

        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->finished = true;
        }
        state_->completed.notify_one();

        delete this;
    }

private:
    std::shared_ptr<CallState> state_;
};

int main()
{
    auto registry =
        std::make_shared<myrpc::ZookeeperRegistry>("127.0.0.1:2181");

    myrpc::RpcChannel channel(registry);
    example::EchoService_Stub stub(&channel);

    auto state = std::make_shared<CallState>();
    state->controller = std::make_shared<myrpc::RpcController>(3000);
    state->request = std::make_shared<example::EchoRequest>();
    state->response = std::make_shared<example::EchoResponse>();

    state->request->set_message("async hello");

    // done 非空：调用立即返回；真正响应由接收线程按 request_id 匹配。
    stub.Echo(
        state->controller.get(),
        state->request.get(),
        state->response.get(),
        new EchoDone(state));

    // 示例程序不能立即退出，否则 channel 和异步调用会被销毁。
    std::unique_lock<std::mutex> lock(state->mutex);
    state->completed.wait(lock, [&state]
                          { return state->finished; });

    return 0;
}
