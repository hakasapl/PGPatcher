#pragma once

#include <wx/event.h>

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
