#include "util/TaskQueue.hpp"

#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

TaskQueue::TaskQueue() { m_workerThread = std::thread(&TaskQueue::workerLoop, this); }

TaskQueue::~TaskQueue() { shutdown(); }

void TaskQueue::workerLoop()
{
    while (m_running) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_cv.wait(lock, [this] { return !m_taskQueue.empty() || !m_running; });

            if (!m_running && m_taskQueue.empty()) {
                break;
            }

            if (!m_taskQueue.empty()) {
                task = std::move(m_taskQueue.front());
                m_taskQueue.pop();
                m_queuedTasks--;
            }
        }

        if (task) {
            m_isBusy = true;
            task();
            m_isBusy = false;
        }
    }
}

auto TaskQueue::isWorking() const -> bool { return m_isBusy || m_queuedTasks > 0; }

auto TaskQueue::getQueuedTaskCount() const -> size_t { return m_queuedTasks; }

auto TaskQueue::isProcessing() const -> bool { return m_isBusy; }

auto TaskQueue::isShutdown() const -> bool { return !m_running; }

void TaskQueue::waitForCompletion() const
{
    while (isWorking()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(LOOP_INTERVAL));
    }
}

void TaskQueue::shutdown()
{
    m_running = false;
    m_cv.notify_one();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}
