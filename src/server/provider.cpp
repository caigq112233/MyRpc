#include "myrpc/provider.hpp"

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>

#include <muduo/net/Buffer.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/TimerId.h>
#include <google/protobuf/message.h>
#include <boost/any.hpp>

namespace myrpc
{

    // deadline 只结束 RPC 的网络语义并请求业务取消；Context 不会被强制释放。
    // 它由 pending 表、业务线程及 CompletionClosure 共同以 shared_ptr 持有，直到
    // 业务调用 done 或退出前不再持有 Context，才由 RAII 自动析构。
    struct RpcProvider::RequestContext
    {
        muduo::net::TcpConnectionPtr connection;
        std::uint64_t request_id{};
        std::unique_ptr<google::protobuf::Message> request;
        std::unique_ptr<google::protobuf::Message> response;
        std::unique_ptr<RpcController> controller;
        std::atomic_bool response_sent{false};
        std::atomic_bool cancel_requested{false};
        std::atomic_bool handler_finished{false};
        std::chrono::steady_clock::time_point deadline;
        muduo::net::TimerId deadline_timer;
        bool timer_armed{false};
    };

    struct RpcProvider::ConnectionCalls
    {
        std::unordered_map<std::uint64_t, std::shared_ptr<RequestContext>> pending;
        bool reading_paused{false};
    };

    // Closure 是业务完成信号；它持有 Context，确保 deadline 到达后业务线程仍可
    // 安全读取 request/response/controller 并通过 controller->IsCanceled() 协作退出。
    class RpcProvider::CompletionClosure final : public google::protobuf::Closure
    {
    public:
        CompletionClosure(RpcProvider *provider, std::shared_ptr<RequestContext> context)
            : provider_(provider),
              context_(std::move(context)) {}

        void Run() override
        {
            if (run_called_.exchange(true))
                return;
            auto context = std::move(context_);
            context->connection->getLoop()->queueInLoop(
                [provider = provider_, context]
                { provider->CompleteFromHandler(context); });
            delete this;
        }

    private:
        RpcProvider *provider_;
        std::shared_ptr<RequestContext> context_;
        std::atomic_bool run_called_{false};
    };

    RpcProvider::RpcProvider(Endpoint endpoint, ServiceRegistryPtr registry, int worker_threads,
                             int timeout_ms)
        : endpoint_(std::move(endpoint)),
          registry_(std::move(registry)),
          worker_threads_(worker_threads)
    {
        request_timeout_ = timeout_ms <= 0 ? std::chrono::milliseconds(3000) : std::chrono::milliseconds(timeout_ms);
    }

    RpcProvider::~RpcProvider() = default;

    void RpcProvider::NotifyService(google::protobuf::Service *service)
    {
        if (service == nullptr)
            return;
        const auto *descriptor = service->GetDescriptor();
        ServiceInfo info;
        info.service = service;
        // 提前建立 service_name -> method_name -> MethodDescriptor 的查找表，
        // 网络请求到达时无需遍历所有 Service。
        for (int index = 0; index < descriptor->method_count(); ++index)
        {
            const auto *method = descriptor->method(index);
            info.methods.emplace(method->name(), method);
        }
        services_[descriptor->name()] = std::move(info);
    }

    void RpcProvider::SetMethodTimeout(const std::string &service, const std::string &method,
                                       int timeout)
    {
        if (timeout <= 0)
            return;
        method_timeouts_[service + "\n" + method] = std::chrono::milliseconds(timeout);
    }

    Status RpcProvider::Run()
    {
        if (!endpoint_.valid() || registry_ == nullptr)
        {
            return {StatusCode::kInvalidArgument, "provider endpoint or registry is invalid"};
        }
        // 先注册再监听端口；客户端在注册完成后才能发现当前 Provider。
        Status registration_status = RegisterServices();
        if (!registration_status.ok())
            return registration_status;

        muduo::net::InetAddress address(endpoint_.host, endpoint_.port);
        server_ = std::make_unique<muduo::net::TcpServer>(&event_loop_, address, "MyRpcProvider");
        server_->setThreadNum(worker_threads_);
        server_->setConnectionCallback(
            [this](const muduo::net::TcpConnectionPtr &connection)
            { OnConnection(connection); });
        server_->setMessageCallback([this](const muduo::net::TcpConnectionPtr &connection,
                                           muduo::net::Buffer *buffer, muduo::Timestamp time)
                                    { OnMessage(connection, buffer, time); });
        worker_pool_ = std::make_unique<ThreadPool>(
            static_cast<std::size_t>(worker_threads_), kMaxWorkerTasks);
        // 临时 ZooKeeper 节点会在会话过期后消失。Registry 在检测到新会话时会利用
        // 已记录的注册项补注册；Provider 周期性触发该恢复动作，不会重复创建节点。
        event_loop_.runEvery(1.0, [this]
                             { (void)registry_->EnsureRegistered(); });
        server_->start();
        event_loop_.loop();
        return Status::Ok();
    }

    Status RpcProvider::RegisterServices()
    {
        for (const auto &[service_name, info] : services_)
        {
            for (const auto &[method_name, ignored] : info.methods)
            {
                (void)ignored;
                Status status = registry_->Register(service_name, method_name, endpoint_);
                if (!status.ok())
                    return status;
            }
        }
        return Status::Ok();
    }

    void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &connection)
    {
        if (connection->connected())
        {
            connection->setContext(std::make_shared<ConnectionCalls>());
            connection->setHighWaterMarkCallback(
                [this](const muduo::net::TcpConnectionPtr &conn, std::size_t bytes)
                {
                    OnHighWater(conn, bytes);
                },
                8 * 1024 * 1024);
            connection->setWriteCompleteCallback(
                [this](const muduo::net::TcpConnectionPtr &conn)
                { OnWriteComplete(conn); });
            return;
        }
        ConnectionCalls *calls = CallsFor(connection);
        if (calls == nullptr)
            return;
        for (const auto &[request_id, context] : calls->pending)
        {
            (void)request_id;
            context->cancel_requested.store(true);
            context->response_sent.store(true);
            context->controller->StartCancel();
            if (context->timer_armed)
                connection->getLoop()->cancel(context->deadline_timer);
        }
        calls->pending.clear();
    }

    void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &connection,
                                muduo::net::Buffer *buffer, muduo::Timestamp)
    {
        while (buffer->readableBytes() > 0)
        {
            RpcFrame frame;
            std::size_t consumed = 0;
            Status status = FrameCodec::DecodeOne(
                std::string_view(buffer->peek(), buffer->readableBytes()), &frame, &consumed);
            if (!status.ok())
            {
                std::uint64_t request_id = 0;
                const std::string_view input(buffer->peek(), buffer->readableBytes());
                // magic/version 已通过时，尽量给对端一个可关联的协议错误；无法信任
                // 固定头的数据（如 magic 错误）时直接断开以避免响应攻击流量。
                if (FrameCodec::TryGetRequestId(input, &request_id))
                {
                    SendError(connection, request_id, StatusCode::kProtocolError, status.message());
                }
                connection->shutdown();
                return;
            }
            // 半包保留在 Muduo Buffer，下一次读事件到来后继续解析。
            if (consumed == 0)
                return;
            buffer->retrieve(consumed);
            if (frame.type == FrameType::kCancel)
            {
                OnCancel(connection, frame.request_id);
            }
            else
            {
                Dispatch(connection, std::move(frame));
            }
        }
    }

    void RpcProvider::Dispatch(const muduo::net::TcpConnectionPtr &connection, RpcFrame frame)
    {
        if (frame.type != FrameType::kRequest)
        {
            SendError(connection, frame.request_id, StatusCode::kProtocolError, "expected request frame");
            return;
        }
        // 根据协议中的 service/method 名称定位业务对象。
        const auto service_it = services_.find(frame.service);
        if (service_it == services_.end())
        {
            SendError(connection, frame.request_id, StatusCode::kUnavailable, "service not found");
            return;
        }
        const auto method_it = service_it->second.methods.find(frame.method);
        if (method_it == service_it->second.methods.end())
        {
            SendError(connection, frame.request_id, StatusCode::kUnavailable, "method not found");
            return;
        }

        google::protobuf::Service *service = service_it->second.service;
        const auto *method = method_it->second;
        auto context = std::make_shared<RequestContext>();
        context->connection = connection;
        context->request_id = frame.request_id;
        context->request.reset(service->GetRequestPrototype(method).New());
        if (!context->request->ParseFromString(frame.payload))
        {
            SendError(connection, frame.request_id, StatusCode::kInvalidArgument, "invalid protobuf request");
            return;
        }
        context->response.reset(service->GetResponsePrototype(method).New());
        // 关于超时时间的设定，如果服务方法在发布时没有设定超时时间，则按照框架内置默认超时时间3s，否则按照发布时设定的超时时间
        auto max_timeout = request_timeout_;
        const auto timeout_it = method_timeouts_.find(frame.service + "\n" + frame.method);
        if (timeout_it != method_timeouts_.end())
        {
            max_timeout = timeout_it->second;
        }
        // 如果调用方没有设定超时时间，则按照服务方的超时时间，否则两者取最小
        const auto requested_timeout = frame.timeout_ms == 0
                                           ? max_timeout
                                           : std::chrono::milliseconds(frame.timeout_ms);
        const auto effective_timeout = std::min(requested_timeout, max_timeout);
        context->deadline = std::chrono::steady_clock::now() + effective_timeout;
        const auto effective_timeout_ms = std::min<std::int64_t>(
            effective_timeout.count(), std::numeric_limits<int>::max());
        context->controller = std::make_unique<RpcController>(
            static_cast<int>(effective_timeout_ms));
        ConnectionCalls *calls = CallsFor(connection);
        if (calls == nullptr)
        {
            SendError(connection, context->request_id, StatusCode::kInternal,
                      "connection call state is unavailable");
            return;
        }
        calls->pending[context->request_id] = context;
        context->deadline_timer = connection->getLoop()->runAfter(
            std::chrono::duration<double>(effective_timeout).count(),
            [this, context]
            { OnDeadline(context); });
        context->timer_armed = true;

        const bool queued = worker_pool_->TryRun([this, service, method, context]
                                                 {
            if (context->cancel_requested.load() || context->controller->Expired()) {
                context->handler_finished.store(true);
                return;
            }

            google::protobuf::Closure* done =
                new CompletionClosure(this, context);

            service->CallMethod(
                method,
                context->controller.get(),
                context->request.get(),
                context->response.get(),
                done); });
        if (!queued)
        {
            connection->getLoop()->cancel(context->deadline_timer);
            context->cancel_requested.store(true);
            context->controller->StartCancel();
            context->response_sent.store(true);
            RemovePending(context);

            SendError(connection, context->request_id, StatusCode::kUnavailable,
                      "provider worker queue is full");
        }
    }

    void RpcProvider::OnDeadline(const std::shared_ptr<RequestContext> &context)
    {
        if (context->response_sent.exchange(true))
            return;
        context->cancel_requested.store(true);
        context->controller->StartCancel();
        // 只结束网络 RPC，不释放 Context；仍在运行的业务可通过 IsCanceled() 退出。
        SendError(context->connection, context->request_id, StatusCode::kDeadlineExceeded,
                  "provider execution deadline exceeded");
        RemovePending(context);
    }

    void RpcProvider::OnCancel(const muduo::net::TcpConnectionPtr &connection,
                               std::uint64_t request_id)
    {
        ConnectionCalls *calls = CallsFor(connection);
        if (calls == nullptr)
            return;
        // 不需要给 calls->pending 单独加锁:同一条 connection 的这些操作都在它自己的 Muduo EventLoop 线程顺序执行
        // OnDeadline() 在该 connection 的 I/O loop 中执行。OnCancel() 是在该 connection 的 I/O loop 收到 kCancel 帧后直接调用的
        // 因此即使“超时”和“取消请求”几乎同时发生，也会排队串行化
        const auto call = calls->pending.find(request_id);
        if (call == calls->pending.end())
            return;
        const auto &context = call->second;
        if (context->response_sent.exchange(true))
            return;
        context->cancel_requested.store(true);
        context->controller->StartCancel();
        if (context->timer_armed)
            connection->getLoop()->cancel(context->deadline_timer);
        // 客户端已不再等待，因此不返回第二种结果帧。
        RemovePending(context);
    }

    void RpcProvider::CompleteFromHandler(const std::shared_ptr<RequestContext> &context)
    {
        context->handler_finished.store(true);
        if (context->timer_armed)
            context->connection->getLoop()->cancel(context->deadline_timer);
        if (!context->response_sent.exchange(true))
        {
            SendRpcResponse(context->connection, context->request_id, context->request.release(),
                            context->response.release(), context->controller.release());
        }
        RemovePending(context);
    }

    void RpcProvider::RemovePending(const std::shared_ptr<RequestContext> &context)
    {
        ConnectionCalls *calls = CallsFor(context->connection);
        if (calls != nullptr)
            calls->pending.erase(context->request_id);
    }

    RpcProvider::ConnectionCalls *RpcProvider::CallsFor(
        const muduo::net::TcpConnectionPtr &connection) const
    {
        const auto *calls = boost::any_cast<std::shared_ptr<ConnectionCalls>>(&connection->getContext());
        return calls == nullptr ? nullptr : calls->get();
    }

    void RpcProvider::OnHighWater(const muduo::net::TcpConnectionPtr &connection, std::size_t)
    {
        ConnectionCalls *calls = CallsFor(connection);
        if (calls == nullptr || calls->reading_paused)
            return;
        calls->reading_paused = true;
        // 等待写缓冲降到空后再继续从慢客户端读取，限制服务端内存增长。
        connection->stopRead();
    }

    void RpcProvider::OnWriteComplete(const muduo::net::TcpConnectionPtr &connection)
    {
        ConnectionCalls *calls = CallsFor(connection);
        if (calls == nullptr || !calls->reading_paused)
            return;
        calls->reading_paused = false;
        connection->startRead();
    }

    void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &connection,
                                      std::uint64_t request_id, google::protobuf::Message *request,
                                      google::protobuf::Message *response, RpcController *controller)
    {
        // 请求、响应和 Controller 由框架创建，必须在 done 回调完成时统一释放。
        std::unique_ptr<google::protobuf::Message> request_owner(request);
        std::unique_ptr<google::protobuf::Message> response_owner(response);
        std::unique_ptr<RpcController> controller_owner(controller);
        //
        if (controller->Failed())
        {
            SendError(connection, request_id, controller->status().code(), controller->ErrorText());
            return;
        }
        RpcFrame frame;
        frame.type = FrameType::kResponse;
        frame.request_id = request_id;
        if (!response->SerializeToString(&frame.payload))
        {
            SendError(connection, request_id, StatusCode::kInternal, "cannot serialize protobuf response");
            return;
        }
        std::string wire;
        if (!FrameCodec::Encode(frame, &wire).ok())
        {
            connection->shutdown();
            return;
        }
        connection->send(wire);
    }

    void RpcProvider::SendError(const muduo::net::TcpConnectionPtr &connection,
                                std::uint64_t request_id, StatusCode code,
                                const std::string &message)
    {
        RpcFrame frame;
        frame.type = FrameType::kResponse;
        frame.request_id = request_id;
        frame.status_code = static_cast<std::uint32_t>(code);
        frame.payload = message;
        std::string wire;
        if (FrameCodec::Encode(frame, &wire).ok())
            connection->send(wire);
    }

} // namespace myrpc
