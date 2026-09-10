#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <google/protobuf/service.h>

#include "myrpc/registry.hpp"

namespace myrpc
{

    class RpcChannel final : public google::protobuf::RpcChannel
    {
    public:
        explicit RpcChannel(ServiceRegistryPtr registry);
        ~RpcChannel() override;

        void CallMethod(const google::protobuf::MethodDescriptor *method,
                        google::protobuf::RpcController *controller,
                        const google::protobuf::Message *request,
                        google::protobuf::Message *response,
                        google::protobuf::Closure *done) override;

    private:
        // 当前实现采用轮询；后续可替换为随机、一致性哈希或加权负载均衡。
        Endpoint SelectEndpoint(const std::vector<Endpoint> &endpoints);
        class ClientConnection;
        std::shared_ptr<ClientConnection> GetConnection(const Endpoint& endpoint);

        ServiceRegistryPtr registry_;
        std::atomic_uint64_t next_request_id_{1};
        std::atomic_size_t next_endpoint_{0};
        std::mutex connections_mutex_;
        std::unordered_map<std::string, std::shared_ptr<ClientConnection>> connections_;
    };

} // namespace myrpc
