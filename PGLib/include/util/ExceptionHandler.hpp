#pragma once

#include <atomic>
#include <exception>
#include <mutex>
#include <string>
#include <thread>

class ExceptionHandler {
private:
    static std::thread::id s_mainThreadId;

    static std::atomic<bool> s_exceptionThrown;
    static std::mutex s_exceptionMutex;
    static std::exception s_exception;
    static std::string s_exceptionStackTrace;

public:
    static void setMainThread();
    static void throwExceptionOnMainThread();

    static void setException(const std::exception& e, const std::string& stackTrace);
    static auto hasException() -> bool;
};
