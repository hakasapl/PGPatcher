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
     * @brief Logs the recorded exception (type, message and stack trace) as a critical error, if there is one.
     *
     * Despite the name nothing is rethrown. Must be called from the thread registered with setMainThread(); throws
     * std::runtime_error when called from another thread.
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
    static bool hasException();

    /**
     * @brief Forgets the recorded exception, so a new one can be recorded.
     *
     * A TaskPoolRunner runs tasks again afterwards; a TaskQueue whose worker thread already stopped stays stopped.
     */
    static void reset();
};
