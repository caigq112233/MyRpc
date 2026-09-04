#include "myrpc/channel.hpp"

#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "myrpc/frame.hpp"

namespace myrpc {
namespace {

class FileDescriptor {
public:
    explicit FileDescriptor(int fd = -1) : fd_(fd) {}
    ~FileDescriptor() { if (fd_ >= 0) ::close(fd_); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    [[nodiscard]] int get() const { return fd_; }
private:
    int fd_;
};

Status SetSocketTimeout(int fd, std::chrono::milliseconds timeout) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(timeout - seconds);
    const timeval value{static_cast<time_t>(seconds.count()),
                        static_cast<suseconds_t>(micros.count())};
    if (::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value)) != 0 ||
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)) != 0) {
        return {StatusCode::kInternal, "failed to configure socket timeout"};
    }
    return Status::Ok();
}

Status Connect(const Endpoint& endpoint, std::chrono::milliseconds timeout, FileDescriptor* output) {
    // getaddrinfo 同时兼容 IPv4、IPv6 和域名，不依赖 inet_addr。
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    const std::string port = std::to_string(endpoint.port);
    if (::getaddrinfo(endpoint.host.c_str(), port.c_str(), &hints, &result) != 0) {
        return {StatusCode::kUnavailable, "cannot resolve endpoint " + endpoint.ToString()};
    }
    Status final_status{StatusCode::kUnavailable, "cannot connect to " + endpoint.ToString()};
    for (addrinfo* candidate = result; candidate != nullptr; candidate = candidate->ai_next) {
        const int fd = ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd < 0) continue;
        FileDescriptor socket(fd);
        Status status = SetSocketTimeout(fd, timeout);
        if (status.ok() && ::connect(fd, candidate->ai_addr, candidate->ai_addrlen) == 0) {
            *output = std::move(socket);
            ::freeaddrinfo(result);
            return Status::Ok();
        }
        final_status = {StatusCode::kUnavailable, std::string("connect failed: ") + std::strerror(errno)};
    }
    ::freeaddrinfo(result);
    return final_status;
}

}  // namespace

RpcChannel::RpcChannel(ServiceRegistryPtr registry, std::chrono::milliseconds timeout)
    : registry_(std::move(registry)), timeout_(timeout) {}

void RpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                            google::protobuf::RpcController* controller,
                            const google::protobuf::Message* request,
                            google::protobuf::Message* response,
                            google::protobuf::Closure* done) {
    auto complete = [&done]() { if (done != nullptr) done->Run(); };
    if (method == nullptr || request == nullptr || response == nullptr || registry_ == nullptr) {
        if (controller != nullptr) controller->SetFailed("invalid RPC call arguments");
        complete();
        return;
    }

    // Protobuf Stub 会调用本函数；这里将“本地方法调用”转换为网络 RPC 请求。
    std::string request_payload;
    if (!request->SerializeToString(&request_payload)) {
        if (controller != nullptr) controller->SetFailed("cannot serialize RPC request");
        complete();
        return;
    }
    const auto* service = method->service();
    std::vector<Endpoint> endpoints;
    // 先发现所有实例，再根据负载均衡策略选择一个调用。
    Status status = registry_->Resolve(service->name(), method->name(), &endpoints);
    if (!status.ok() || endpoints.empty()) {
        if (controller != nullptr) controller->SetFailed(
            status.ok() ? "no provider is available" : status.message());
        complete();
        return;
    }

    const std::uint64_t request_id = next_request_id_.fetch_add(1);
    std::string response_payload;
    status = Invoke(SelectEndpoint(endpoints), service->name(), method->name(),
                    request_payload, &response_payload, request_id);
    if (!status.ok()) {
        if (controller != nullptr) controller->SetFailed(status.message());
        complete();
        return;
    }
    if (!response->ParseFromString(response_payload)) {
        if (controller != nullptr) controller->SetFailed("cannot parse RPC response");
        complete();
        return;
    }
    complete();
}

Endpoint RpcChannel::SelectEndpoint(const std::vector<Endpoint>& endpoints) {
    return endpoints[next_endpoint_.fetch_add(1) % endpoints.size()];
}

Status RpcChannel::Invoke(const Endpoint& endpoint, const std::string& service,
                          const std::string& method, const std::string& request,
                          std::string* response, std::uint64_t request_id) const {
    RpcFrame frame;
    frame.type = FrameType::kRequest;
    frame.request_id = request_id;
    frame.service = service;
    frame.method = method;
    frame.payload = request;
    std::string wire;
    Status status = FrameCodec::Encode(frame, &wire);
    if (!status.ok()) return status;

    FileDescriptor socket;
    status = Connect(endpoint, timeout_, &socket);
    if (!status.ok()) return status;

    std::size_t sent = 0;
    // send 可能只写入部分字节，必须循环直到整个帧发完。
    while (sent < wire.size()) {
        const ssize_t count = ::send(socket.get(), wire.data() + sent, wire.size() - sent, MSG_NOSIGNAL);
        if (count <= 0) return {StatusCode::kUnavailable, std::string("send failed: ") + std::strerror(errno)};
        sent += static_cast<std::size_t>(count);
    }

    // 响应同样可能被 TCP 拆分或与其他数据合并，不能假设一次 recv 完整。
    FrameParser parser;
    char buffer[8192];
    for (;;) {
        const ssize_t count = ::recv(socket.get(), buffer, sizeof(buffer), 0);
        if (count == 0) return {StatusCode::kUnavailable, "provider closed connection before response"};
        if (count < 0) return {StatusCode::kDeadlineExceeded, std::string("receive failed: ") + std::strerror(errno)};
        std::vector<RpcFrame> frames;
        status = parser.Append(std::string_view(buffer, static_cast<std::size_t>(count)), &frames);
        if (!status.ok()) return status;
        for (const RpcFrame& reply : frames) {
            if (reply.type != FrameType::kResponse || reply.request_id != request_id) continue;
            if (reply.status_code != 0) {
                return {StatusCode::kInternal, "provider returned error " +
                                                   std::to_string(reply.status_code) +
                                                   ": " + reply.payload};
            }
            *response = reply.payload;
            return Status::Ok();
        }
    }
}

}  // namespace myrpc
