#include "util/TaskPoolRunner.hpp"

#include "util/ExceptionHandler.hpp"
#include "util/Logger.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <cpptrace/from_current.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <thread>

// STATICS.
std::function<void()> TaskPoolRunner::s_exceptionCallback = nullptr;

TaskPoolRunner::TaskPoolRunner(const bool& multithread)
    : m_threadPool([] {
        auto availableThreads = std::thread::hardware_concurrency();
        if (availableThreads == 0)
            availableThreads = 4;
        return availableThreads - numStaticThreads;
    }())
    , m_multithread(multithread)
    , m_completedTasks(0)
{
}

void TaskPoolRunner::addTask(const std::function<void()>& task) { m_tasks.push_back(task); }

void TaskPoolRunner::runTasks()
{
    if (!m_multithread) {
        CPPTRACE_TRY
        {
            for (const auto& task : m_tasks) {
                task();
                m_completedTasks.fetch_add(1);
            }
        }
        CPPTRACE_CATCH(const std::exception& e)
        {
            ExceptionHandler::setException(e, cpptrace::from_current_exception().to_string());
            if (s_exceptionCallback)
                s_exceptionCallback();
        }

        return;
    }

    // Multithreading only beyond this point.
    for (const auto& task : m_tasks) {
        boost::asio::post(m_threadPool, [this, task] {
            if (ExceptionHandler::hasException()) {
                // Exception already thrown, don't run thread.
                return;
            }

            // Create log buffer.
            Logger::startThreadedBuffer();

            CPPTRACE_TRY
            {
                task();
                m_completedTasks.fetch_add(1);
            }
            CPPTRACE_CATCH(const std::exception& e)
            {
                ExceptionHandler::setException(e, cpptrace::from_current_exception().to_string());
                if (s_exceptionCallback)
                    s_exceptionCallback();
            }

            // Flush log buffer.
            Logger::flushThreadedBuffer();
        });
    }

    while (true) {
        // Check if all tasks are done.
        if (m_completedTasks.load() >= m_tasks.size()) {
            // All tasks done.
            break;
        }

        // If exception stop thread pool and throw.
        if (ExceptionHandler::hasException()) {
            m_threadPool.stop();
            m_threadPool.join();
            break;
        }

        // Sleep in between loops.
        std::this_thread::sleep_for(std::chrono::milliseconds(loopInterval));
    }
}

void TaskPoolRunner::setExceptionCallback(const std::function<void()>& callback) { s_exceptionCallback = callback; }
