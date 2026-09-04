#pragma once

#include <atomic>
#include <chrono>
#include <memory>

#include <google/protobuf/service.h>

#include "myrpc/registry.hpp"

namespace myrpc
{

    class RpcChannel final : public google::protobuf::RpcChannel
    {
    public:
        explicit RpcChannel(ServiceRegistryPtr registry,
                            std::chrono::milliseconds timeout = std::chrono::seconds(3000));

        void CallMethod(const google::protobuf::MethodDescriptor *method,
                        google::protobuf::RpcController *controller,
                        const google::protobuf::Message *request,
                        google::protobuf::Message *response,
                        google::protobuf::Closure *done) override;

    private:
        // 当前实现采用轮询；后续可替换为随机、一致性哈希或加权负载均衡。
        Endpoint SelectEndpoint(const std::vector<Endpoint> &endpoints);
        Status Invoke(const Endpoint &endpoint, const std::string &service,
                      const std::string &method, const std::string &request,
                      std::string *response, std::uint64_t request_id) const;

        ServiceRegistryPtr registry_;
        std::chrono::milliseconds timeout_;
        std::atomic_uint64_t next_request_id_{1};
        std::atomic_size_t next_endpoint_{0};
    };

} // namespace myrpc
