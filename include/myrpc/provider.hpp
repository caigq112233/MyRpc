#pragma once

#include <cstdint>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <cstddef>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpConnection.h>

#include "myrpc/controller.hpp"
#include "myrpc/endpoint.hpp"
#include "myrpc/frame.hpp"
#include "myrpc/registry.hpp"
#include "myrpc/thread_pool.hpp"

namespace muduo::net
{
    class Buffer;
    class TcpServer;
}

namespace myrpc
{

    class RpcProvider
    {
    public:
        RpcProvider(Endpoint endpoint, ServiceRegistryPtr registry, int worker_threads = 4,
                    int timeout_ms = 3000);
        ~RpcProvider();

        void NotifyService(google::protobuf::Service *service);
        void SetMethodTimeout(const std::string &service, const std::string &method,
                              int timeout);
        // 注册全部方法、启动 Muduo 服务端，并进入事件循环。
        Status Run();

    private:
        class CompletionClosure;
        struct RequestContext;
        struct ConnectionCalls;

        struct ServiceInfo
        {
            // Service 的所有权属于调用方；调用方必须保证其生命周期覆盖 Run()。
            google::protobuf::Service *service{};
            std::unordered_map<std::string, const google::protobuf::MethodDescriptor *> methods;
        };

        void OnConnection(const muduo::net::TcpConnectionPtr &connection);
        void OnMessage(const muduo::net::TcpConnectionPtr &connection,
                       muduo::net::Buffer *buffer, muduo::Timestamp time);
        void Dispatch(const muduo::net::TcpConnectionPtr &connection, RpcFrame frame);
        void OnDeadline(const std::shared_ptr<RequestContext> &context);
        void OnCancel(const muduo::net::TcpConnectionPtr &connection, std::uint64_t request_id);
        void CompleteFromHandler(const std::shared_ptr<RequestContext> &context);
        void RemovePending(const std::shared_ptr<RequestContext> &context);
        ConnectionCalls *CallsFor(const muduo::net::TcpConnectionPtr &connection) const;
        void OnHighWater(const muduo::net::TcpConnectionPtr &connection, std::size_t bytes);
        void OnWriteComplete(const muduo::net::TcpConnectionPtr &connection);
        void SendRpcResponse(const muduo::net::TcpConnectionPtr &connection,
                             std::uint64_t request_id, google::protobuf::Message *request,
                             google::protobuf::Message *response, RpcController *controller);
        void SendError(const muduo::net::TcpConnectionPtr &connection, std::uint64_t request_id,
                       StatusCode code, const std::string &message);
        Status RegisterServices();

        Endpoint endpoint_;
        ServiceRegistryPtr registry_;
        int worker_threads_;
        std::chrono::milliseconds request_timeout_{std::chrono::seconds(3)};
        muduo::net::EventLoop event_loop_;
        std::unique_ptr<muduo::net::TcpServer> server_;
        std::unique_ptr<ThreadPool> worker_pool_;
        std::unordered_map<std::string, ServiceInfo> services_;
        std::unordered_map<std::string, std::chrono::milliseconds> method_timeouts_;
        static constexpr std::size_t kMaxWorkerTasks = 1024;
    };

} // namespace myrpc
