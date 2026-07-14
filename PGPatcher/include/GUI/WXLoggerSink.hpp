#pragma once

#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/base_sink.h>
#include <wx/app.h>
#include <wx/msgdlg.h>
#include <wx/string.h>

#include <cstddef>
#include <cstdlib>
#include <fmt/format.h>
#include <mutex>
#include <vector>

/**
 * @brief spdlog sink that captures log messages for display in the wxWidgets UI.
 *
 * Critical-level messages trigger an immediate modal wxMessageBox and call exit(1).
 * Error-level messages are collected and accessible via getErrorMessages().
 * Warning-level messages are collected and accessible via getWarningMessages().
 * All other levels are silently discarded.
 *
 * @tparam Mutex Mutex type used by the base spdlog sink (e.g., std::mutex).
 */
template <typename Mutex> class WXLoggerSink : public spdlog::sinks::base_sink<Mutex> {
private:
    std::vector<wxString> m_errorMessages;
    std::vector<wxString> m_warningMessages;

    // Message counts captured at the start of the patching step. Used to discard
    // messages of a previous patch run when the patching step is re-run.
    size_t m_runStartErrorCount = 0;
    size_t m_runStartWarningCount = 0;

protected:
    /**
     * @brief Handle a single log message from spdlog.
     *
     * Formats the message and, depending on its level, shows a message box (critical),
     * stores it in the error list (err), or stores it in the warning list (warn).
     *
     * @param msg The log message details provided by spdlog.
     */
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        // Format message
        spdlog::memory_buf_t formatted;
        this->formatter_->format(msg, formatted);

        if (msg.level == spdlog::level::critical) {
            // Convert to wxString
            const wxString wxMsg = wxString::FromUTF8(fmt::to_string(formatted).c_str());

            wxMessageBox(wxMsg, "Critical Error", wxOK | wxICON_ERROR);
            exit(1);
        } else if (msg.level == spdlog::level::err) {
            // Convert to wxString
            const wxString wxMsg = wxString::FromUTF8(fmt::to_string(formatted).c_str());
            m_errorMessages.push_back(wxMsg);
        } else if (msg.level == spdlog::level::warn) {
            // Convert to wxString
            const wxString wxMsg = wxString::FromUTF8(fmt::to_string(formatted).c_str());
            m_warningMessages.push_back(wxMsg);
        }
    }

    /**
     * @brief Flush buffered log output (no-op for this sink).
     */
    void flush_() override { }

public:
    /**
     * @brief Check whether any error-level messages have been collected.
     *
     * @return true if at least one error message has been captured, false otherwise.
     */
    [[nodiscard]] auto hasErrors() -> bool
    {
        std::scoped_lock<Mutex> lock(this->mutex_);
        return !m_errorMessages.empty();
    }
    /**
     * @brief Check whether any warning-level messages have been collected.
     *
     * @return true if at least one warning message has been captured, false otherwise.
     */
    [[nodiscard]] auto hasWarnings() -> bool
    {
        std::scoped_lock<Mutex> lock(this->mutex_);
        return !m_warningMessages.empty();
    }

    /**
     * @brief Retrieve all collected error-level messages.
     *
     * @return Vector of formatted error messages as wxStrings.
     */
    [[nodiscard]] auto getErrorMessages() -> std::vector<wxString>
    {
        std::scoped_lock<Mutex> lock(this->mutex_);
        return m_errorMessages;
    }
    /**
     * @brief Retrieve all collected warning-level messages.
     *
     * @return Vector of formatted warning messages as wxStrings.
     */
    [[nodiscard]] auto getWarningMessages() -> std::vector<wxString>
    {
        std::scoped_lock<Mutex> lock(this->mutex_);
        return m_warningMessages;
    }

    /**
     * @brief Record the current message counts as the patch-run baseline.
     *
     * Call this after preparation completes, right before the patching step. Messages
     * collected up to this point (preparation-phase messages) are preserved by
     * resetToRunStart().
     */
    void markRunStart()
    {
        std::scoped_lock<Mutex> lock(this->mutex_);
        m_runStartErrorCount = m_errorMessages.size();
        m_runStartWarningCount = m_warningMessages.size();
    }

    /**
     * @brief Discard all messages collected after the last markRunStart() call.
     *
     * Call this when the patching step is re-run so messages from the previous patch
     * run do not linger while preparation-phase messages are kept.
     */
    void resetToRunStart()
    {
        std::scoped_lock<Mutex> lock(this->mutex_);
        if (m_errorMessages.size() > m_runStartErrorCount) {
            m_errorMessages.resize(m_runStartErrorCount);
        }
        if (m_warningMessages.size() > m_runStartWarningCount) {
            m_warningMessages.resize(m_runStartWarningCount);
        }
    }
};
