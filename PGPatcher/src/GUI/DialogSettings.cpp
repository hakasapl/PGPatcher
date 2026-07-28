#include "GUI/DialogSettings.hpp"

#include "PGLocale.hpp"
#include "PGPatcherGlobals.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

namespace {
constexpr int BORDER_SIZE = 10;
constexpr int COMBO_MIN_WIDTH = 200;
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

void DialogSettings::onOkButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    const int selection = m_languageCombo->GetSelection();
    if (selection != wxNOT_FOUND) {
        const auto& selectedLang = m_languages.at(static_cast<size_t>(selection));
        if (selectedLang.code != m_pgc.getUILanguage()) {
            m_pgc.setUILanguage(selectedLang.code);
            m_pgc.saveUserConfig();
            PGLocale::init(PGPatcherGlobals::getEXEPath() / "translations", selectedLang.code);
            m_languageChanged = true;
        }
    }

    EndModal(wxID_OK);
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
