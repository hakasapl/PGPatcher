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

class DragGhostWindow : public wxFrame {
private:
    std::vector<wxString> m_lines;

    static constexpr wxByte ALPHA = 200;
    static constexpr int PADDING = 8;

public:
    DragGhostWindow(wxWindow* parent, const std::vector<wxString>& lines);

    void OnPaint([[maybe_unused]] wxPaintEvent& event);

    void updatePosition(const wxPoint& pos);
};
