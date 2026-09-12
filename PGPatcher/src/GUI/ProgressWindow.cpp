#include "GUI/ProgressWindow.hpp"
#include "GUI/components/PGAnimationCtrl.hpp"
#include "PGLocale.hpp"
#include "PGPatcherGlobals.hpp"

#include <wx/animate.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/gauge.h>
#include <wx/iconbndl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <filesystem>
#include <string>
#include <wx/string.h>
#include <wx/toplevel.h>

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)

ProgressWindow::ProgressWindow()
    : wxDialog(nullptr,
               wxID_ANY,
               PGTr("progress.title", "PGPatcher Generation Progress"),
               wxDefaultPosition,
               wxSize(300,
                      150),
               wxDEFAULT_DIALOG_STYLE | wxMINIMIZE_BOX)
{
    // Every size of the icon resource, so that the title bar and the taskbar get the size matching the monitor's DPI
    SetIcons(wxIconBundle("IDI_ICON1", nullptr));

    // Pixel sizes are defined for 100% scaling, so scale them to the DPI of the monitor showing the window
    const int border = FromDIP(10);
    const int spacing = FromDIP(5);

    // Main sizer
    auto* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // Animated GIF on the left (part of the main sizer)
    wxAnimation anim;
    const auto resourcesPath = PGPatcherGlobals::getEXEPath() / "resources";
    auto gifPath
        = resourcesPath / (PGPatcherGlobals::isDarkMode() ? "runningparallaxgen_dark.gif" : "runningparallaxgen.gif");
    if (!std::filesystem::exists(gifPath)) {
        gifPath = resourcesPath / "runningparallaxgen.gif";
    }
    if (anim.LoadFile(gifPath.wstring(), wxANIMATION_TYPE_GIF)) {
        auto* animCtrl = new PGAnimationCtrl(this, wxID_ANY, anim);
        animCtrl->Play(); // start playing
        mainSizer->Add(animCtrl, 0, wxALL | wxALIGN_CENTER_VERTICAL, border);
    }

    // Right Side (main progress area)
    auto* rightSizer = new wxBoxSizer(wxVERTICAL);

    m_mainStatusText = new wxStaticText(this, wxID_ANY, PGTr("progress.overall", "Overall Progress:"));
    m_progressBarMain = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, FromDIP(wxSize(300, 20)));

    m_stepStatusText = new wxStaticText(this, wxID_ANY, "");
    m_progressBarStep = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, FromDIP(wxSize(300, 20)));

    rightSizer->Add(m_mainStatusText, 0, wxEXPAND | wxBOTTOM, spacing);
    rightSizer->Add(m_progressBarMain, 0, wxEXPAND | wxBOTTOM, spacing);
    rightSizer->Add(m_stepStatusText, 0, wxEXPAND | wxBOTTOM, spacing);
    rightSizer->Add(m_progressBarStep, 0, wxEXPAND | wxBOTTOM, spacing);

    auto* cancelButton = new wxButton(this, wxID_CANCEL, PGTr("progress.stopButton", "Stop Generation / Quit"));
    rightSizer->Add(cancelButton, 0, wxEXPAND | wxTOP, spacing);

    mainSizer->Add(rightSizer, 1, wxEXPAND | wxALL, border);

    // Bind the Stop button
    cancelButton->Bind(wxEVT_BUTTON, [](wxCommandEvent&) -> void { wxTheApp->Exit(); });

    // Bind the window close event (X button)
    this->Bind(wxEVT_CLOSE_WINDOW, [](wxCloseEvent&) -> void { wxTheApp->Exit(); });

    SetSizerAndFit(mainSizer);
    Centre();
}

void ProgressWindow::setMainProgress(int done,
                                     int total,
                                     bool addToLabel)
{
    int perc = 0;
    if (total > 0) {
        perc = static_cast<int>((static_cast<double>(done) / static_cast<double>(total)) * 100.0);
    }
    m_progressBarMain->SetValue(perc);
    m_progressBarMain->Refresh();
    m_progressBarMain->Update();
    if (addToLabel) {
        m_mainStatusText->SetLabel(m_mainLabelBase + wxString::Format(" %d / %d [ %d%% ]", done, total, perc));
        m_mainStatusText->Refresh();
        m_mainStatusText->Update();
    }
}
void ProgressWindow::setMainLabel(const wxString& label)
{
    m_mainLabelBase = label;
    m_mainStatusText->SetLabel(label);
    m_mainStatusText->Refresh();
    m_mainStatusText->Update();
}

void ProgressWindow::setStepProgress(int done,
                                     int total,
                                     bool addToLabel)
{
    int perc = 0;
    if (total > 0) {
        perc = static_cast<int>((static_cast<double>(done) / static_cast<double>(total)) * 100.0);
    }
    m_progressBarStep->SetValue(perc);
    m_progressBarStep->Refresh();
    m_progressBarStep->Update();
    if (addToLabel) {
        m_stepStatusText->SetLabel(m_stepLabelBase + wxString::Format(" %d / %d [ %d%% ]", done, total, perc));
        m_stepStatusText->Refresh();
        m_stepStatusText->Update();
    }
}
void ProgressWindow::setStepLabel(const wxString& label)
{
    m_stepLabelBase = label;
    m_stepStatusText->SetLabel(label);
    m_stepStatusText->Refresh();
    m_stepStatusText->Update();
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)
