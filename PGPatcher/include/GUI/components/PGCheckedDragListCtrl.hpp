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

#include "GUI/components/PGCheckedDragListCtrlGhostWindow.hpp"

class PGCheckedDragListCtrl : public wxListCtrl {
private:
    wxImageList* m_imagelist; /** Image list for checkboxes */

    wxTimer m_autoscrollTimer; /** Timer that is responsible for autoscroll */
    static constexpr int AUTOSCROLL_TIMER_INTERVAL = 250; /** Scroll every this amount in ms when autoscrolling */

    /**
     * @brief Struct that represents a row being dragged
     */
    struct Row {
        long index;
        wxString text;
    };

    int m_targetLineIndex = -1; /** Stores the index of the element where an element is being dropped */
    std::vector<Row> m_draggedRows; /** Stores rows currently being dragged */
    PGCheckedDragListCtrlGhostWindow* m_ghost; /** Ghost frame for render while dragging */

    int m_cutoffLine; /** Cutoff line, below which dragging is disabled */

public:
    /**
     * @brief Construct a new PGCheckedDragListCtrl object
     *
     * @param parent parent window
     * @param id window ID
     * @param pt position
     * @param sz size
     * @param style window style (default wxLC_REPORT)
     */
    PGCheckedDragListCtrl(wxWindow* parent, wxWindowID id, const wxPoint& pt = wxDefaultPosition,
        const wxSize& sz = wxDefaultSize, long style = wxLC_REPORT);

    /**
     * @brief Check if an item is checked
     *
     * @param item item index
     * @return true if checked
     * @return false if not checked
     */
    [[nodiscard]] auto isChecked(long item) const -> bool;

    /**
     * @brief Check or uncheck an item
     *
     * @param item item index
     * @param checked true to check, false to uncheck
     */
    void check(long item, bool checked);

    /**
     * @brief Set the Cutoff Line object
     *
     * @param index index of the cutoff line (-1 to disable cutoff)
     */
    void setCutoffLine(int index);

private:
    // Event Handlers

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
    void onAutoscrollTimer(wxTimerEvent& event);

    /**
     * @brief Event handler that triggers when the context menu is requested (right-click)
     *
     * @param event wxWidgets event object
     */
    void onContextMenu(wxContextMenuEvent& event);

    // Helpers

    /**
     * @brief Process an item being checked (movement about the cutoff line)
     *
     * @param item item index
     * @param checked true if checked, false if unchecked
     */
    void processCheckItem(long item, bool checked);

    /**
     * @brief Process multiple items being checked (movement about the cutoff line)
     *
     * @param items vector of item indices
     * @param checked true if checked, false if unchecked
     */
    void processCheckItems(const std::vector<long>& items, bool checked);

    /**
     * @brief Move an item from one index to another
     *
     * @param fromIndex index to move from
     * @param toIndex index to move to
     * @return long new index of the moved item
     */
    auto moveItem(long fromIndex, long toIndex) -> long;

    /**
     * @brief Move multiple items from one set of indices to a target index
     *
     * @param fromIndices vector of indices to move
     * @param toIndex target index to move to
     * @return std::vector<long> new indices of the moved items (in same order as fromIndices)
     */
    auto moveItems(const std::vector<long>& fromIndices, long toIndex) -> std::vector<long>;

    [[nodiscard]] auto getSelectedItems() const -> std::vector<long>;

    void clearAllSelections();
};
