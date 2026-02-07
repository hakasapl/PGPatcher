#pragma once

#include "GUI/components/PGModifiableListCtrl.hpp"

#include <wx/listctrl.h>
#include <wx/wx.h>

#include <string>
#include <vector>

class DialogModifiableListCtrl : public wxDialog {
private:
    PGModifiableListCtrl* m_listCtrl;
    wxStaticText* m_helpText;

public:
    DialogModifiableListCtrl(wxWindow* parent, const wxString& title, const wxString& text);

    [[nodiscard]] auto getList() const -> std::vector<std::wstring>;
    void populateList(const std::vector<std::wstring>& items);

private:
    void updateColumnWidth();
};
