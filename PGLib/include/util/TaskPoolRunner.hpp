#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <vector>

/**
 * @brief Runs a batch of independent tasks, either serially on the calling thread or on a pool of threads.
 *
 * The pool has poolThreadCount() threads, which runTasks() starts and joins. Every task runs through the task guard of
 * ExceptionHandler (runGuarded()); when a task throws, the exception callback runs and, if the guard recorded the
 * exception (the offline guard does), the tasks that did not start yet are skipped. A guard that only reports the
 * exception (the PGLite runtime's) lets the rest of the batch run. Each task runs with a Logger thread buffer so its
 * log lines are not interleaved with those of other tasks.
 */
class TaskPoolRunner {
private:
    bool m_multithread; /** If true, run multithreaded */
    unsigned m_threadCount; /** Number of pool threads */

    std::vector<std::function<void()>> m_tasks; /** Task list to run */

    static constexpr unsigned numStaticThreads = 2; /** Number of static threads to reserve for system */

    static constexpr unsigned defaultConcurrency = 4; /** Assumed concurrency when the hardware does not report one */

    static std::function<void()> s_exceptionCallback; /** Exception callback function */

public:
    /**
     * @brief Constructs the runner.
     *
     * @param multithread If true, runTasks() runs the tasks on the thread pool, otherwise it runs them in order on the
     * calling thread.
     */
    explicit TaskPoolRunner(const bool& multithread = true);

    /**
     * @brief Number of pool threads for a hardware concurrency: two less than the concurrency, but at least one.
     *
     * @param hardwareConcurrency std::thread::hardware_concurrency(), 0 (unknown) counts as four.
     */
    static unsigned poolThreadCount(unsigned hardwareConcurrency);

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
     * @brief Runs every added task and blocks until all of them completed, or until one of them threw and the guard
     * recorded it.
     *
     * Meant to be called once, from the thread that owns the runner.
     */
    void runTasks();
};
