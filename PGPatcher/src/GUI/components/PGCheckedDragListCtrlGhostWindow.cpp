#include "GUI/components/PGCheckedDragListCtrlGhostWindow.hpp"
#include "PGPatcherGlobals.hpp"

#include <algorithm>
#include <vector>

PGCheckedDragListCtrlGhostWindow::PGCheckedDragListCtrlGhostWindow(wxWindow* parent,
                                                                   const std::vector<wxString>& lines)
    : wxFrame(parent,
              wxID_ANY,
              wxEmptyString,
              wxDefaultPosition,
              wxDefaultSize,
              wxFRAME_SHAPED | wxBORDER_NONE | wxSTAY_ON_TOP)
    , m_lines(lines)
{
    //
    // DARK MODE Adjustments.
    //
    if (PGPatcherGlobals::isDarkMode()) {
        const static auto selfColor = GetBackgroundColour();
        s_ghostBackground = wxColour(std::min(selfColor.Red() + darkGhostBoost, maxRGBValue),
                                     std::min(selfColor.Green() + darkGhostBoost, maxRGBValue),
                                     std::min(selfColor.Blue() + darkGhostBoost, maxRGBValue));
        s_ghostForeground = *wxWHITE;
    } else {
        s_ghostBackground = *wxWHITE;
        s_ghostForeground = *wxBLACK;
    }

    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetTransparent(alpha); // semi-transparent

    // Compute size based on text (sizes in DIPs, scaled to the monitor's DPI).
    wxClientDC dc(this);
    dc.SetFont(GetFont());

    const int padding = FromDIP(paddingDIP);
    const int lineSpacing = FromDIP(lineSpacingDIP);
    int width = 0;
    int height = 0;
    for (const auto& line : m_lines) {
        const wxSize sz = dc.GetTextExtent(line);
        width = std::max(width, sz.x + padding); // horizontal padding
        height += sz.y + lineSpacing; // vertical padding
    }
    SetSize(width, height);

    Bind(wxEVT_PAINT, &PGCheckedDragListCtrlGhostWindow::OnPaint, this);

    Show();
}

void PGCheckedDragListCtrlGhostWindow::OnPaint([[maybe_unused]] wxPaintEvent& event)
{

    wxPaintDC dc(this);
    dc.SetPen(*wxTRANSPARENT_PEN); // Disables black border
    dc.SetBrush(s_ghostBackground);
    dc.SetTextForeground(s_ghostForeground);

    const wxSize sz = GetClientSize();
    dc.DrawRectangle(0, 0, sz.x, sz.y);

    const int textIndent = FromDIP(textIndentDIP);
    const int lineSpacing = FromDIP(lineSpacingDIP);
    int offsetY = lineSpacing;
    for (const auto& line : m_lines) {
        dc.DrawText(line, textIndent, offsetY);
        offsetY += dc.GetTextExtent(line).y + lineSpacing;
    }
}

void PGCheckedDragListCtrlGhostWindow::updatePosition(const wxPoint& pos) { Move(pos); }
