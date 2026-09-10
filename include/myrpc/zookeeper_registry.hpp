#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <zookeeper/zookeeper.h>

#include "myrpc/registry.hpp"

namespace myrpc
{
    // 一个进程长期复用一个 ZooKeeper 会话。Provider 以临时顺序子节点注册，
    // 因而多个服务实例可以同时存在，进程失联后节点会由 ZooKeeper 自动清理。
    class ZookeeperRegistry final : public ServiceRegistry
    {
    public:
        explicit ZookeeperRegistry(std::string connection_string,
                                   std::chrono::milliseconds connect_timeout = std::chrono::seconds(5));
        ~ZookeeperRegistry() override;

        Status Start();
        Status Register(const std::string &service, const std::string &method,
                        const Endpoint &endpoint) override;
        Status Resolve(const std::string &service, const std::string &method,
                       std::vector<Endpoint> *endpoints) override;
        Status EnsureRegistered() override;

    private:
        enum class ConnectionState
        {
            kNotStarted, // 尚未开始连接
            kConnecting, // 已调用 zookeeper_init，正在等待 watcher 结果
            kConnected,  // 已连接成功
            kFailed,     // 初始化或连接失败
        };
        static void Watcher(zhandle_t *handle, int type, int state, const char *path, void *context);
        void OnSessionEvent(int type, int state);
        Status EnsurePath(const std::string &path);
        Status CreateProviderNode(const std::string& service, const std::string& method,
                                  const Endpoint& endpoint);
        // /myrpc/services/{service}/{method}
        static std::string BasePath(const std::string &service, const std::string &method);

        struct Registration {
            std::string service;
            std::string method;
            Endpoint endpoint;
        };

        std::string connection_string_;
        std::chrono::milliseconds connect_timeout_;
        // handle_ ,zookeeper 会话连接，同一个进程中，多个线程同时共享一个 handle_
        zhandle_t *handle_{nullptr};
        // handle_ 锁，多线程注册和调用的时候会竞争访问 handle_，需要加锁保护
        std::mutex mutex_;
        std::condition_variable connected_cv_;
        // handle_ 连接状态机，因为连接不是原子的，存在多线程并发风险，需要状态机保证原子性
        ConnectionState connection_state_{ConnectionState::kNotStarted};
        // 记录本进程发布过的临时节点。会话过期后节点会消失，后续
        // EnsureRegistered 会在新会话中将这些节点重新创建。
        std::unordered_map<std::string, Registration> registrations_;
        std::uint64_t session_generation_{0};
        std::uint64_t registered_generation_{0};
    };

} // namespace myrpc
