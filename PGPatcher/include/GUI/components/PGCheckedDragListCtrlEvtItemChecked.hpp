#pragma once

#include <wx/event.h>

class PGCheckedDragListCtrlEvtItemChecked;
wxDECLARE_EVENT(pgEVT_CDLC_ITEM_CHECKED, // NOLINT(readability-identifier-naming)
                PGCheckedDragListCtrlEvtItemChecked);

/**
 * @brief Custom wxCommandEvent fired when an item is checked or unchecked in a PGCheckedDragListCtrl.
 */
class PGCheckedDragListCtrlEvtItemChecked : public wxCommandEvent {
private:
    long m_itemIndex;
    bool m_checked;

public:
    /**
     * @brief Construct a new PGCheckedDragListCtrlEvtItemChecked event.
     *
     * @param id Window or event ID.
     * @param item Item index of the checked item.
     * @param checked Whether the item is now checked or unchecked.
     */
    PGCheckedDragListCtrlEvtItemChecked(long id = wxID_ANY,
                                        long item = -1,
                                        bool checked = false)
        : wxCommandEvent(pgEVT_CDLC_ITEM_CHECKED,
                         id)
        , m_itemIndex(item)
        , m_checked(checked)
    {
    }

    // Getters
    /**
     * @brief Get the index of the checked item.
     *
     * @return Index of the checked item.
     */
    [[nodiscard]] auto getItemIndex() const -> long { return m_itemIndex; }
    /**
     * @brief Check whether the item is now checked.
     *
     * @return True if the item is checked, false if unchecked.
     */
    [[nodiscard]] auto isChecked() const -> bool { return m_checked; }

    // Required for sending with wxPostEvent
    /**
     * @brief Create a heap-allocated copy of this event (required for wxPostEvent).
     *
     * @return Pointer to a cloned copy of this event.
     */
    [[nodiscard]] auto Clone() const -> wxEvent* override;
};
