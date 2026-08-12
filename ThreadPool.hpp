#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

// 固定大小的线程池：每个 worker 线程反复从任务队列取任务执行。
// 用于把“一条连接的完整生命周期”整体扔给某个线程处理，而不占用 accept 所在的事件循环线程。
class ThreadPool
{
public:
    explicit ThreadPool(size_t thread_count)
    {
        workers_.reserve(thread_count);
        for (size_t i = 0; i < thread_count; ++i)
        {
            workers_.emplace_back(&ThreadPool::WorkerLoop, this);
        }
    }

    ~ThreadPool()
    {
        Stop();
    }

    // 提交一个任务，交给某个空闲 worker 执行；如果当前所有 worker 都忙，任务在队列里排队等待。
    void Enqueue(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                return;
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

    // 停止线程池：不再接受新任务，等待队列中已有的任务执行完毕后再退出所有 worker 线程。
    void Stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                return;
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto &t : workers_)
        {
            if (t.joinable())
                t.join();
        }
    }

private:
    void WorkerLoop()
    {
        while (true)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]
                          { return stopping_ || !tasks_.empty(); });

                if (stopping_ && tasks_.empty())
                    return;

                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
};
