#pragma once

#include <vector>
#include <wx/wx.h>

class PGCheckedDragListCtrlGhostWindow : public wxFrame {
private:
    std::vector<wxString> m_lines;

    static constexpr wxByte ALPHA = 200;
    static constexpr int PADDING = 8;

public:
    PGCheckedDragListCtrlGhostWindow(wxWindow* parent, const std::vector<wxString>& lines);

    void OnPaint([[maybe_unused]] wxPaintEvent& event);

    void updatePosition(const wxPoint& pos);
};
