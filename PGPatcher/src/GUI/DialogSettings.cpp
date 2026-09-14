#include "GUI/DialogSettings.hpp"

#include "PGConfig.hpp"
#include "PGLocale.hpp"
#include "PGPatcherGlobals.hpp"
#include "PGUI.hpp"

#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <cstddef>
#include <string>

// Disable owning memory checks because wxWidgets will take care of deleting the objects.
// Disable convert member functions to static because these functions need to be non-static for wxWidgets.
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

namespace {
// Sizes in DIPs (pixels at 100% scaling), scaled to the monitor's DPI with FromDIP() where they are used
constexpr int borderSizeDIP = 10;
constexpr int comboMinWidth = 200;

// Selection indices of the theme wxRadioBox.
constexpr int themeIdxLight = 0;
constexpr int themeIdxDark = 1;
constexpr int themeIdxSystem = 2;
} // namespace

DialogSettings::DialogSettings(wxWindow* parent,
                               PGConfig& pgc)
    : wxDialog(parent,
               wxID_ANY,
               pgTr("settings.title"))
    , m_pgc(pgc)
{
    SetIcons(PGUI::appIcons());

    // Pixel sizes are defined for 100% scaling, so scale them to the DPI of the monitor showing the dialog.
    const int borderSize = FromDIP(borderSizeDIP);

    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Language selection.
    auto* langSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* langLabel = new wxStaticText(this, wxID_ANY, pgTr("settings.language.label"));
    langSizer->Add(langLabel, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, borderSize);

    m_languages = PGLocale::availableLanguages();

    wxArrayString langNames;
    for (const auto& lang : m_languages)
        langNames.Add(lang.displayName);

    m_languageCombo
        = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, langNames, wxCB_READONLY);
    m_languageCombo->SetMinSize(wxSize(FromDIP(comboMinWidth), -1));
    m_languageCombo->SetToolTip(pgTr("settings.language.tooltip"));

    // Select the currently active language.
    const auto currentLang = m_pgc.uiLanguage();
    for (size_t i = 0; i < m_languages.size(); ++i) {
        if (m_languages.at(i).code == currentLang) {
            m_languageCombo->SetSelection(static_cast<int>(i));
            break;
        }
    }

    langSizer->Add(m_languageCombo, 1, wxEXPAND, 0);
    mainSizer->Add(langSizer, 0, wxEXPAND | wxALL, borderSize);

    // Theme selection (light/dark/system).
    wxArrayString themeChoices;
    themeChoices.Add(pgTr("settings.theme.light"));
    themeChoices.Add(pgTr("settings.theme.dark"));
    themeChoices.Add(pgTr("settings.theme.system"));

    m_themeRadioBox = new wxRadioBox(this,
                                     wxID_ANY,
                                     pgTr("settings.theme.label"),
                                     wxDefaultPosition,
                                     wxDefaultSize,
                                     themeChoices,
                                     1,
                                     wxRA_SPECIFY_ROWS);
    m_themeRadioBox->SetToolTip(pgTr("settings.theme.tooltip"));

    // Select the currently configured theme.
    const auto currentTheme = m_pgc.uiTheme();
    if (currentTheme == "light")
        m_themeRadioBox->SetSelection(themeIdxLight);
    else if (currentTheme == "dark")
        m_themeRadioBox->SetSelection(themeIdxDark);
    else
        m_themeRadioBox->SetSelection(themeIdxSystem);

    mainSizer->Add(m_themeRadioBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, borderSize);

    // Buttons.
    auto* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    buttonSizer->AddStretchSpacer(1);
    auto* okButton = new wxButton(this, wxID_OK, pgTr("common.ok"));
    okButton->Bind(wxEVT_BUTTON, &DialogSettings::onOkButtonPressed, this);
    auto* cancelButton = new wxButton(this, wxID_CANCEL, pgTr("common.cancel"));
    buttonSizer->Add(okButton, 0, wxRIGHT, borderSize);
    buttonSizer->Add(cancelButton, 0, 0, 0);
    mainSizer->Add(buttonSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, borderSize);

    okButton->SetDefault();

    SetSizerAndFit(mainSizer);
    CentreOnParent();
}

bool DialogSettings::languageChanged() const { return m_didLanguageChange; }

bool DialogSettings::themeChanged() const { return m_didThemeChange; }

void DialogSettings::onOkButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    bool needsSave = false;

    const int selection = m_languageCombo->GetSelection();
    if (selection != wxNOT_FOUND) {
        const auto& selectedLang = m_languages.at(static_cast<size_t>(selection));
        if (selectedLang.code != m_pgc.uiLanguage()) {
            m_pgc.setUILanguage(selectedLang.code);
            PGLocale::init(PGPatcherGlobals::exePath() / "translations", selectedLang.code);
            m_didLanguageChange = true;
            needsSave = true;
        }
    }

    std::string selectedTheme = "system";
    if (m_themeRadioBox->GetSelection() == themeIdxLight)
        selectedTheme = "light";
    else if (m_themeRadioBox->GetSelection() == themeIdxDark)
        selectedTheme = "dark";

    if (selectedTheme != m_pgc.uiTheme()) {
        m_pgc.setUITheme(selectedTheme);
        m_didThemeChange = true;
        needsSave = true;
    }

    if (needsSave)
        m_pgc.saveUserConfig();

    EndModal(wxID_OK);
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
