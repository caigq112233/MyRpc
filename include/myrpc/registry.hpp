#pragma once

#include <memory>
#include <string>
#include <vector>

#include "myrpc/endpoint.hpp"
#include "myrpc/status.hpp"

namespace myrpc {

class ServiceRegistry {
public:
    virtual ~ServiceRegistry() = default;
    // Provider 调用 Register 发布自己；同一方法可对应多个 Endpoint。
    virtual Status Register(const std::string& service, const std::string& method,
                            const Endpoint& endpoint) = 0;
    // Consumer 调用 Resolve 获取当前可用的所有实例。
    virtual Status Resolve(const std::string& service, const std::string& method,
                           std::vector<Endpoint>* endpoints) = 0;
    // Provider 可周期性调用此函数。需要会话恢复的注册中心应在这里恢复连接并补注册
    // 因会话过期而丢失的临时节点；不需要该能力的实现保持默认无操作即可。
    virtual Status EnsureRegistered() { return Status::Ok(); }
};

using ServiceRegistryPtr = std::shared_ptr<ServiceRegistry>;

}  // namespace myrpc
