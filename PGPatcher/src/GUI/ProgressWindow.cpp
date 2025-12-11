#include "GUI/ProgressWindow.hpp"

#include <wx/animate.h>
#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)

ProgressWindow::ProgressWindow()
    : wxDialog(nullptr, wxID_ANY, "PGPatcher Generation Progress", wxDefaultPosition, wxSize(300, 150))
{
    // Main horizontal sizer
    auto* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // --- Left: Animated GIF ---
    wxAnimation anim;
    if (anim.LoadFile("resources/runningparallaxgen.gif", wxANIMATION_TYPE_GIF)) {
        auto* animCtrl = new wxAnimationCtrl(this, wxID_ANY, anim);
        animCtrl->Play(); // start playing
        mainSizer->Add(animCtrl, 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);
    }

    // --- Right: Progress bars and cancel button ---
    auto* rightSizer = new wxBoxSizer(wxVERTICAL);

    m_progressBarMain = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(300, 20));
    m_progressBarStep = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(300, 20));

    rightSizer->Add(m_progressBarMain, 0, wxEXPAND | wxBOTTOM, 10);
    rightSizer->Add(m_progressBarStep, 0, wxEXPAND | wxBOTTOM, 20);

    auto* cancelButton = new wxButton(this, wxID_CANCEL, "Stop");
    rightSizer->Add(cancelButton, 0, wxALIGN_RIGHT);

    mainSizer->Add(rightSizer, 1, wxEXPAND | wxALL, 10);

    SetSizerAndFit(mainSizer);
    Centre();
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)
