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

class PGCheckedDragListCtrlEvtItemDragged;
wxDECLARE_EVENT(pgEVT_CDLC_ITEM_DRAGGED, PGCheckedDragListCtrlEvtItemDragged); // NOLINT(readability-identifier-naming)

class PGCheckedDragListCtrlEvtItemDragged : public wxCommandEvent {
private:
    long m_itemIndex;
    long m_newPosition;

public:
    PGCheckedDragListCtrlEvtItemDragged(long id = wxID_ANY, long item = -1, long pos = -1)
        : wxCommandEvent(pgEVT_CDLC_ITEM_DRAGGED, id)
        , m_itemIndex(item)
        , m_newPosition(pos)
    {
    }

    // Getters
    [[nodiscard]] auto getItemIndex() const -> long { return m_itemIndex; }
    [[nodiscard]] auto getNewPosition() const -> long { return m_newPosition; }

    // Required for sending with wxPostEvent
    [[nodiscard]] auto Clone() const -> wxEvent* override;
};
