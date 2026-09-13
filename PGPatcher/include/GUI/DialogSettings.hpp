#pragma once

#include "PGConfig.hpp"
#include "PGLocale.hpp"

#include <wx/wx.h>

#include <vector>

/**
 * @class DialogSettings
 * @brief Application settings dialog (GUI language and theme selection)
 */
class DialogSettings : public wxDialog {
public:
    DialogSettings(wxWindow* parent,
                   PGConfig& pgc);

    /**
     * @brief Whether the language was changed (the GUI needs to be rebuilt to apply it)
     */
    [[nodiscard]] bool languageChanged() const;

    /**
     * @brief Whether the theme was changed (the GUI needs to be rebuilt to apply it)
     */
    [[nodiscard]] bool themeChanged() const;

private:
    PGConfig& m_pgc;
    wxComboBox* m_languageCombo;
    wxRadioBox* m_themeRadioBox;
    std::vector<PGLocale::Language> m_languages;
    bool m_didLanguageChange = false;
    bool m_didThemeChange = false;

    void onOkButtonPressed(wxCommandEvent& event);
};
