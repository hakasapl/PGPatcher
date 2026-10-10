#pragma once

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>

#include <atomic>
#include <cstddef>
#include <functional>
#include <vector>

/**
 * @brief Runs a batch of independent tasks, either serially on the calling thread or on a thread pool.
 *
 * The pool has hardware_concurrency() - 2 threads (4 - 2 when the concurrency is unknown). A task that throws stops the
 * batch: the exception is recorded in ExceptionHandler, the exception callback runs and runTasks() returns early. Each
 * task runs with a Logger thread buffer so its log lines are not interleaved with those of other tasks.
 */
class TaskPoolRunner {
private:
    boost::asio::thread_pool m_threadPool; /** Thread pool responsible for queueing tasks for multithreaded */

    const bool m_multithread; /** If true, run multithreaded */

    std::vector<std::function<void()>> m_tasks; /** Task list to run */
    std::atomic<size_t> m_completedTasks; /** Counter of completed tasks */

    static constexpr int loopInterval = 10; /** Task loop interval */

    static constexpr int numStaticThreads = 2; /** Number of static threads to reserve for system */

    static std::function<void()> s_exceptionCallback; /** Exception callback function */

public:
    /**
     * @brief Constructs the runner and its thread pool.
     *
     * @param multithread If true, runTasks() posts the tasks to the thread pool, otherwise it runs them in order on the
     * calling thread.
     */
    explicit TaskPoolRunner(const bool& multithread = true);

    /**
     * @brief Adds a task to the batch. Tasks only start in runTasks().
     *
     * @param task Task to add.
     */
    void addTask(const std::function<void()>& task);

    /**
     * @brief Sets the callback invoked (on the throwing thread) when a task throws, for every runner.
     *
     * @param callback Callback, or an empty function to clear it.
     */
    static void setExceptionCallback(const std::function<void()>& callback);

    /**
     * @brief Runs every added task and blocks until all of them completed or one of them threw.
     *
     * Meant to be called once, from the thread that owns the runner.
     */
    void runTasks();
};
