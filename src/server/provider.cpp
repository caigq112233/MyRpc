#include "myrpc/provider.hpp"

#include <memory>

#include <muduo/net/Buffer.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpServer.h>
#include <google/protobuf/message.h>

namespace myrpc {

// 部分旧版 Protobuf 的 NewCallback 最多支持两个绑定参数。
// 自定义 Closure 保存一次异步调用所需的全部上下文，并在 Run 后自删除。
class RpcProvider::CompletionClosure final : public google::protobuf::Closure {
public:
    CompletionClosure(RpcProvider* provider, muduo::net::TcpConnectionPtr connection,
                      std::uint64_t request_id, google::protobuf::Message* request,
                      google::protobuf::Message* response, RpcController* controller)
        : provider_(provider),
          connection_(std::move(connection)),
          request_id_(request_id),
          request_(request),
          response_(response),
          controller_(controller) {}

    void Run() override {
        provider_->SendRpcResponse(connection_, request_id_, request_, response_, controller_);
        delete this;
    }

private:
    RpcProvider* provider_;
    muduo::net::TcpConnectionPtr connection_;
    std::uint64_t request_id_;
    google::protobuf::Message* request_;
    google::protobuf::Message* response_;
    RpcController* controller_;
};

RpcProvider::RpcProvider(Endpoint endpoint, ServiceRegistryPtr registry, int worker_threads)
    : endpoint_(std::move(endpoint)),
      registry_(std::move(registry)),
      worker_threads_(worker_threads) {}

RpcProvider::~RpcProvider() = default;

void RpcProvider::NotifyService(google::protobuf::Service* service) {
    if (service == nullptr) return;
    const auto* descriptor = service->GetDescriptor();
    ServiceInfo info;
    info.service = service;
    // 提前建立 service_name -> method_name -> MethodDescriptor 的查找表，
    // 网络请求到达时无需遍历所有 Service。
    for (int index = 0; index < descriptor->method_count(); ++index) {
        const auto* method = descriptor->method(index);
        info.methods.emplace(method->name(), method);
    }
    services_[descriptor->name()] = std::move(info);
}

Status RpcProvider::Run() {
    if (!endpoint_.valid() || registry_ == nullptr) {
        return {StatusCode::kInvalidArgument, "provider endpoint or registry is invalid"};
    }
    // 先注册再监听端口；客户端在注册完成后才能发现当前 Provider。
    for (const auto& [service_name, info] : services_) {
        for (const auto& [method_name, ignored] : info.methods) {
            (void)ignored;
            Status status = registry_->Register(service_name, method_name, endpoint_);
            if (!status.ok()) return status;
        }
    }

    muduo::net::InetAddress address(endpoint_.host, endpoint_.port);
    server_ = std::make_unique<muduo::net::TcpServer>(&event_loop_, address, "MyRpcProvider");
    server_->setThreadNum(worker_threads_);
    server_->setConnectionCallback(
        [this](const muduo::net::TcpConnectionPtr& connection) { OnConnection(connection); });
    server_->setMessageCallback([this](const muduo::net::TcpConnectionPtr& connection,
                                       muduo::net::Buffer* buffer, muduo::Timestamp time) {
        OnMessage(connection, buffer, time);
    });
    server_->start();
    event_loop_.loop();
    return Status::Ok();
}

void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr& connection) {
    if (!connection->connected()) connection->shutdown();
}

void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr& connection,
                            muduo::net::Buffer* buffer, muduo::Timestamp) {
    while (buffer->readableBytes() > 0) {
        RpcFrame frame;
        std::size_t consumed = 0;
        Status status = FrameCodec::DecodeOne(
            std::string_view(buffer->peek(), buffer->readableBytes()), &frame, &consumed);
        if (!status.ok()) {
            connection->shutdown();
            return;
        }
        // 半包保留在 Muduo Buffer，下一次读事件到来后继续解析。
        if (consumed == 0) return;
        buffer->retrieve(consumed);
        Dispatch(connection, std::move(frame));
    }
}

void RpcProvider::Dispatch(const muduo::net::TcpConnectionPtr& connection, RpcFrame frame) {
    if (frame.type != FrameType::kRequest) {
        SendError(connection, frame.request_id, StatusCode::kProtocolError, "expected request frame");
        return;
    }
    // 根据协议中的 service/method 名称定位业务对象。
    const auto service_it = services_.find(frame.service);
    if (service_it == services_.end()) {
        SendError(connection, frame.request_id, StatusCode::kUnavailable, "service not found");
        return;
    }
    const auto method_it = service_it->second.methods.find(frame.method);
    if (method_it == service_it->second.methods.end()) {
        SendError(connection, frame.request_id, StatusCode::kUnavailable, "method not found");
        return;
    }

    google::protobuf::Service* service = service_it->second.service;
    const auto* method = method_it->second;
    auto* request = service->GetRequestPrototype(method).New();
    if (!request->ParseFromString(frame.payload)) {
        delete request;
        SendError(connection, frame.request_id, StatusCode::kInvalidArgument, "invalid protobuf request");
        return;
    }
    auto* response = service->GetResponsePrototype(method).New();
    auto* controller = new RpcController();
    // 业务方法可异步执行；业务层在完成时调用 done->Run() 才会发送响应。
    google::protobuf::Closure* done =
        new CompletionClosure(this, connection, frame.request_id, request, response, controller);
    service->CallMethod(method, controller, request, response, done);
}

void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr& connection,
                                  std::uint64_t request_id, google::protobuf::Message* request,
                                  google::protobuf::Message* response, RpcController* controller) {
    // 请求、响应和 Controller 由框架创建，必须在 done 回调完成时统一释放。
    std::unique_ptr<google::protobuf::Message> request_owner(request);
    std::unique_ptr<google::protobuf::Message> response_owner(response);
    std::unique_ptr<RpcController> controller_owner(controller);
    if (controller->Failed()) {
        SendError(connection, request_id, controller->status().code(), controller->ErrorText());
        return;
    }
    RpcFrame frame;
    frame.type = FrameType::kResponse;
    frame.request_id = request_id;
    if (!response->SerializeToString(&frame.payload)) {
        SendError(connection, request_id, StatusCode::kInternal, "cannot serialize protobuf response");
        return;
    }
    std::string wire;
    if (!FrameCodec::Encode(frame, &wire).ok()) {
        connection->shutdown();
        return;
    }
    connection->send(wire);
}

void RpcProvider::SendError(const muduo::net::TcpConnectionPtr& connection,
                            std::uint64_t request_id, StatusCode code,
                            const std::string& message) {
    RpcFrame frame;
    frame.type = FrameType::kResponse;
    frame.request_id = request_id;
    frame.status_code = static_cast<std::uint32_t>(code);
    frame.payload = message;
    std::string wire;
    if (FrameCodec::Encode(frame, &wire).ok()) connection->send(wire);
}

}  // namespace myrpc
