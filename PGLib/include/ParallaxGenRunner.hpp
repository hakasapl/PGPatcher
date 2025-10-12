#pragma once

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>

#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <string>
#include <vector>

class ParallaxGenRunner {
private:
    boost::asio::thread_pool m_threadPool; /** Thread pool responsible for queueing tasks for multithreaded */

    const bool m_multithread; /** If true, run multithreaded */

    std::vector<std::function<void()>> m_tasks; /** Task list to run */
    std::atomic<size_t> m_completedTasks; /** Counter of completed tasks */

    static constexpr int LOOP_INTERVAL = 10; /** Task loop interval */

public:
    /**
     * @brief Construct a new Parallax Gen Runner object
     *
     * @param multithread if true, use multithreading
     */
    ParallaxGenRunner(const bool& multithread = true);

    /**
     * @brief Add a task to the task list
     *
     * @param task Task to add (function<void()>)
     */
    void addTask(const std::function<void()>& task);

    /**
     * @brief Blocking function that runs all tasks in the task list. Intended to be run from the main thread
     */
    void runTasks();

    /**
     * @brief Process an exception - prints stack trace and exception message, and throws a main thread exception (for
     * external callers)
     *
     * @param e Exception to process
     * @param stacktrace Stack trace of the exception
     */
    static void processException(const std::exception& e, const std::string& stacktrace);

private:
    /**
     * @brief Process an exception - prints stack trace and exception message, and throws a main thread exception
     *
     * @param e Exception to process
     * @param stacktrace Stack trace of the exception
     * @param externalCaller Set to false for internal, true for external
     */
    static void processException(const std::exception& e, const std::string& stackTrace, const bool& externalCaller);
};
