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

    static constexpr wxByte alpha = 200;
    static constexpr int paddingDIP = 8; /** Horizontal paddingDIP around the text in DIPs */
    static constexpr int textIndentDIP = 4; /** Left indent of the text in DIPs */
    static constexpr int lineSpacingDIP = 2; /** Vertical spacing around every line in DIPs */

    static constexpr unsigned char darkGhostBoost = 50;
    static constexpr int maxRGBValue = 255;
    static inline wxColour s_ghostBackground = *wxWHITE; /** Base ghost background color for light mode */
    static inline wxColour s_ghostForeground = *wxBLACK;

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
    void onPaint([[maybe_unused]] wxPaintEvent& event);

    /**
     * @brief Move the ghost window to follow the mouse cursor.
     *
     * @param pos New screen position to move the window to.
     */
    void updatePosition(const wxPoint& pos);
};
