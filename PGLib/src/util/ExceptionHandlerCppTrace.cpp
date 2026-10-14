#include "util/ExceptionHandler.hpp"

#include <cpptrace/from_current.hpp>

#include <exception>
#include <functional>

bool ExceptionHandler::cppTraceGuard(const std::function<void()>& task)
{
    bool isCompleted = false;
    CPPTRACE_TRY
    {
        task();
        isCompleted = true;
    }
    CPPTRACE_CATCH(const std::exception& e) { setException(e, cpptrace::from_current_exception().to_string()); }

    return isCompleted;
}
