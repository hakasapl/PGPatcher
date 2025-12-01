#pragma once

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>

#include <atomic>
#include <cstddef>
#include <functional>
#include <vector>

class ParallaxGenRunner {
private:
    boost::asio::thread_pool m_threadPool; /** Thread pool responsible for queueing tasks for multithreaded */

    const bool m_multithread; /** If true, run multithreaded */

    std::vector<std::function<void()>> m_tasks; /** Task list to run */
    std::atomic<size_t> m_completedTasks; /** Counter of completed tasks */

    static constexpr int LOOP_INTERVAL = 10; /** Task loop interval */

    static constexpr int NUM_STATIC_THREADS = 2; /** Number of static threads to reserve for system */

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
};
