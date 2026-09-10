#include "myrpc/channel.hpp"

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <future>
#include <limits>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "myrpc/frame.hpp"
#include "myrpc/controller.hpp"

namespace myrpc
{
    namespace
    {

        class FileDescriptor
        {
        public:
            explicit FileDescriptor(int fd = -1) : fd_(fd) {}
            ~FileDescriptor()
            {
                if (fd_ >= 0)
                    ::close(fd_);
            }
            FileDescriptor(const FileDescriptor &) = delete;
            FileDescriptor &operator=(const FileDescriptor &) = delete;
            FileDescriptor(FileDescriptor &&other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
            FileDescriptor &operator=(FileDescriptor &&other) noexcept
            {
                if (this != &other)
                {
                    if (fd_ >= 0)
                        ::close(fd_);
                    fd_ = other.fd_;
                    other.fd_ = -1;
                }
                return *this;
            }
            [[nodiscard]] int get() const { return fd_; }
            [[nodiscard]] int release()
            {
                const int value = fd_;
                fd_ = -1;
                return value;
            }

        private:
            int fd_;
        };

        using TimePoint = std::chrono::steady_clock::time_point;

        bool IsInfiniteDeadline(TimePoint deadline) { return deadline == TimePoint::max(); }

        bool DeadlineExpired(TimePoint deadline)
        {
            return !IsInfiniteDeadline(deadline) && std::chrono::steady_clock::now() >= deadline;
        }

        int PollTimeoutMs(TimePoint deadline)
        {
            if (IsInfiniteDeadline(deadline))
                return -1;
            const auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining <= TimePoint::duration::zero())
                return 0;
            const auto milliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
            const auto count = std::max<std::int64_t>(1, milliseconds);
            return static_cast<int>(std::min<std::int64_t>(count, std::numeric_limits<int>::max()));
        }

        Status SetNonBlocking(int fd)
        {
            const int flags = ::fcntl(fd, F_GETFL, 0);
            if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
                return {StatusCode::kInternal, std::string("cannot configure nonblocking socket: ") + std::strerror(errno)};
            return Status::Ok();
        }

        Status Connect(const Endpoint &endpoint, TimePoint deadline, FileDescriptor *output)
        {
            // getaddrinfo 同时兼容 IPv4、IPv6 和域名，不依赖 inet_addr。
            if (DeadlineExpired(deadline))
                return {StatusCode::kDeadlineExceeded, "RPC deadline exceeded before connecting"};
            addrinfo hints{};
            // 兼容 IPv4、IPv6
            hints.ai_family = AF_UNSPEC;
            // TCP
            hints.ai_socktype = SOCK_STREAM;
            addrinfo *result = nullptr;
            const std::string port = std::to_string(endpoint.port);
            if (::getaddrinfo(endpoint.host.c_str(), port.c_str(), &hints, &result) != 0)
            {
                return {StatusCode::kUnavailable, "cannot resolve endpoint " + endpoint.ToString()};
            }
            if (DeadlineExpired(deadline))
            {
                ::freeaddrinfo(result);
                return {StatusCode::kDeadlineExceeded, "RPC deadline exceeded during endpoint resolution"};
            }
            Status final_status{StatusCode::kUnavailable, "cannot connect to " + endpoint.ToString()};
            for (addrinfo *candidate = result; candidate != nullptr; candidate = candidate->ai_next)
            {
                if (DeadlineExpired(deadline))
                {
                    final_status = {StatusCode::kDeadlineExceeded, "RPC deadline exceeded while connecting"};
                    break;
                }
                const int fd = ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
                if (fd < 0)
                    continue;
                FileDescriptor socket(fd);
                Status status = SetNonBlocking(fd);
                if (!status.ok())
                {
                    final_status = status;
                    continue;
                }
                if (::connect(fd, candidate->ai_addr, candidate->ai_addrlen) != 0)
                {
                    if (errno != EINPROGRESS)
                    {
                        final_status = {StatusCode::kUnavailable, std::string("connect failed: ") + std::strerror(errno)};
                        continue;
                    }
                    pollfd poll_fd{fd, POLLOUT, 0};
                    int ready = 0;
                    for (;;)
                    {
                        ready = ::poll(&poll_fd, 1, PollTimeoutMs(deadline));
                        // 超时
                        if (ready == 0)
                        {
                            final_status = {StatusCode::kDeadlineExceeded, "RPC deadline exceeded while connecting"};
                            return final_status;
                        }
                        if (errno == EINTR)
                        {
                            continue;
                        }
                        break;
                    }
                    // poll() 调用出错,继续遍历 candidate
                    if (ready < 0)
                    {
                        final_status = {StatusCode::kUnavailable, std::string("connect poll failed: ") + std::strerror(errno)};
                        continue;
                    }
                    // ready > 0 poll成功 poll 返回可写 != connect 一定成功； 必须检查 socket_error == 0 才算成功
                    int socket_error = 0;
                    socklen_t length = sizeof(socket_error);
                    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &length) != 0 || socket_error != 0)
                    {
                        final_status = {StatusCode::kUnavailable, std::string("connect failed: ") + std::strerror(socket_error == 0 ? errno : socket_error)};
                        continue;
                    }
                }
                if (!DeadlineExpired(deadline))
                {
                    *output = std::move(socket);
                    ::freeaddrinfo(result);
                    return Status::Ok();
                }
                final_status = {StatusCode::kDeadlineExceeded, "RPC deadline exceeded while connecting"};
            }
            ::freeaddrinfo(result);
            return final_status;
        }

        void SetControllerFailure(google::protobuf::RpcController *controller, Status status)
        {
            if (controller == nullptr)
                return;
            // 自定义 Controller 可保留服务端的精确状态码；普通 Protobuf Controller仍能获得可读的错误文本。
            if (auto *rpc_controller = dynamic_cast<RpcController *>(controller))
            {
                rpc_controller->SetStatus(std::move(status));
                return;
            }
            controller->SetFailed(status.message());
        }

        Status DecodeRemoteStatus(std::uint32_t raw_code, const std::string &message)
        {
            if (raw_code > static_cast<std::uint32_t>(StatusCode::kInternal))
            {
                return {StatusCode::kProtocolError, "provider returned invalid status code"};
            }
            return {static_cast<StatusCode>(raw_code), message};
        }

    } // namespace

    class RpcChannel::ClientConnection final
    {
    public:
        using Completion = std::function<void(Status)>;

        explicit ClientConnection(Endpoint endpoint) : endpoint_(std::move(endpoint)) {}

        ~ClientConnection() { Stop(); }

        Status Submit(RpcFrame frame, TimePoint deadline,
                      google::protobuf::Message *response, Completion completion)
        {
            if (DeadlineExpired(deadline))
                return {StatusCode::kDeadlineExceeded, "RPC deadline exceeded before sending"};
            Status status = EnsureConnected(deadline);
            if (!status.ok())
                return status;
            if (DeadlineExpired(deadline))
                return {StatusCode::kDeadlineExceeded, "RPC deadline exceeded while connecting"};

            // 帧中携带发送瞬间的剩余预算，而非 Controller 的初始超时时间。
            frame.timeout_ms = IsInfiniteDeadline(deadline)
                                   ? 0
                                   : static_cast<std::uint32_t>(std::max(1, PollTimeoutMs(deadline)));
            std::string wire;
            status = FrameCodec::Encode(frame, &wire);
            if (!status.ok())
                return status;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_.emplace(frame.request_id, PendingCall{deadline, response, std::move(completion)});
            }
            status = SendWire(wire, deadline);
            if (!status.ok())
            {
                Completion failed;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    const auto it = pending_.find(frame.request_id);
                    if (it != pending_.end())
                    {
                        failed = std::move(it->second.completion);
                        pending_.erase(it);
                    }
                }
                if (failed)
                    failed(status);
            }
            return status;
        }

        void Stop()
        {
            std::vector<Completion> completions;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = true;
                if (fd_ >= 0)
                {
                    ::shutdown(fd_, SHUT_RDWR);
                    ::close(fd_);
                    fd_ = -1;
                }
                for (auto &[id, pending] : pending_)
                {
                    (void)id;
                    completions.push_back(std::move(pending.completion));
                }
                pending_.clear();
            }
            if (reader_.joinable() && reader_.get_id() != std::this_thread::get_id())
                reader_.join();
            for (auto &completion : completions)
            {
                if (completion)
                    completion({StatusCode::kUnavailable, "RPC connection stopped"});
            }
        }

    private:
        struct PendingCall
        {
            std::chrono::steady_clock::time_point deadline;
            google::protobuf::Message *response;
            Completion completion;
        };

        Status EnsureConnected(TimePoint deadline)
        {
            // 多个异步调用共用同一条长连接时，只允许一个线程实际执行重连；
            // 等待这个互斥量的时间同样计入本次 RPC 的 Controller deadline。
            std::unique_lock<std::timed_mutex> connect_lock(connect_mutex_, std::defer_lock);
            if (IsInfiniteDeadline(deadline))
            {
                connect_lock.lock();
            }
            else if (!connect_lock.try_lock_until(deadline))
            {
                return {StatusCode::kDeadlineExceeded, "RPC deadline exceeded waiting to connect"};
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_)
                    return {StatusCode::kUnavailable, "RPC connection is stopped"};
                if (fd_ >= 0)
                    return Status::Ok();
            }
            if (reader_.joinable())
            {
                if (reader_.get_id() == std::this_thread::get_id())
                {
                    return {StatusCode::kUnavailable, "RPC connection is reconnecting"};
                }
                reader_.join();
            }
            FileDescriptor socket;
            Status status = Connect(endpoint_, deadline, &socket);
            if (!status.ok())
                return status;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_)
                    return {StatusCode::kUnavailable, "RPC connection is stopped"};
                fd_ = socket.release();
            }
            reader_ = std::thread([this]
                                  { ReaderLoop(); });
            return Status::Ok();
        }

        Status SendWire(const std::string &wire, TimePoint deadline)
        {
            std::unique_lock<std::timed_mutex> send_lock(send_mutex_, std::defer_lock);
            if (IsInfiniteDeadline(deadline))
            {
                send_lock.lock();
            }
            else if (!send_lock.try_lock_until(deadline))
            {
                return {StatusCode::kDeadlineExceeded, "RPC deadline exceeded waiting to send"};
            }
            int fd = -1;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                fd = fd_;
            }
            if (fd < 0)
                return {StatusCode::kUnavailable, "RPC connection is closed"};
            std::size_t sent = 0;
            while (sent < wire.size())
            {
                if (DeadlineExpired(deadline))
                    return {StatusCode::kDeadlineExceeded, "RPC deadline exceeded while sending"};
                // MSG_NOSIGNAL; linux 下如果对端已经关闭连接，再 send 可能触发 SIGPIPE，默认行为甚至可能让整个进程退出
                // MSG_NOSIGNAL 的意思是：不要发这个信号，改为让 send 返回错误，例如 EPIPE。
                const ssize_t count = ::send(fd, wire.data() + sent, wire.size() - sent, MSG_NOSIGNAL);
                if (count > 0)
                {
                    sent += static_cast<std::size_t>(count);
                    continue;
                }
                if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                {
                    pollfd poll_fd{fd, POLLOUT, 0};
                    const int ready = ::poll(&poll_fd, 1, PollTimeoutMs(deadline));
                    // 等到了 deadline，返回超时
                    if (ready == 0)
                        return {StatusCode::kDeadlineExceeded, "RPC deadline exceeded while sending"};
                    // poll 被信号打断，重新进入循环。
                    if (ready < 0 && errno == EINTR)
                        continue;
                    // 出现 POLLERR | POLLHUP | POLLNVAL：连接异常、对端关闭或 fd 无效，返回不可用。
                    if (ready <= 0 || (poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)))
                        return {StatusCode::kUnavailable, std::string("send poll failed: ") + std::strerror(errno)};
                    // ready > 0 可写
                    continue;
                }
                return {StatusCode::kUnavailable, std::string("send failed: ") + std::strerror(errno)};
            }
            return Status::Ok();
        }

        void ReaderLoop()
        {
            FrameParser parser;
            char buffer[8192];
            for (;;)
            {
                int fd = -1;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (stopping_ || fd_ < 0)
                        break;
                    fd = fd_;
                }
                pollfd poll_fd{fd, POLLIN, 0};
                const int ready = ::poll(&poll_fd, 1, ReceivePollTimeoutMs());
                // 除了 EINTR（被信号临时中断，可重试）之外，poll 失败，说明接收线程无法继续工作，所有仍在等待响应的请求都要失败。
                if (ready < 0 && errno != EINTR)
                {
                    FailAll({StatusCode::kUnavailable, std::string("poll failed: ") + std::strerror(errno)});
                    break;
                }
                // POLLERR：socket 出错；POLLHUP：对端关闭连接；POLLNVAL：fd 无效
                if (ready > 0 && (poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)))
                {
                    FailAll({StatusCode::kUnavailable, "provider connection closed"});
                    break;
                }
                if (ready > 0 && (poll_fd.revents & POLLIN))
                {
                    // poll 表示“理论上可读”，实际 recv 仍可能读到不同结果
                    const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
                    // count == 0：对端正常关闭连接；
                    if (count == 0)
                    {
                        FailAll({StatusCode::kUnavailable, "provider connection closed"});
                        break;
                    }
                    // EAGAIN/EWOULDBLOCK：可读状态已被其他原因消耗，非阻塞 socket 下稍后重试；EINTR：recv 被信号打断，重试；
                    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
                        continue;
                    if (count < 0)
                    {
                        FailAll({StatusCode::kUnavailable, std::string("recv failed: ") + std::strerror(errno)});
                        break;
                    }
                    std::vector<RpcFrame> frames;
                    Status status = parser.Append(std::string_view(buffer, static_cast<std::size_t>(count)), &frames);
                    if (!status.ok())
                    {
                        FailAll(status);
                        break;
                    }
                    for (const RpcFrame &frame : frames)
                        CompleteResponse(frame);
                }
                ExpireCalls();
            }
        }

        void CompleteResponse(const RpcFrame &frame)
        {
            if (frame.type != FrameType::kResponse)
                return;
            PendingCall pending;
            bool found = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto it = pending_.find(frame.request_id);
                if (it == pending_.end())
                    return;
                pending = std::move(it->second);
                pending_.erase(it);
                found = true;
            }
            if (!found)
                return;
            Status status = frame.status_code == 0
                                ? Status::Ok()
                                : DecodeRemoteStatus(frame.status_code, frame.payload);
            if (status.ok() && !pending.response->ParseFromString(frame.payload))
            {
                status = {StatusCode::kProtocolError, "cannot parse RPC response"};
            }
            if (pending.completion)
                pending.completion(std::move(status));
        }

        void ExpireCalls()
        {
            std::vector<std::pair<std::uint64_t, Completion>> expired;
            const auto now = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto it = pending_.begin(); it != pending_.end();)
                {
                    if (it->second.deadline > now)
                    {
                        ++it;
                        continue;
                    }
                    expired.emplace_back(it->first, std::move(it->second.completion));
                    it = pending_.erase(it);
                }
            }
            for (auto &[request_id, completion] : expired)
            {
                RpcFrame cancel;
                cancel.type = FrameType::kCancel;
                cancel.request_id = request_id;
                std::string wire;
                if (FrameCodec::Encode(cancel, &wire).ok())
                    (void)SendWire(wire, TimePoint::max());
                if (completion)
                    completion({StatusCode::kDeadlineExceeded, "RPC deadline exceeded"});
            }
        }

        int ReceivePollTimeoutMs()
        {
            TimePoint nearest = TimePoint::max();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto &[request_id, pending] : pending_)
                {
                    (void)request_id;
                    nearest = std::min(nearest, pending.deadline);
                }
            }
            if (IsInfiniteDeadline(nearest))
                return 100;
            return std::min(100, PollTimeoutMs(nearest));
        }

        void FailAll(Status status)
        {
            std::vector<Completion> completions;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (fd_ >= 0)
                {
                    ::close(fd_);
                    fd_ = -1;
                }
                for (auto &[id, pending] : pending_)
                {
                    (void)id;
                    completions.push_back(std::move(pending.completion));
                }
                pending_.clear();
            }
            for (auto &completion : completions)
                if (completion)
                    completion(status);
        }

        Endpoint endpoint_;
        std::mutex mutex_;
        std::timed_mutex connect_mutex_;
        std::timed_mutex send_mutex_;
        int fd_{-1};
        bool stopping_{false};
        std::unordered_map<std::uint64_t, PendingCall> pending_;
        std::thread reader_;
    };

    RpcChannel::RpcChannel(ServiceRegistryPtr registry)
        : registry_(std::move(registry)) {}

    RpcChannel::~RpcChannel()
    {
        std::unordered_map<std::string, std::shared_ptr<ClientConnection>> connections;
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections.swap(connections_);
        }
        for (auto &[endpoint, connection] : connections)
        {
            (void)endpoint;
            connection->Stop();
        }
    }

    void RpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
                                google::protobuf::RpcController *controller,
                                const google::protobuf::Message *request,
                                google::protobuf::Message *response,
                                google::protobuf::Closure *done)
    {
        auto complete = [&done]()
        { if (done != nullptr) done->Run(); };
        if (method == nullptr || controller == nullptr || request == nullptr || response == nullptr || registry_ == nullptr)
        {
            SetControllerFailure(controller, {StatusCode::kInvalidArgument, "invalid RPC call arguments"});
            complete();
            return;
        }
        auto *rpc_controller = dynamic_cast<RpcController *>(controller);
        if (rpc_controller == nullptr)
        {
            SetControllerFailure(controller, {StatusCode::kInvalidArgument, "RPC calls require myrpc::RpcController"});
            complete();
            return;
        }
        if (rpc_controller->Expired())
        {
            SetControllerFailure(controller, {StatusCode::kDeadlineExceeded, "RPC deadline already expired"});
            complete();
            return;
        }
        const TimePoint deadline = rpc_controller->deadline();

        // Protobuf Stub 会调用本函数；这里将“本地方法调用”转换为网络 RPC 请求。
        std::string request_payload;
        if (!request->SerializeToString(&request_payload))
        {
            SetControllerFailure(controller, {StatusCode::kInternal, "cannot serialize RPC request"});
            complete();
            return;
        }
        if (rpc_controller->Expired())
        {
            SetControllerFailure(controller, {StatusCode::kDeadlineExceeded, "RPC deadline exceeded while serializing request"});
            complete();
            return;
        }
        const auto *service = method->service();
        std::vector<Endpoint> endpoints;
        // 先发现所有实例，再根据负载均衡策略选择一个调用。
        Status status = registry_->Resolve(service->name(), method->name(), &endpoints);
        if (rpc_controller->Expired())
        {
            SetControllerFailure(controller, {StatusCode::kDeadlineExceeded, "RPC deadline exceeded during discovery"});
            complete();
            return;
        }
        if (!status.ok() || endpoints.empty())
        {
            SetControllerFailure(controller, status.ok()
                                                 ? Status{StatusCode::kUnavailable, "no provider is available"}
                                                 : status);
            complete();
            return;
        }

        RpcFrame frame;
        frame.type = FrameType::kRequest;
        // fetch_add 先返回旧值，再原子加 1
        frame.request_id = next_request_id_.fetch_add(1);
        frame.service = service->name();
        frame.method = method->name();
        frame.payload = std::move(request_payload);

        if (done != nullptr)
        {
            auto once = std::make_shared<std::atomic_bool>(false);
            auto finish = [controller, done, once](Status result)
            {
                // once 的作用是：两个不同 RPC，执行finish互不影响， 但同一个 RPC 的 finish 可能由多个线程触发
                // 线程 A：收到服务端正常响应:finish(Status::Ok())
                // 线程 B：超时扫描发现 deadline 到期 ： finish(kDeadlineExceeded)
                // 线程 C：连接断开 : finish(kUnavailable)
                if (once->exchange(true))
                    return;
                if (!result.ok())
                    SetControllerFailure(controller, std::move(result));
                done->Run();
            };
            status = GetConnection(SelectEndpoint(endpoints))->Submit(frame, deadline, response, finish);
            if (!status.ok())
                finish(status);
            return;
        }

        auto promise = std::make_shared<std::promise<Status>>();
        auto future = promise->get_future();
        auto once = std::make_shared<std::atomic_bool>(false);
        auto finish = [controller, promise, once](Status result)
        {
            if (once->exchange(true))
                return;
            if (!result.ok())
                SetControllerFailure(controller, result);
            promise->set_value(std::move(result));
        };
        status = GetConnection(SelectEndpoint(endpoints))->Submit(frame, deadline, response, finish);
        if (!status.ok())
            finish(status);
        (void)future.get();
    }

    Endpoint RpcChannel::SelectEndpoint(const std::vector<Endpoint> &endpoints)
    {
        return endpoints[next_endpoint_.fetch_add(1) % endpoints.size()];
    }

    std::shared_ptr<RpcChannel::ClientConnection> RpcChannel::GetConnection(const Endpoint &endpoint)
    {
        const std::string key = endpoint.ToString();
        std::cout << "selected:" << key << "\n";
        std::lock_guard<std::mutex> lock(connections_mutex_);
        const auto found = connections_.find(key);
        if (found != connections_.end())
            return found->second;
        auto connection = std::make_shared<ClientConnection>(endpoint);
        connections_.emplace(key, connection);
        return connection;
    }

} // namespace myrpc
