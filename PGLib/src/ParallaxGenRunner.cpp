#include "ParallaxGenRunner.hpp"

#include "util/Logger.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <cpptrace/from_current.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std;

ParallaxGenRunner::ParallaxGenRunner(const bool& multithread)
    : m_threadPool([]() -> unsigned int {
        auto availableThreads = std::thread::hardware_concurrency();
        if (availableThreads == 0) {
            availableThreads = 4;
        }
        return availableThreads - NUM_STATIC_THREADS;
    }())
    , m_multithread(multithread)
    , m_completedTasks(0)
{
}

void ParallaxGenRunner::addTask(const function<void()>& task) { m_tasks.push_back(task); }

void ParallaxGenRunner::runTasks()
{
    if (!m_multithread) {
        CPPTRACE_TRY
        {
            for (const auto& task : m_tasks) {
                task();
                m_completedTasks.fetch_add(1);
            }
        }
        CPPTRACE_CATCH(const exception& e)
        {
            processException(e, cpptrace::from_current_exception().to_string(), false);
        }

        return;
    }

    std::atomic<bool> exceptionThrown;
    std::exception exception;
    std::string exceptionStackTrace;
    std::mutex exceptionMutex;

    // Multithreading only beyond this point
    for (const auto& task : m_tasks) {
        boost::asio::post(
            m_threadPool, [this, task, &exceptionThrown, &exception, &exceptionStackTrace, &exceptionMutex] {
                if (exceptionThrown.load()) {
                    // Exception already thrown, don't run thread
                    return;
                }

                // Create log buffer
                Logger::startThreadedBuffer();

                CPPTRACE_TRY
                {
                    task();
                    m_completedTasks.fetch_add(1);
                }
                CPPTRACE_CATCH(const class exception& e)
                {
                    if (!exceptionThrown.load()) {
                        const lock_guard<mutex> lock(exceptionMutex);
                        exception = e;
                        exceptionStackTrace = cpptrace::from_current_exception().to_string();
                        exceptionThrown.store(true);
                    }
                }

                // Flush log buffer
                Logger::flushThreadedBuffer();
            });
    }

    while (true) {
        // Check if all tasks are done
        if (m_completedTasks.load() >= m_tasks.size()) {
            // All tasks done
            break;
        }

        // If exception stop thread pool and throw
        if (exceptionThrown.load()) {
            m_threadPool.stop();
            processException(exception, exceptionStackTrace, false);
        }

        // Sleep in between loops
        this_thread::sleep_for(chrono::milliseconds(LOOP_INTERVAL));
    }
}

void ParallaxGenRunner::processException(const exception& e, const string& stacktrace)
{
    processException(e, stacktrace, true);
}

void ParallaxGenRunner::processException(
    const std::exception& e, const std::string& stacktrace, const bool& externalCaller)
{
    if (string(e.what()) == "PGRUNNERINTERNAL") {
        // Internal exception, don't print
        return;
    }

    Logger::critical("An unhandled exception occured. Please provide your full log in the bug report.\nException type: "
                     "\"{}\" / Message: \"{}\"\n{}",
        typeid(e).name(), e.what(), stacktrace);

    if (!externalCaller) {
        // Internal exception, throw on main thread
        throw runtime_error("PGRUNNERINTERNAL");
    }
}
