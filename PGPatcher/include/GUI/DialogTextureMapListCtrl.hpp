#pragma once

#include "GUI/components/PGTextureMapListCtrl.hpp"

#include "pgutil/PGNIFUtil.hpp"

#include <wx/listctrl.h>
#include <wx/wx.h>

#include <string>
#include <utility>
#include <vector>

class DialogTextureMapListCtrl : public wxDialog {
private:
    PGTextureMapListCtrl* m_listCtrl;
    wxStaticText* m_helpText;

public:
    DialogTextureMapListCtrl(wxWindow* parent,
                             const wxString& title,
                             const wxString& text);

    [[nodiscard]] auto getList() const -> std::vector<std::pair<std::wstring,
                                                                PGEnums::TextureType>>;
    void populateList(const std::vector<std::pair<std::wstring,
                                                  PGEnums::TextureType>>& items);

private:
    void updateColumnWidths();
};
