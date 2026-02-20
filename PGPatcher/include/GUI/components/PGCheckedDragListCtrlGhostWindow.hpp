#pragma once

#include <wx/wx.h>

#include <vector>

/**
 * @brief Semi-transparent floating window shown while dragging items in a PGCheckedDragListCtrl.
 *
 * Displays a ghost image of the dragged items that follows the mouse cursor during a drag operation.
 */
class PGCheckedDragListCtrlGhostWindow : public wxFrame {
private:
    std::vector<wxString> m_lines;

    static constexpr wxByte ALPHA = 200;
    static constexpr int PADDING = 8;

    static constexpr unsigned char DARK_GHOST_BOOST = 50;
    static constexpr int MAX_RGB_VALUE = 255;
    static inline wxColour s_GhostBackground = *wxWHITE; /** Base ghost background color for light mode */
    static inline wxColour s_GhostForeground = *wxBLACK;

public:
    /**
     * @brief Construct a new PGCheckedDragListCtrlGhostWindow.
     *
     * @param parent Parent window that owns this ghost window.
     * @param lines Lines of text to display in the ghost image.
     */
    PGCheckedDragListCtrlGhostWindow(wxWindow* parent,
                                     const std::vector<wxString>& lines);

    /**
     * @brief Render the ghost window contents.
     *
     * @param event The wxWidgets paint event (unused).
     */
    void OnPaint([[maybe_unused]] wxPaintEvent& event);

    /**
     * @brief Move the ghost window to follow the mouse cursor.
     *
     * @param pos New screen position to move the window to.
     */
    void updatePosition(const wxPoint& pos);
};
