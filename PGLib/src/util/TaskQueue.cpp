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

            if (!m_taskQueue.empty()) {
                task = std::move(m_taskQueue.front());
                m_taskQueue.pop();
                // Busy before the task leaves the count, so that isWorking() always sees it queued or running.
                m_isBusy = true;
                m_queuedTasks--;
            }
        }

        if (task) {
            if (!ExceptionHandler::runGuarded(task) && s_exceptionCallback)
                s_exceptionCallback();
            m_isBusy = false;
        }
    }

    // Whatever is still queued will never run now. Drop it and say so, or waitForCompletion() would wait for it
    // forever.
    const std::scoped_lock lock(m_queueMutex);
    m_taskQueue = { };
    m_queuedTasks = 0;
}

bool TaskQueue::isWorking() const
{
    // The count first: the worker is busy before a task leaves the count, so a task that is not counted anymore is
    // either still running or done.
    return m_queuedTasks > 0 || m_isBusy;
}

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
    m_running = false;
    m_cv.notify_one();
    if (m_workerThread.joinable())
        m_workerThread.join();
}

void TaskQueue::setExceptionCallback(const std::function<void()>& callback) { s_exceptionCallback = callback; }
