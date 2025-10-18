#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

class TaskQueue {
private:
    static constexpr int LOOP_INTERVAL = 10; /** Worker loop interval in milliseconds */

    std::queue<std::function<void()>> m_taskQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running { true };
    std::atomic<bool> m_isBusy { false };
    std::atomic<size_t> m_queuedTasks { 0 };
    std::thread m_workerThread;

    void workerLoop();

public:
    TaskQueue();
    ~TaskQueue();

    TaskQueue(const TaskQueue&) = delete;
    auto operator=(const TaskQueue&) -> TaskQueue& = delete;
    TaskQueue(TaskQueue&&) = delete;
    auto operator=(TaskQueue&&) -> TaskQueue& = delete;

    // Queue a task to run
    template <typename Func> void queueTask(Func&& func)
    {
        {
            std::scoped_lock const lock(m_queueMutex);
            m_taskQueue.emplace(std::forward<Func>(func));
            m_queuedTasks++;
        }
        m_cv.notify_one();
    }

    // Check if the thread is currently working or has queued tasks
    auto isWorking() const -> bool;

    // Get number of queued tasks (not including currently processing)
    auto getQueuedTaskCount() const -> size_t;

    // Check if currently processing a task
    auto isProcessing() const -> bool;

    // Check if the queue has been shut down
    auto isShutdown() const -> bool;

    // Wait for all tasks to complete
    void waitForCompletion() const;

    void shutdown();
};
