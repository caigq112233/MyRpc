#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpConnection.h>

#include "myrpc/controller.hpp"
#include "myrpc/endpoint.hpp"
#include "myrpc/frame.hpp"
#include "myrpc/registry.hpp"

namespace muduo::net {
class Buffer;
class TcpServer;
}

namespace myrpc {

class RpcProvider {
public:
    RpcProvider(Endpoint endpoint, ServiceRegistryPtr registry, int worker_threads = 4);
    ~RpcProvider();

    void NotifyService(google::protobuf::Service* service);
    // 注册全部方法、启动 Muduo 服务端，并进入事件循环。
    Status Run();

private:
    class CompletionClosure;

    struct ServiceInfo {
        // Service 的所有权属于调用方；调用方必须保证其生命周期覆盖 Run()。
        google::protobuf::Service* service{};
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor*> methods;
    };

    void OnConnection(const muduo::net::TcpConnectionPtr& connection);
    void OnMessage(const muduo::net::TcpConnectionPtr& connection,
                   muduo::net::Buffer* buffer, muduo::Timestamp time);
    void Dispatch(const muduo::net::TcpConnectionPtr& connection, RpcFrame frame);
    void SendRpcResponse(const muduo::net::TcpConnectionPtr& connection,
                         std::uint64_t request_id, google::protobuf::Message* request,
                         google::protobuf::Message* response, RpcController* controller);
    void SendError(const muduo::net::TcpConnectionPtr& connection, std::uint64_t request_id,
                   StatusCode code, const std::string& message);

    Endpoint endpoint_;
    ServiceRegistryPtr registry_;
    int worker_threads_;
    muduo::net::EventLoop event_loop_;
    std::unique_ptr<muduo::net::TcpServer> server_;
    std::unordered_map<std::string, ServiceInfo> services_;
};

}  // namespace myrpc
