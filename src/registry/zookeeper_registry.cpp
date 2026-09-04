#include "myrpc/zookeeper_registry.hpp"

#include <algorithm>
#include <array>
#include <iostream>
namespace myrpc
{

    ZookeeperRegistry::ZookeeperRegistry(std::string connection_string,
                                         std::chrono::milliseconds connect_timeout)
        : connection_string_(std::move(connection_string)), connect_timeout_(connect_timeout) {}

    ZookeeperRegistry::~ZookeeperRegistry()
    {
        zhandle_t *handle = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handle = handle_;
            handle_ = nullptr;
            connection_state_ = ConnectionState::kFailed;
        }
        if (handle != nullptr)
            zookeeper_close(handle);
    }

    Status ZookeeperRegistry::Start()
    {
        zhandle_t *handle_to_close = nullptr;

        std::unique_lock<std::mutex> lock(mutex_);

        // 已连接成功：直接复用同一个 ZooKeeper 会话。
        if (connection_state_ == ConnectionState::kConnected)
        {
            lock.unlock();
            return EnsurePath("/myrpc");
        }

        // 已经失败时不让其他线程错误地把它当成成功连接。
        if (connection_state_ == ConnectionState::kFailed)
        {
            return {StatusCode::kUnavailable, "ZooKeeper connection is unavailable"};
        }

        // 只有第一个线程真正发起初始化。
        if (connection_state_ == ConnectionState::kNotStarted)
        {
            connection_state_ = ConnectionState::kConnecting;

            handle_ = zookeeper_init(
                connection_string_.c_str(),
                &ZookeeperRegistry::Watcher,
                300000,
                nullptr,
                this,
                0);

            if (handle_ == nullptr)
            {
                connection_state_ = ConnectionState::kFailed;
                connected_cv_.notify_all();
                return {StatusCode::kUnavailable, "zookeeper_init failed"};
            }
        }

        // 如果当前状态是 kConnecting，说明：
        // - 当前线程刚发起初始化；或
        // - 另一个线程正在初始化。
        // 两种情况都等待 watcher 通知最终结果。
        const bool completed = connected_cv_.wait_for(
            lock,
            connect_timeout_,
            [this]
            {
                return connection_state_ == ConnectionState::kConnected ||
                       connection_state_ == ConnectionState::kFailed;
            });

        if (!completed || connection_state_ != ConnectionState::kConnected)
        {
            // 只有拿到 handle_ 的线程负责关闭，避免重复 close。
            handle_to_close = handle_;
            handle_ = nullptr;
            connection_state_ = ConnectionState::kFailed;
            connected_cv_.notify_all();

            lock.unlock();

            if (handle_to_close != nullptr)
            {
                zookeeper_close(handle_to_close);
            }

            return {StatusCode::kDeadlineExceeded,
                    "ZooKeeper connection timed out"};
        }

        lock.unlock();
        return EnsurePath("/myrpc");
    }

    Status ZookeeperRegistry::Register(const std::string &service, const std::string &method,
                                       const Endpoint &endpoint)
    {
        if (!endpoint.valid())
            return {StatusCode::kInvalidArgument, "invalid provider endpoint"};
        Status status = Start();
        if (!status.ok())
            return status;
        const std::string base = BasePath(service, method);
        // 父节点是永久节点；真正的 Provider 节点才是临时顺序节点。
        for (const std::string &path : {std::string("/myrpc/services"),
                                        std::string("/myrpc/services/") + service,
                                        base, base + "/providers"})
        {
            status = EnsurePath(path);
            if (!status.ok())
                return status;
        }

        std::array<char, 512> created_path{};
        const std::string address = endpoint.ToString();
        std::lock_guard<std::mutex> lock(mutex_);
        // ZooKeeper 会追加序号，例如 provider-0000000007，避免多实例冲突。
        const int result = zoo_create(handle_, (base + "/providers/provider-").c_str(),
                                      address.data(), static_cast<int>(address.size()),
                                      &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL | ZOO_SEQUENCE,
                                      created_path.data(), static_cast<int>(created_path.size()));
        if (result != ZOK)
            return {StatusCode::kUnavailable, "cannot register provider in ZooKeeper"};
        return Status::Ok();
    }

    Status ZookeeperRegistry::Resolve(const std::string &service, const std::string &method,
                                      std::vector<Endpoint> *endpoints)
    {
        if (endpoints == nullptr)
            return {StatusCode::kInvalidArgument, "endpoints is null"};
        endpoints->clear();
        Status status = Start();
        if (!status.ok())
            return status;

        String_vector children{};
        const std::string providers = BasePath(service, method) + "/providers";
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const int result = zoo_get_children(handle_, providers.c_str(), 0, &children);
            if (result == ZNONODE)
                return Status::Ok();
            if (result != ZOK)
                return {StatusCode::kUnavailable, "cannot query providers from ZooKeeper"};
        }
        for (int index = 0; index < children.count; ++index)
        {
            const std::string path = providers + "/" + children.data[index];
            std::array<char, 256> data{};
            int data_size = static_cast<int>(data.size());
            int result = ZSYSTEMERROR;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                result = zoo_get(handle_, path.c_str(), 0, data.data(), &data_size, nullptr);
            }
            // 在“查询子节点”和“读取节点数据”之间，实例可能下线；忽略即可。
            if (result != ZOK)
                continue;
            Endpoint endpoint;
            status = Endpoint::Parse(std::string(data.data(), data_size), &endpoint);
            if (status.ok())
                endpoints->push_back(std::move(endpoint));
        }
        deallocate_String_vector(&children);
        std::sort(endpoints->begin(), endpoints->end(),
                  [](const Endpoint &left, const Endpoint &right)
                  { return left.ToString() < right.ToString(); });
        return Status::Ok();
    }

    void ZookeeperRegistry::Watcher(zhandle_t *, int type, int state, const char *, void *context)
    {
        static_cast<ZookeeperRegistry *>(context)->OnSessionEvent(type, state);
    }

    void ZookeeperRegistry::OnSessionEvent(int type, int state)
    {
        std::cout << "ZooKeeper event: type=" << type
                  << ", state=" << state << std::endl;

        if (type != ZOO_SESSION_EVENT)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (state == ZOO_CONNECTED_STATE)
        {
            connection_state_ = ConnectionState::kConnected;
            connected_cv_.notify_all();
            return;
        }

        // 初始化阶段收到明确失败事件时，唤醒所有等待连接的线程。
        if (state == ZOO_AUTH_FAILED_STATE ||
            state == ZOO_EXPIRED_SESSION_STATE)
        {
            connection_state_ = ConnectionState::kFailed;
            connected_cv_.notify_all();
        }
    }

    Status ZookeeperRegistry::EnsurePath(const std::string &path)
    {
        std::array<char, 512> created_path{};
        std::lock_guard<std::mutex> lock(mutex_);
        const int result = zoo_create(handle_, path.c_str(), nullptr, 0, &ZOO_OPEN_ACL_UNSAFE, 0,
                                      created_path.data(), static_cast<int>(created_path.size()));
        if (result == ZOK || result == ZNODEEXISTS)
            return Status::Ok();
        return {StatusCode::kUnavailable, "cannot create ZooKeeper path " + path};
    }

    std::string ZookeeperRegistry::BasePath(const std::string &service, const std::string &method)
    {
        return "/myrpc/services/" + service + "/" + method;
    }

} // namespace myrpc
