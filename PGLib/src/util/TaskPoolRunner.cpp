#include "util/TaskPoolRunner.hpp"

#include "util/ExceptionHandler.hpp"
#include "util/Logger.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

// STATICS.
std::function<void()> TaskPoolRunner::s_exceptionCallback = nullptr;

TaskPoolRunner::TaskPoolRunner(const bool& multithread)
    : m_multithread(multithread)
    , m_threadCount(poolThreadCount(std::thread::hardware_concurrency()))
    , m_completedTasks(0)
{
}

unsigned TaskPoolRunner::poolThreadCount(unsigned hardwareConcurrency)
{
    const unsigned available = hardwareConcurrency ? hardwareConcurrency : defaultConcurrency;
    return available > numStaticThreads ? available - numStaticThreads : 1;
}

void TaskPoolRunner::addTask(const std::function<void()>& task) { m_tasks.push_back(task); }

void TaskPoolRunner::runTasks()
{
    if (!m_multithread) {
        for (const auto& task : m_tasks) {
            if (!ExceptionHandler::runGuarded(task)) {
                // The exception is recorded; the rest of the batch is skipped.
                if (s_exceptionCallback)
                    s_exceptionCallback();
                return;
            }

            m_completedTasks.fetch_add(1);
        }

        return;
    }

    // Multithreading only beyond this point. Every worker takes the next task until none is left. An exception, from
    // one of the tasks or recorded before the batch started, skips every task that did not start yet.
    std::atomic<size_t> nextTask { 0 };
    const auto worker = [this, &nextTask] {
        while (true) {
            const size_t taskIndex = nextTask.fetch_add(1);
            if (taskIndex >= m_tasks.size())
                break;

            if (ExceptionHandler::hasException()) {
                // Exception already thrown, don't run the task.
                continue;
            }

            // Create log buffer.
            Logger::startThreadedBuffer();

            if (ExceptionHandler::runGuarded(m_tasks.at(taskIndex)))
                m_completedTasks.fetch_add(1);
            else if (s_exceptionCallback)
                s_exceptionCallback();

            // Flush log buffer.
            Logger::flushThreadedBuffer();
        }
    };

    std::vector<std::jthread> threads;
    threads.reserve(m_threadCount);
    for (unsigned i = 0; i < m_threadCount; i++)
        threads.emplace_back(worker);

    for (auto& thread : threads)
        thread.join();
}

void TaskPoolRunner::setExceptionCallback(const std::function<void()>& callback) { s_exceptionCallback = callback; }
