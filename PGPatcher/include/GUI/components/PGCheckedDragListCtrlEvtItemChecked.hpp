#pragma once

#include <wx/event.h>

class PGCheckedDragListCtrlEvtItemChecked;
wxDECLARE_EVENT(pgEVT_CDLC_ITEM_CHECKED, PGCheckedDragListCtrlEvtItemChecked); // NOLINT(readability-identifier-naming)

class PGCheckedDragListCtrlEvtItemChecked : public wxCommandEvent {
private:
    long m_itemIndex;
    bool m_checked;

public:
    PGCheckedDragListCtrlEvtItemChecked(long id = wxID_ANY, long item = -1, bool checked = false)
        : wxCommandEvent(pgEVT_CDLC_ITEM_CHECKED, id)
        , m_itemIndex(item)
        , m_checked(checked)
    {
    }

    // Getters
    [[nodiscard]] auto getItemIndex() const -> long { return m_itemIndex; }
    [[nodiscard]] auto isChecked() const -> bool { return m_checked; }

    // Required for sending with wxPostEvent
    [[nodiscard]] auto Clone() const -> wxEvent* override;
};
