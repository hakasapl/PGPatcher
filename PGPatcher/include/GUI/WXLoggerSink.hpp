#pragma once

#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/base_sink.h>
#include <wx/app.h>
#include <wx/msgdlg.h>
#include <wx/string.h>

#include <fmt/format.h>

template <typename Mutex> class WXLoggerSink : public spdlog::sinks::base_sink<Mutex> {
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
            wxTheApp->Exit();
        }
    }

    void flush_() override { }
};
