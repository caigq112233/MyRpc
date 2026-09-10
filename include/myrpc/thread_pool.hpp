#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace myrpc
{

    class ThreadPool final
    {
    public:
        using Task = std::function<void()>;

        ThreadPool(std::size_t thread_count, std::size_t max_queue_size);
        ~ThreadPool();
        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;

        // 不等待：线程池停止或队列满时立即返回 false。
        bool TryRun(Task task);

    private:
        void WorkerLoop();

        std::mutex mutex_;
        std::condition_variable not_empty_;
        std::queue<Task> tasks_;
        std::vector<std::thread> workers_;
        std::size_t max_queue_size_;
        bool stopping_{false};
    };
}
