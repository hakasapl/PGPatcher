#pragma once

#include <wx/wx.h>

#include <vector>

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
    PGCheckedDragListCtrlGhostWindow(wxWindow* parent, const std::vector<wxString>& lines);

    void OnPaint([[maybe_unused]] wxPaintEvent& event);

    void updatePosition(const wxPoint& pos);
};
