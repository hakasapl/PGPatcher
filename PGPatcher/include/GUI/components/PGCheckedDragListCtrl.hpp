#pragma once

#include "GUI/components/PGCheckedDragListCtrlGhostWindow.hpp"

#include <vector>

#include <wx/listctrl.h>
#include <wx/wx.h>

class PGCheckedDragListCtrl : public wxListCtrl {
private:
    wxImageList* m_imagelist; /** Image list for checkboxes */

    bool m_draggingEnabled = true; /** True if user can drag, false otherwise */

    wxTimer m_autoscrollTimer; /** Timer that is responsible for autoscroll */
    static constexpr int AUTOSCROLL_TIMER_INTERVAL = 250; /** Scroll every this amount in ms when autoscrolling */

    static inline const wxColor s_DisabledTextColor = wxColour(50, 50, 50); /** Color for disabled (unchecked) items */

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

    int m_cutoffLine = -1; /** Cutoff line, below which dragging is disabled */

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
     * @brief Destroy the PGCheckedDragListCtrl object
     */
    ~PGCheckedDragListCtrl() override;

    // Disable copy and move constructors and assignment operators
    PGCheckedDragListCtrl(const PGCheckedDragListCtrl&) = delete;
    auto operator=(const PGCheckedDragListCtrl&) -> PGCheckedDragListCtrl& = delete;
    PGCheckedDragListCtrl(PGCheckedDragListCtrl&&) = delete;
    auto operator=(PGCheckedDragListCtrl&&) -> PGCheckedDragListCtrl& = delete;

    /**
     * @brief Check or uncheck an item
     *
     * @param item item index
     * @param checked true to check, false to uncheck
     */
    void check(long item, bool checked);

    /**
     * @brief Check if an item is checked
     *
     * @param item item index
     * @return true if checked
     * @return false if not checked
     */
    [[nodiscard]] auto isChecked(long item) const -> bool;

    /**
     * @brief Ignore or unignore meshes from a mod (item)
     *
     * @param item item index
     * @param ignore true to ignore, false to unignore
     */
    void ignoreMeshes(long item, bool ignore);

    /**
     * @brief Check if meshes from a mod (item) are ignored
     *
     * @param item item index
     * @return true if ignored
     * @return false if not ignored
     */
    [[nodiscard]] auto areMeshesIgnored(long item) const -> bool;

    /**
     * @brief Set the Cutoff Line object
     *
     * @param index index of the cutoff line (-1 to disable cutoff)
     */
    void setCutoffLine(int index);

    /**
     * @brief Get the Cutoff Line object
     *
     * @return int index of the cutoff line (-1 if disabled)
     */
    [[nodiscard]] auto getCutoffLine() const -> int;

    /**
     * @brief Set the Dragging Enabled object
     *
     * @param enabled true to enable dragging, false to disable
     */
    void setDraggingEnabled(bool enabled);

    /**
     * @brief Get the Dragging Enabled object
     *
     * @return true if dragging is enabled
     * @return false if dragging is disabled
     */
    [[nodiscard]] auto isDraggingEnabled() const -> bool;

private:
    // Event Handlers

    /**
     * @brief Event handler that triggers when the left mouse button is pressed down (dragging or checking)
     *
     * @param event wxWidgets event object
     */
    void onMouseLeftDown(wxMouseEvent& event);

    /**
     * @brief Event handler that triggers when the mouse is moved (dragging or checking)
     *
     * @param event wxWidgets event object
     */
    void onMouseMotion(wxMouseEvent& event);

    /**
     * @brief Event handler that triggers when the left mouse button is released (dragging or checking)
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

    /**
     * @brief Get currently selected items in the list
     *
     * @return std::vector<long> vector of selected item indices
     */
    [[nodiscard]] auto getSelectedItems() const -> std::vector<long>;

    /**
     * @brief Clear all selections in the list
     */
    void clearAllSelections();
};
