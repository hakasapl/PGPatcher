#include "util/TaskQueue.hpp"

#include "util/ExceptionHandler.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

// STATICS.
std::function<void()> TaskQueue::s_exceptionCallback = nullptr;

TaskQueue::TaskQueue()
    : m_workerThread(&TaskQueue::workerLoop,
                     this)
{
}

TaskQueue::~TaskQueue() { shutdown(); }

void TaskQueue::workerLoop()
{
    while (m_running) {
        if (ExceptionHandler::hasException()) {
            // Exception was thrown, stop processing further tasks.
            m_running = false;
            break;
        }

        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_cv.wait(lock, [this] { return !m_taskQueue.empty() || !m_running; });

            if (!m_running) {
                // Shutting down: whatever is still queued is dropped below.
                break;
            }

            // Still running, so the wait ended because a task was queued.
            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();
            m_queuedTasks--;
        }

        if (task) {
            m_isBusy = true;
            if (!ExceptionHandler::runGuarded(task) && s_exceptionCallback)
                s_exceptionCallback();
            m_isBusy = false;
        }

        // Only now that it ran does the task stop counting as unfinished, so that isWorking() cannot miss it.
        m_unfinishedTasks--;
    }

    // Whatever is still queued will never run now. Drop it and say so, or waitForCompletion() would wait for it
    // forever.
    const std::scoped_lock lock(m_queueMutex);
    m_taskQueue = { };
    m_queuedTasks = 0;
    m_unfinishedTasks = 0;
}

bool TaskQueue::isWorking() const { return m_unfinishedTasks > 0; }

size_t TaskQueue::queuedTaskCount() const { return m_queuedTasks; }

bool TaskQueue::isProcessing() const { return m_isBusy; }

bool TaskQueue::isShutdown() const { return !m_running; }

void TaskQueue::waitForCompletion() const
{
    while (isWorking())
        std::this_thread::sleep_for(std::chrono::milliseconds(loopInterval));
}

void TaskQueue::shutdown()
{
    {
        // Under the lock: the worker checks m_running and starts to wait while it holds the lock, so the notification
        // cannot come between the two and leave it waiting for good.
        const std::scoped_lock lock(m_queueMutex);
        m_running = false;
    }
    m_cv.notify_one();
    if (m_workerThread.joinable())
        m_workerThread.join();
}

void TaskQueue::setExceptionCallback(const std::function<void()>& callback) { s_exceptionCallback = callback; }
