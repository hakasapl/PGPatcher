#pragma once

#include <atomic>
#include <exception>
#include <mutex>
#include <string>
#include <thread>

/**
 * @brief Thread-safe handler for propagating exceptions from worker threads to the main thread.
 *
 * Worker threads call setException() to record an unhandled exception. The main thread
 * periodically calls throwExceptionOnMainThread() or hasException() to detect and report it.
 */
class ExceptionHandler {
private:
    static std::thread::id s_mainThreadId;

    static std::atomic<bool> s_exceptionThrown;
    static std::mutex s_exceptionMutex;
    static std::exception s_exception;
    static std::string s_exceptionStackTrace;

public:
    /**
     * @brief Records the calling thread as the main thread for later validation.
     */
    static void setMainThread();

    /**
     * @brief Logs a critical error message if an exception has been recorded, then returns.
     *
     * Must be called from the main thread; throws std::runtime_error if called from another thread.
     */
    static void throwExceptionOnMainThread();

    /**
     * @brief Stores an exception and its stack trace so the main thread can report it.
     *
     * Only the first exception is stored; subsequent calls are silently ignored.
     *
     * @param e The exception to store.
     * @param stackTrace Human-readable stack trace string associated with the exception.
     */
    static void setException(const std::exception& e,
                             const std::string& stackTrace);

    /**
     * @brief Returns whether an exception has been recorded by any worker thread.
     *
     * @return true if setException() has been called at least once, false otherwise.
     */
    static auto hasException() -> bool;
};
