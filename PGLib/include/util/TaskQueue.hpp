#pragma once

#include "util/ExceptionHandler.hpp"

#include <cpptrace/from_current.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

/**
 * @brief Single-threaded task queue that executes submitted callables serially on a background thread.
 *
 * Tasks are processed in FIFO order. If a task throws an exception it is captured via
 * ExceptionHandler and an optional callback is invoked; no further tasks are processed after that.
 */
class TaskQueue {
private:
    static constexpr int LOOP_INTERVAL = 10; /** Worker loop interval in milliseconds */

    std::queue<std::function<void()>> m_taskQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running {true};
    std::atomic<bool> m_isBusy {false};
    std::atomic<size_t> m_queuedTasks {0};
    std::thread m_workerThread;

    static std::function<void()> s_exceptionCallback;

    void workerLoop();

public:
    /**
     * @brief Constructs a TaskQueue and starts the background worker thread.
     */
    TaskQueue();

    /**
     * @brief Destroys the TaskQueue, shutting down the worker thread and waiting for it to finish.
     */
    ~TaskQueue();

    TaskQueue(const TaskQueue&) = delete;
    auto operator=(const TaskQueue&) -> TaskQueue& = delete;
    TaskQueue(TaskQueue&&) = delete;
    auto operator=(TaskQueue&&) -> TaskQueue& = delete;

    /**
     * @brief Submits a callable to be executed on the background worker thread.
     *
     * The task is silently dropped if ExceptionHandler::hasException() returns true.
     *
     * @tparam Func Callable type (any invocable that takes no arguments).
     * @param func The callable to enqueue.
     */
    template <typename Func> void queueTask(Func&& func)
    {
        if (ExceptionHandler::hasException()) {
            // exception was thrown, don't allow any further queued tasks
            return;
        }

        {
            std::scoped_lock const lock(m_queueMutex);
            m_taskQueue.emplace(std::forward<Func>(func));
            m_queuedTasks++;
        }
        m_cv.notify_one();
    }

    /**
     * @brief Returns whether the queue is currently processing a task or has tasks waiting.
     *
     * @return true if a task is executing or at least one task is queued, false otherwise.
     */
    auto isWorking() const -> bool;

    /**
     * @brief Returns the number of tasks waiting in the queue (not including any currently executing task).
     *
     * @return Number of pending tasks.
     */
    auto getQueuedTaskCount() const -> size_t;

    /**
     * @brief Returns whether the worker thread is actively executing a task right now.
     *
     * @return true if a task is currently being executed, false otherwise.
     */
    auto isProcessing() const -> bool;

    /**
     * @brief Returns whether the queue has been shut down.
     *
     * @return true if shutdown() has been called and the worker thread is no longer running.
     */
    auto isShutdown() const -> bool;

    /**
     * @brief Blocks the calling thread until all queued and in-progress tasks have finished.
     */
    void waitForCompletion() const;

    /**
     * @brief Signals the worker thread to stop and waits for it to exit.
     *
     * Any tasks remaining in the queue are discarded.
     */
    void shutdown();

    /**
     * @brief Registers a callback that is invoked when a task throws an unhandled exception.
     *
     * @param callback Callable invoked (on the worker thread) immediately after the exception is captured.
     */
    static void setExceptionCallback(const std::function<void()>& callback);
};
