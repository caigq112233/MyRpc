#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
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
        // /myrpc/services/{service}/{method}
        static std::string BasePath(const std::string &service, const std::string &method);

        std::string connection_string_;
        std::chrono::milliseconds connect_timeout_;
        // handle_ ,zookeeper 会话连接，同一个进程中，多个线程同时共享一个 handle_
        zhandle_t *handle_{nullptr};
        // handle_ 锁，多线程注册和调用的时候会竞争访问 handle_，需要加锁保护
        std::mutex mutex_;
        std::condition_variable connected_cv_;
        // handle_ 连接状态机，因为连接不是原子的，存在多线程并发风险，需要状态机保证原子性
        ConnectionState connection_state_{ConnectionState::kNotStarted};
    };

} // namespace myrpc
