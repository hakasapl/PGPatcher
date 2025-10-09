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
    PGTextureMapListCtrl(wxWindow* parent, wxWindowID id, const wxPoint& pt = wxDefaultPosition,
        const wxSize& sz = wxDefaultSize, long style = wxLC_REPORT);

private:
    void onTextureRulesMapsChangeStart(wxMouseEvent& event);

    auto getColumnAtPosition(const wxPoint& pos, long item) -> int;
};
