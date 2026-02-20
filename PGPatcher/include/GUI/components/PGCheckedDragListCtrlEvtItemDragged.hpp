#pragma once

#include <wx/event.h>

class PGCheckedDragListCtrlEvtItemDragged;
wxDECLARE_EVENT(pgEVT_CDLC_ITEM_DRAGGED, // NOLINT(readability-identifier-naming)
                PGCheckedDragListCtrlEvtItemDragged);

/**
 * @brief Custom wxCommandEvent fired when an item is dragged to a new position in a PGCheckedDragListCtrl.
 */
class PGCheckedDragListCtrlEvtItemDragged : public wxCommandEvent {
private:
    long m_itemIndex;
    long m_newPosition;

public:
    /**
     * @brief Construct a new PGCheckedDragListCtrlEvtItemDragged event.
     *
     * @param id Window or event ID.
     * @param item Item index of the dragged item.
     * @param pos New position where the item was dropped.
     */
    PGCheckedDragListCtrlEvtItemDragged(long id = wxID_ANY,
                                        long item = -1,
                                        long pos = -1)
        : wxCommandEvent(pgEVT_CDLC_ITEM_DRAGGED,
                         id)
        , m_itemIndex(item)
        , m_newPosition(pos)
    {
    }

    // Getters
    /**
     * @brief Get the index of the dragged item.
     *
     * @return Index of the dragged item.
     */
    [[nodiscard]] auto getItemIndex() const -> long { return m_itemIndex; }
    /**
     * @brief Get the new position where the item was dropped.
     *
     * @return New position of the item.
     */
    [[nodiscard]] auto getNewPosition() const -> long { return m_newPosition; }

    // Required for sending with wxPostEvent
    /**
     * @brief Create a heap-allocated copy of this event (required for wxPostEvent).
     *
     * @return Pointer to a cloned copy of this event.
     */
    [[nodiscard]] auto Clone() const -> wxEvent* override;
};
