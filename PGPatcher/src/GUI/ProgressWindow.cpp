#include "GUI/ProgressWindow.hpp"

#include <wx/animate.h>
#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <string>
#include <wx/string.h>

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)

ProgressWindow::ProgressWindow()
    : wxDialog(nullptr, wxID_ANY, "PGPatcher Generation Progress", wxDefaultPosition, wxSize(300, 150))
{
    const wxIcon icon(wxICON(IDI_ICON1));
    SetIcon(icon);

    // Main sizer
    auto* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // Animated GIF on the left (part of the main sizer)
    wxAnimation anim;
    if (anim.LoadFile("resources/runningparallaxgen.gif", wxANIMATION_TYPE_GIF)) {
        auto* animCtrl = new wxAnimationCtrl(this, wxID_ANY, anim);
        animCtrl->Play(); // start playing
        mainSizer->Add(animCtrl, 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);
    }

    // Right Side (main progress area)
    auto* rightSizer = new wxBoxSizer(wxVERTICAL);

    m_mainStatusText = new wxStaticText(this, wxID_ANY, "Overall Progress:");
    m_progressBarMain = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(300, 20));

    m_stepStatusText = new wxStaticText(this, wxID_ANY, "");
    m_progressBarStep = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(300, 20));

    rightSizer->Add(m_mainStatusText, 0, wxEXPAND | wxBOTTOM, 5);
    rightSizer->Add(m_progressBarMain, 0, wxEXPAND | wxBOTTOM, 5);
    rightSizer->Add(m_stepStatusText, 0, wxEXPAND | wxBOTTOM, 5);
    rightSizer->Add(m_progressBarStep, 0, wxEXPAND | wxBOTTOM, 5);

    auto* cancelButton = new wxButton(this, wxID_CANCEL, "Stop Generation / Quit");
    rightSizer->Add(cancelButton, 0, wxEXPAND | wxTOP, 5);

    mainSizer->Add(rightSizer, 1, wxEXPAND | wxALL, 10);

    // Bind the Stop button
    cancelButton->Bind(wxEVT_BUTTON, [](wxCommandEvent&) -> void { wxTheApp->Exit(); });

    // Bind the window close event (X button)
    this->Bind(wxEVT_CLOSE_WINDOW, [](wxCloseEvent&) -> void { wxTheApp->Exit(); });

    SetSizerAndFit(mainSizer);
    Centre();
}

void ProgressWindow::setMainProgress(int done, int total, bool addToLabel)
{
    const int perc = static_cast<int>((static_cast<double>(done) / static_cast<double>(total)) * 100.0);
    m_progressBarMain->SetValue(perc);
    m_progressBarMain->Refresh();
    m_progressBarMain->Update();
    if (addToLabel) {
        m_mainStatusText->SetLabel(m_mainLabelBase + wxString::Format(" %d / %d [ %d%% ]", done, total, perc));
        m_mainStatusText->Refresh();
        m_mainStatusText->Update();
    }
}
void ProgressWindow::setMainLabel(const std::string& label)
{
    m_mainLabelBase = wxString(label);
    m_mainStatusText->SetLabel(label);
    m_mainStatusText->Refresh();
    m_mainStatusText->Update();
}

void ProgressWindow::setStepProgress(int done, int total, bool addToLabel)
{
    const int perc = static_cast<int>((static_cast<double>(done) / static_cast<double>(total)) * 100.0);
    m_progressBarStep->SetValue(perc);
    m_progressBarStep->Refresh();
    m_progressBarStep->Update();
    if (addToLabel) {
        m_stepStatusText->SetLabel(m_stepLabelBase + wxString::Format(" %d / %d [ %d%% ]", done, total, perc));
        m_stepStatusText->Refresh();
        m_stepStatusText->Update();
    }
}
void ProgressWindow::setStepLabel(const std::string& label)
{
    m_stepLabelBase = wxString(label);
    m_stepStatusText->SetLabel(label);
    m_stepStatusText->Refresh();
    m_stepStatusText->Update();
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)
