#include "GUI/DialogSettings.hpp"

#include "PGLocale.hpp"
#include "PGPatcherGlobals.hpp"

#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <string>

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

namespace {
constexpr int BORDER_SIZE = 10;
constexpr int COMBO_MIN_WIDTH = 200;

// wxRadioBox selection indices for the theme choices
constexpr int THEME_IDX_LIGHT = 0;
constexpr int THEME_IDX_DARK = 1;
constexpr int THEME_IDX_SYSTEM = 2;
} // namespace

DialogSettings::DialogSettings(wxWindow* parent,
                               PGConfig& pgc)
    : wxDialog(parent, wxID_ANY, PGTr("settings.title", "Settings"))
    , m_pgc(pgc)
{
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Language selection
    auto* langSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* langLabel = new wxStaticText(this, wxID_ANY, PGTr("settings.language.label", "Language"));
    langSizer->Add(langLabel, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, BORDER_SIZE);

    m_languages = PGLocale::getAvailableLanguages();

    wxArrayString langNames;
    for (const auto& lang : m_languages) {
        langNames.Add(lang.displayName);
    }

    m_languageCombo = new wxComboBox(
        this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, langNames, wxCB_READONLY);
    m_languageCombo->SetMinSize(wxSize(COMBO_MIN_WIDTH, -1));
    m_languageCombo->SetToolTip(
        PGTr("settings.language.tooltip", "Languages are read from the \"translations\" folder"));

    // Select the currently active language
    const auto currentLang = m_pgc.getUILanguage();
    for (size_t i = 0; i < m_languages.size(); ++i) {
        if (m_languages.at(i).code == currentLang) {
            m_languageCombo->SetSelection(static_cast<int>(i));
            break;
        }
    }

    langSizer->Add(m_languageCombo, 1, wxEXPAND, 0);
    mainSizer->Add(langSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Theme selection (light/dark/system)
    wxArrayString themeChoices;
    themeChoices.Add(PGTr("settings.theme.light", "Light"));
    themeChoices.Add(PGTr("settings.theme.dark", "Dark"));
    themeChoices.Add(PGTr("settings.theme.system", "System"));

    m_themeRadioBox = new wxRadioBox(this,
                                     wxID_ANY,
                                     PGTr("settings.theme.label", "Theme"),
                                     wxDefaultPosition,
                                     wxDefaultSize,
                                     themeChoices,
                                     1,
                                     wxRA_SPECIFY_ROWS);
    m_themeRadioBox->SetToolTip(PGTr("settings.theme.tooltip",
                                     "\"System\" follows the Windows theme. Applied when the launcher restarts."));

    // Select the currently configured theme
    const auto currentTheme = m_pgc.getUITheme();
    if (currentTheme == "light") {
        m_themeRadioBox->SetSelection(THEME_IDX_LIGHT);
    } else if (currentTheme == "dark") {
        m_themeRadioBox->SetSelection(THEME_IDX_DARK);
    } else {
        m_themeRadioBox->SetSelection(THEME_IDX_SYSTEM);
    }

    mainSizer->Add(m_themeRadioBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, BORDER_SIZE);

    // Buttons
    auto* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    buttonSizer->AddStretchSpacer(1);
    auto* okButton = new wxButton(this, wxID_OK, PGTr("common.ok", "OK"));
    okButton->Bind(wxEVT_BUTTON, &DialogSettings::onOkButtonPressed, this);
    auto* cancelButton = new wxButton(this, wxID_CANCEL, PGTr("common.cancel", "Cancel"));
    buttonSizer->Add(okButton, 0, wxRIGHT, BORDER_SIZE);
    buttonSizer->Add(cancelButton, 0, 0, 0);
    mainSizer->Add(buttonSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, BORDER_SIZE);

    okButton->SetDefault();

    SetSizerAndFit(mainSizer);
    CentreOnParent();
}

auto DialogSettings::languageChanged() const -> bool { return m_languageChanged; }

auto DialogSettings::themeChanged() const -> bool { return m_themeChanged; }

void DialogSettings::onOkButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    bool needsSave = false;

    const int selection = m_languageCombo->GetSelection();
    if (selection != wxNOT_FOUND) {
        const auto& selectedLang = m_languages.at(static_cast<size_t>(selection));
        if (selectedLang.code != m_pgc.getUILanguage()) {
            m_pgc.setUILanguage(selectedLang.code);
            PGLocale::init(PGPatcherGlobals::getEXEPath() / "translations", selectedLang.code);
            m_languageChanged = true;
            needsSave = true;
        }
    }

    std::string selectedTheme = "system";
    if (m_themeRadioBox->GetSelection() == THEME_IDX_LIGHT) {
        selectedTheme = "light";
    } else if (m_themeRadioBox->GetSelection() == THEME_IDX_DARK) {
        selectedTheme = "dark";
    }

    if (selectedTheme != m_pgc.getUITheme()) {
        m_pgc.setUITheme(selectedTheme);
        m_themeChanged = true;
        needsSave = true;
    }

    if (needsSave) {
        m_pgc.saveUserConfig();
    }

    EndModal(wxID_OK);
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
