#pragma once

#include <wx/app.h>
#include <wx/msgdlg.h>

#include <spdlog/sinks/base_sink.h>

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

            if (wxIsMainThread()) {
                wxMessageBox(wxMsg, "Critical Error", wxOK | wxICON_ERROR);
            } else {
                // Post to main thread safely
                wxTheApp->CallAfter([wxMsg]() -> auto { wxMessageBox(wxMsg, "Critical Error", wxOK | wxICON_ERROR); });
            }

            // Exit application
            wxTheApp->Exit();
        }
    }

    void flush_() override { }
};
