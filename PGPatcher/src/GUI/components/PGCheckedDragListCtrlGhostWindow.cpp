#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/gdicmn.h>

#include "GUI/components/PGCheckedDragListCtrlGhostWindow.hpp"

PGCheckedDragListCtrlGhostWindow::PGCheckedDragListCtrlGhostWindow(wxWindow* parent, const std::vector<wxString>& lines)
    : wxFrame(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
          wxFRAME_SHAPED | wxBORDER_NONE | wxSTAY_ON_TOP)
    , m_lines(lines)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetTransparent(ALPHA); // semi-transparent

    // Compute size based on text
    wxClientDC dc(this);
    dc.SetFont(GetFont());

    int width = 0;
    int height = 0;
    for (const auto& line : m_lines) {
        const wxSize sz = dc.GetTextExtent(line);
        width = std::max(width, sz.x + PADDING); // horizontal padding
        height += sz.y + 2; // vertical padding
    }
    SetSize(width, height);

    Bind(wxEVT_PAINT, &PGCheckedDragListCtrlGhostWindow::OnPaint, this);

    Show();
}

void PGCheckedDragListCtrlGhostWindow::OnPaint([[maybe_unused]] wxPaintEvent& event)
{
    wxPaintDC dc(this);
    dc.SetPen(*wxTRANSPARENT_PEN); // Disables black border
    dc.SetBrush(*wxWHITE_BRUSH);

    const wxSize sz = GetClientSize();
    dc.DrawRectangle(0, 0, sz.x, sz.y);

    int offsetY = 2;
    for (const auto& line : m_lines) {
        dc.DrawText(line, 4, offsetY);
        offsetY += dc.GetTextExtent(line).y + 2;
    }
}

void PGCheckedDragListCtrlGhostWindow::updatePosition(const wxPoint& pos) { Move(pos); }
