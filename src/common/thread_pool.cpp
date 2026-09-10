#include "myrpc/thread_pool.hpp"

#include <utility>

namespace myrpc {

ThreadPool::ThreadPool(std::size_t thread_count, std::size_t max_queue_size)
    : max_queue_size_(max_queue_size) {
    workers_.reserve(thread_count);
    for (std::size_t index = 0; index < thread_count; ++index) {
        workers_.emplace_back([this] { WorkerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    not_empty_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

bool ThreadPool::TryRun(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || tasks_.size() >= max_queue_size_) {
            return false;
        }
        tasks_.push(std::move(task));
    }
    not_empty_.notify_one();
    return true;
}

void ThreadPool::WorkerLoop() {
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            not_empty_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (stopping_ && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}

}  // namespace myrpc
