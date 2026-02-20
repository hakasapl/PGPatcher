#pragma once

#include "GUI/components/PGModifiableListCtrl.hpp"

#include <wx/listctrl.h>
#include <wx/wx.h>

class PGTextureMapListCtrl : public PGModifiableListCtrl {
private:
    wxComboBox* m_textureMapTypeCombo; /** Stores the texture map type combo box */

public:
    /**
     * @brief Construct a new PGTextureMapListCtrl object
     *
     * @param parent parent window
     * @param id window ID
     * @param pt position
     * @param sz size
     * @param style window style (default wxLC_REPORT)
     */
    PGTextureMapListCtrl(wxWindow* parent,
                         wxWindowID id,
                         const wxPoint& pt = wxDefaultPosition,
                         const wxSize& sz = wxDefaultSize,
                         long style = wxLC_REPORT);

private:
    /**
     * @brief Handle a double-click event to begin editing a texture map entry.
     *
     * @param event The mouse event from the double-click.
     */
    void onTextureRulesMapsChangeStart(wxMouseEvent& event);

    /**
     * @brief Determine which column contains a given point for a specific list item.
     *
     * @param pos Position to test, in client coordinates.
     * @param item List item index to check sub-item rectangles against.
     * @return Zero-based column index, or -1 if the point is not within any column.
     */
    auto getColumnAtPosition(const wxPoint& pos,
                             long item) -> int;
};
