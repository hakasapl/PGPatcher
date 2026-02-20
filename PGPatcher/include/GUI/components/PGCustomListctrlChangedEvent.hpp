#pragma once

#include <wx/event.h>

class PGCustomListctrlChangedEvent;
wxDECLARE_EVENT(pgEVT_LISTCTRL_CHANGED, // NOLINT(readability-identifier-naming)
                PGCustomListctrlChangedEvent);

/**
 * @brief Custom wxCommandEvent fired when the contents of a PG list control change.
 */
class PGCustomListctrlChangedEvent : public wxCommandEvent {
private:
    long m_itemIndex;

public:
    /**
     * @brief Construct a new PGCustomListctrlChangedEvent.
     *
     * @param id Window or event ID.
     * @param item Item index that changed.
     */
    PGCustomListctrlChangedEvent(long id = wxID_ANY,
                                 long item = -1)
        : wxCommandEvent(pgEVT_LISTCTRL_CHANGED,
                         id)
        , m_itemIndex(item)
    {
    }

    // Getters
    /**
     * @brief Get the index of the item that changed.
     *
     * @return Index of the changed item.
     */
    [[nodiscard]] auto getItemIndex() const -> long { return m_itemIndex; }

    // Required for sending with wxPostEvent
    /**
     * @brief Create a heap-allocated copy of this event (required for wxPostEvent).
     *
     * @return Pointer to a cloned copy of this event.
     */
    [[nodiscard]] auto Clone() const -> wxEvent* override;
};
