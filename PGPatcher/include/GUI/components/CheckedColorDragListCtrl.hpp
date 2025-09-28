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

#include "GUI/components/DragGhostWindow.hpp"

// Custom events
class ItemDraggedEvent;
wxDECLARE_EVENT(pgEVT_CCDLC_ITEM_DRAGGED, ItemDraggedEvent); // NOLINT(readability-identifier-naming)

class ItemCheckedEvent;
wxDECLARE_EVENT(pgEVT_CCDLC_ITEM_CHECKED, ItemCheckedEvent); // NOLINT(readability-identifier-naming)

class ItemDraggedEvent : public wxCommandEvent {
private:
    long m_itemIndex;
    long m_newPosition;

public:
    ItemDraggedEvent(long id = wxID_ANY, long item = -1, long pos = -1)
        : wxCommandEvent(pgEVT_CCDLC_ITEM_DRAGGED, id)
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

class ItemCheckedEvent : public wxCommandEvent {
private:
    long m_itemIndex;
    bool m_checked;

public:
    ItemCheckedEvent(long id = wxID_ANY, long item = -1, bool checked = false)
        : wxCommandEvent(pgEVT_CCDLC_ITEM_CHECKED, id)
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

class CheckedColorDragListCtrl : public wxListCtrl {
private:
    struct Row {
        long index;
        wxString text;
    };

    wxImageList* m_imagelist;

    wxTimer m_scrollTimer; /** Timer that is responsible for autoscroll */

    constexpr static int TIMER_INTERVAL = 250;

    //
    // Dragging
    //
    int m_targetLineIndex = -1; /** Stores the index of the element where an element is being dropped */

    std::vector<Row> m_draggedRows;
    DragGhostWindow* m_ghost;

    int m_cutoffLine;

    static constexpr int AUTOSCROLL_MARGIN = 30;

public:
    CheckedColorDragListCtrl(wxWindow* parent, wxWindowID id, const wxPoint& pt = wxDefaultPosition,
        const wxSize& sz = wxDefaultSize, long style = wxLC_REPORT);

    [[nodiscard]] auto isChecked(long item) const -> bool;

    void check(long item, bool checked);

    void setCutoffLine(int index);
    auto moveItem(long fromIndex, long toIndex) -> long;
    auto moveItems(const std::vector<long>& fromIndices, long toIndex) -> std::vector<long>;

private:
    /**
     * @brief Event handler that triggers when the left mouse button is pressed down (dragging)
     *
     * @param event wxWidgets event object
     */
    void onMouseLeftDown(wxMouseEvent& event);

    /**
     * @brief Event handler that triggers when the mouse is moved (dragging)
     *
     * @param event wxWidgets event object
     */
    void onMouseMotion(wxMouseEvent& event);

    /**
     * @brief Event handler that triggers when the left mouse button is released (dragging)
     *
     * @param event wxWidgets event object
     */
    void onMouseLeftUp(wxMouseEvent& event);

    /**
     * @brief Event handler that triggers from timer for autoscrolling while dragging
     *
     * @param event wxWidgets event object
     */
    void onTimer(wxTimerEvent& event);

    void onContextMenu(wxContextMenuEvent& event);

    void processCheckItem(long item, bool checked);
    void processCheckItems(const std::vector<long>& items, bool checked);
};
