#pragma once

#include <wx/arrstr.h>
#include <wx/dnd.h>
#include <wx/dragimag.h>
#include <wx/gdicmn.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/msw/textctrl.h>
#include <wx/overlay.h>
#include <wx/renderer.h>
#include <wx/sizer.h>
#include <wx/wx.h>

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
