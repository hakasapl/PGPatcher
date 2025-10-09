#pragma once

#include <wx/event.h>

class PGCustomListctrlChangedEvent;
wxDECLARE_EVENT(pgEVT_LISTCTRL_CHANGED, PGCustomListctrlChangedEvent); // NOLINT(readability-identifier-naming)

class PGCustomListctrlChangedEvent : public wxCommandEvent {
private:
    long m_itemIndex;

public:
    PGCustomListctrlChangedEvent(long id = wxID_ANY, long item = -1)
        : wxCommandEvent(pgEVT_LISTCTRL_CHANGED, id)
        , m_itemIndex(item)
    {
    }

    // Getters
    [[nodiscard]] auto getItemIndex() const -> long { return m_itemIndex; }

    // Required for sending with wxPostEvent
    [[nodiscard]] auto Clone() const -> wxEvent* override;
};
