#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "myrpc/endpoint.hpp"
#include "myrpc/provider.hpp"
#include "myrpc/zookeeper_registry.hpp"
#include "user.pb.h"

class EchoServiceImpl final : public example::EchoService
{
public:
    void Echo(google::protobuf::RpcController *controller,
              const example::EchoRequest *request,
              example::EchoResponse *response,
              google::protobuf::Closure *done) override
    {
        // 模拟约 1 秒耗时业务，每 100ms 检查一次取消请求。
        for (int i = 0; i < 10; ++i)
        {
            if (controller->IsCanceled())
            {
                // 即使已超时或取消，也必须调用 done，供框架回收 Closure。
                done->Run();
                return;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (controller->IsCanceled())
        {
            done->Run();
            return;
        }

        response->set_message("echo: " + request->message());

        // 无论成功还是失败，每次 RPC 都必须恰好调用一次。
        done->Run();
    }
};

int main()
{
    auto registry =
        std::make_shared<myrpc::ZookeeperRegistry>("127.0.0.1:2181");

    myrpc::Endpoint endpoint{"127.0.0.1", 8000};

    // 4 个 I/O 工作线程；单请求服务端最长执行时间默认 3 秒。
    myrpc::RpcProvider provider(
        endpoint,
        registry,
        4,
        3000);

    // Echo 的服务端执行上限设为 2 秒。
    // 本示例业务约 1 秒，所以正常情况下会成功。
    provider.SetMethodTimeout(
        "EchoService",
        "Echo",
        2000);

    EchoServiceImpl service;
    provider.NotifyService(&service);

    const myrpc::Status status = provider.Run();
    if (!status.ok())
    {
        std::cerr << "server failed: " << status.message() << '\n';
        return 1;
    }

    return 0;
}
