#include "GUI/CompletionDialog.hpp"

#include "GUI/components/PGLogMessageListCtrl.hpp"
#include "PGPatcherGlobals.hpp"
#include "ParallaxGenConfig.hpp"

#include <wx/artprov.h>
#include <wx/collpane.h>
#include <wx/gdicmn.h>
#include <wx/listbase.h>
#include <wx/listctrl.h>
#include <wx/toplevel.h>
#include <wx/wx.h>

#include <algorithm>
#include <stdexcept>
#include <string>

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)

CompletionDialog::CompletionDialog(const long long& timeTaken)
    : wxDialog(nullptr, wxID_ANY, "PGPatcher Generation Complete", wxDefaultPosition, wxDefaultSize,
          wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP | wxRESIZE_BORDER | wxMINIMIZE_BOX)
{
    // Play system information sound
    wxBell();

    // Get config
    const auto outputPath = PGPatcherGlobals::getPGC()->getParams().Output.dir;

    // Calculate required width based on path length
    const wxClientDC dc(this);
    const wxSize pathSize = dc.GetTextExtent(outputPath.wstring());
    const int requiredWidth = std::max(MIN_WIDTH, pathSize.GetWidth() + 60); // +60 for icon and padding

    // Main sizer
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Horizontal sizer for icon and text
    auto* contentSizer = new wxBoxSizer(wxHORIZONTAL);

    // Add information icon
    auto* icon = new wxStaticBitmap(this, wxID_ANY, wxArtProvider::GetIcon(wxART_INFORMATION, wxART_MESSAGE_BOX));
    contentSizer->Add(icon, 0, wxTOP | wxLEFT | wxBOTTOM | wxALIGN_CENTER_VERTICAL, 10);

    // Text
    auto* text = new wxStaticText(this, wxID_ANY,
        "PGPatcher has completed generating output.\n\nProcessing Time: " + std::to_string(timeTaken)
            + " seconds\nOutput Location:\n" + outputPath.wstring());
    text->Wrap(requiredWidth - 80); // Wrap based on calculated width
    contentSizer->Add(text, 1, wxALL | wxALIGN_CENTER_VERTICAL, 15);

    mainSizer->Add(contentSizer, 1, wxEXPAND);

    // WARNINGS
    auto* warningsCtrl = new wxCollapsiblePane(
        this, wxID_ANY, "Show Warnings", wxDefaultPosition, wxDefaultSize, wxCP_DEFAULT_STYLE | wxCP_NO_TLW_RESIZE);

    m_warnListCtrl = new PGLogMessageListCtrl(warningsCtrl->GetPane(), wxID_ANY);
    m_warnListCtrl->Bind(s_EVT_PG_LOG_IGNORE_CHANGED, [this, warningsCtrl](wxCommandEvent&) -> void {
        const auto numWarnings = m_warnListCtrl->getNumUnignoredMessages();
        warningsCtrl->SetLabel("Show Warnings (" + std::to_string(numWarnings) + ")");

        warningsCtrl->Refresh();
        warningsCtrl->Update();
    });

    // get existing ignore messages
    const auto ignoreMap = ParallaxGenConfig::getIgnoredMessagesConfig();
    m_warnListCtrl->setIgnoreMap(ignoreMap);
    m_warnListCtrl->setLogMessages(PGPatcherGlobals::getWXLoggerSink()->getWarningMessages());
    setupLogMessagePane(warningsCtrl, m_warnListCtrl);

    mainSizer->Add(warningsCtrl, 0, wxEXPAND, 0);

    // ERRORS
    auto* errorsCtrl = new wxCollapsiblePane(
        this, wxID_ANY, "Show Errors", wxDefaultPosition, wxDefaultSize, wxCP_DEFAULT_STYLE | wxCP_NO_TLW_RESIZE);
    m_errListCtrl = new PGLogMessageListCtrl(errorsCtrl->GetPane(), wxID_ANY, false);
    m_errListCtrl->Bind(s_EVT_PG_LOG_IGNORE_CHANGED, [this, errorsCtrl](wxCommandEvent&) -> void {
        const auto numErrors = m_errListCtrl->getNumUnignoredMessages();
        errorsCtrl->SetLabel("Show Errors (" + std::to_string(numErrors) + ")");

        errorsCtrl->Refresh();
        errorsCtrl->Update();
    });

    m_errListCtrl->setLogMessages(PGPatcherGlobals::getWXLoggerSink()->getErrorMessages());
    setupLogMessagePane(errorsCtrl, m_errListCtrl, false);

    mainSizer->Add(errorsCtrl, 0, wxEXPAND, 0);

    // Buttons sizer
    auto* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

    // OK button
    auto* okButton = new wxButton(this, wxID_ANY, "OK");
    okButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) -> void {
        saveIgnoredMessagesToConfig();
        EndModal(wxID_OK); // then close
    });
    buttonSizer->Add(okButton, 0, wxALL, 10);

    // Open File Location button
    auto* openFileLocationButton = new wxButton(this, wxID_ANY, "Open Output Location");
    openFileLocationButton->Bind(wxEVT_BUTTON, &CompletionDialog::onOpenOutputLocation, this);
    buttonSizer->Add(openFileLocationButton, 0, wxALL, 10);

    // Open Log file button
    auto* openLogFileButton = new wxButton(this, wxID_ANY, "Open Log File");
    openLogFileButton->Bind(wxEVT_BUTTON, &CompletionDialog::onOpenLogFile, this);
    buttonSizer->Add(openLogFileButton, 0, wxALL, 10);

    mainSizer->Add(buttonSizer, 0, wxALIGN_CENTER_HORIZONTAL);

    SetSizerAndFit(mainSizer); // This automatically sizes the dialog to fit content

    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& evt) {
        if (evt.GetKeyCode() == WXK_ESCAPE) {
            Close(); // acts like the X button
        } else {
            evt.Skip(); // allow other keys to behave normally
        }
    });
    Bind(wxEVT_CLOSE_WINDOW, [](wxCloseEvent& evt) -> void {
        evt.Skip(); // allow normal close without running pre-close logic
    });

    // Set min/max size based on fitted size
    const wxSize fittedSize = GetSize();
    SetSizeHints(fittedSize, wxSize(-1, fittedSize.GetHeight()));

    Centre(); // Center the dialog on screen
}

void CompletionDialog::setupLogMessagePane(wxCollapsiblePane* pane, PGLogMessageListCtrl* listCtrl, bool ignoreCheckbox)
{
    if (pane == nullptr || listCtrl == nullptr) {
        throw std::invalid_argument("pane and listCtrl cannot be null");
    }

    // Limit number of visible items before scrolling
    static constexpr int LIST_SIZE = 150;
    listCtrl->SetMinSize(wxSize(-1, LIST_SIZE));
    listCtrl->SetMaxSize(wxSize(-1, LIST_SIZE));

    // Sizer for collapsible pane
    auto* parentSizer = new wxBoxSizer(wxVERTICAL);

    // checkbox for showing ignored warnings
    int checkboxHeight = 0;
    if (ignoreCheckbox) {
        auto* checkboxShowIgnored = new wxCheckBox(pane->GetPane(), wxID_ANY, "Show Ignored Warnings");
        checkboxShowIgnored->SetValue(false);

        // bind checkbox event
        checkboxShowIgnored->Bind(
            wxEVT_CHECKBOX, [listCtrl](wxCommandEvent& evt) -> void { listCtrl->setShowIgnored(evt.IsChecked()); });

        // add checkbox to the sizer first, so it appears above the list
        parentSizer->Add(checkboxShowIgnored, 0, wxALL | wxEXPAND, 5);

        // get height for later size calculations
        checkboxHeight = checkboxShowIgnored->GetSize().GetHeight() + 10;
    }

    const int expandDelta = LIST_SIZE + 5 + checkboxHeight;

    // add the list control below
    parentSizer->Add(listCtrl, 1, wxEXPAND);

    pane->GetPane()->SetSizer(parentSizer);

    // collapsible pane expand/shrink handling
    pane->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, [this, pane, expandDelta](wxCollapsiblePaneEvent&) -> void {
        wxSize dlgSize = this->GetSize();
        wxSize dlgMinSize = this->GetMinSize();
        if (pane->IsExpanded()) {
            dlgSize.SetHeight(dlgSize.GetHeight() + expandDelta);
            dlgMinSize.SetHeight(dlgMinSize.GetHeight() + expandDelta);
        } else {
            dlgSize.SetHeight(dlgSize.GetHeight() - expandDelta);
            dlgMinSize.SetHeight(dlgMinSize.GetHeight() - expandDelta);
        }
        this->SetSizeHints(dlgMinSize, wxSize(-1, dlgMinSize.GetHeight()));
        this->SetSize(dlgSize);
        this->Layout();
    });
}

void CompletionDialog::onOpenOutputLocation([[maybe_unused]] wxCommandEvent& event)
{
    saveIgnoredMessagesToConfig();

    const auto outputPath = PGPatcherGlobals::getPGC()->getParams().Output.dir;
    wxLaunchDefaultApplication(outputPath.wstring());

    // Close dialog
    EndModal(wxID_OK);
}

void CompletionDialog::onOpenLogFile([[maybe_unused]] wxCommandEvent& event)
{
    saveIgnoredMessagesToConfig();

    const auto logFilePath = PGPatcherGlobals::getEXEPath() / "log" / "PGPatcher.log";
    wxLaunchDefaultApplication(logFilePath.wstring());

    // Close dialog
    EndModal(wxID_OK);
}

void CompletionDialog::saveIgnoredMessagesToConfig()
{
    // Combine ignore maps from both lists
    const auto ignoreMap = m_warnListCtrl->getIgnoreMap();
    // Save to config
    ParallaxGenConfig::saveIgnoredMessagesConfig(ignoreMap);
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)
