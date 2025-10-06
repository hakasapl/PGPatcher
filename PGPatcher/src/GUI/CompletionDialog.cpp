#include "GUI/CompletionDialog.hpp"
#include "PGPatcherGlobals.hpp"

#include <string>
#include <wx/artprov.h>

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)

CompletionDialog::CompletionDialog(const long long& timeTaken)
    : wxDialog(nullptr, wxID_ANY, "PGPatcher Generation Complete", wxDefaultPosition, wxDefaultSize,
          wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP)
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

    // Buttons sizer
    auto* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

    // OK button
    auto* okButton = new wxButton(this, wxID_OK, "OK");
    buttonSizer->Add(okButton, 0, wxALL, 10);

    // Open File Location button
    auto* openFileLocationButton = new wxButton(this, wxID_ANY, "Open Output Location");
    openFileLocationButton->Bind(wxEVT_BUTTON, &CompletionDialog::onOpenOutputLocation, this);
    buttonSizer->Add(openFileLocationButton, 0, wxALL, 10);

    mainSizer->Add(buttonSizer, 0, wxALIGN_CENTER_HORIZONTAL);

    SetSizerAndFit(mainSizer); // This automatically sizes the dialog to fit content

    // Set min/max size based on fitted size
    const wxSize fittedSize = GetSize();
    SetMinSize(fittedSize);
    SetMaxSize(fittedSize);

    Centre(); // Center the dialog on screen
}

void CompletionDialog::onOpenOutputLocation([[maybe_unused]] wxCommandEvent& event)
{
    const auto outputPath = PGPatcherGlobals::getPGC()->getParams().Output.dir;
    wxLaunchDefaultApplication(outputPath.wstring());

    // Close dialog
    EndModal(wxID_OK);
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)
