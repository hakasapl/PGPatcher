#pragma once

#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/base_sink.h>
#include <wx/app.h>
#include <wx/msgdlg.h>
#include <wx/string.h>

#include <cstdlib>
#include <fmt/format.h>
#include <mutex>
#include <vector>

template <typename Mutex> class WXLoggerSink : public spdlog::sinks::base_sink<Mutex> {
private:
    std::vector<wxString> m_errorMessages;
    std::vector<wxString> m_warningMessages;

protected:
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

    void flush_() override { }

public:
    [[nodiscard]] auto hasErrors() -> bool
    {
        std::scoped_lock<Mutex> lock(this->mutex_);
        return !m_errorMessages.empty();
    }
    [[nodiscard]] auto hasWarnings() -> bool
    {
        std::scoped_lock<Mutex> lock(this->mutex_);
        return !m_warningMessages.empty();
    }

    [[nodiscard]] auto getErrorMessages() -> std::vector<wxString>
    {
        std::scoped_lock<Mutex> lock(this->mutex_);
        return m_errorMessages;
    }
    [[nodiscard]] auto getWarningMessages() -> std::vector<wxString>
    {
        std::scoped_lock<Mutex> lock(this->mutex_);
        return m_warningMessages;
    }
};
