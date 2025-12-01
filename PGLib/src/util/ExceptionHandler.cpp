#include "util/ExceptionHandler.hpp"

#include "util/Logger.hpp"

#include <atomic>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

// Statics
std::thread::id ExceptionHandler::s_mainThreadId;
std::atomic<bool> ExceptionHandler::s_exceptionThrown { false };
std::mutex ExceptionHandler::s_exceptionMutex;
std::exception ExceptionHandler::s_exception;
std::string ExceptionHandler::s_exceptionStackTrace;

void ExceptionHandler::setMainThread() { s_mainThreadId = std::this_thread::get_id(); }

void ExceptionHandler::throwExceptionOnMainThread()
{
    if (s_mainThreadId != std::this_thread::get_id()) {
        throw std::runtime_error("throwExceptionOnMainThread called from non-main thread");
    }

    if (s_exceptionThrown.load()) {
        // only log critical message if an exception has been thrown
        const std::scoped_lock lock(s_exceptionMutex);
        Logger::critical(
            "An unhandled exception occurred. Please provide your full log in the bug report.\nException type: "
            "\"{}\" / Message: \"{}\"\n{}",
            typeid(s_exception).name(), s_exception.what(), s_exceptionStackTrace);
    }
}

void ExceptionHandler::setException(const std::exception& e, const std::string& stackTrace)
{
    const std::scoped_lock lock(s_exceptionMutex);
    if (!s_exceptionThrown.load()) {
        s_exception = e;
        s_exceptionStackTrace = stackTrace;
        s_exceptionThrown.store(true);
    }
}

auto ExceptionHandler::hasException() -> bool { return s_exceptionThrown.load(); }
