#pragma once

#include "PGConfig.hpp"
#include "PGLocale.hpp"

#include <wx/wx.h>

#include <vector>

/**
 * @class DialogSettings
 * @brief Application settings dialog (currently GUI language selection)
 */
class DialogSettings : public wxDialog {
public:
    DialogSettings(wxWindow* parent,
                   PGConfig& pgc);

    /**
     * @brief Whether the language was changed (the GUI needs to be rebuilt to apply it)
     */
    [[nodiscard]] auto languageChanged() const -> bool;

private:
    PGConfig& m_pgc;
    wxComboBox* m_languageCombo;
    std::vector<PGLocale::Language> m_languages;
    bool m_languageChanged = false;

    void onOkButtonPressed(wxCommandEvent& event);
};
