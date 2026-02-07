#pragma once

#include "ParallaxGenPlugin.hpp"

#include <wx/listctrl.h>
#include <wx/wx.h>

#include <unordered_set>

class DialogRecTypeSelector : public wxDialog {
private:
    wxListCtrl* m_listCtrl;

public:
    DialogRecTypeSelector(wxWindow* parent, const wxString& title = "Allowed Record Types");

    [[nodiscard]] auto getSelectedRecordTypes() const -> std::unordered_set<ParallaxGenPlugin::ModelRecordType>;

    void populateList(const std::unordered_set<ParallaxGenPlugin::ModelRecordType>& selectedRecTypes);
};
