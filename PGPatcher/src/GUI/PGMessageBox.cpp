#include "GUI/PGMessageBox.hpp"

#include "PGPatcherGlobals.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// NOLINTBEGIN(cppcoreguidelines-owning-memory)

namespace {

constexpr int DIALOG_BORDER = 10;
constexpr int TEXT_WRAP_WIDTH = 400;

/**
 * @brief Minimal wx-drawn message dialog used in dark mode (message text and standard buttons only)
 */
class PGDarkMessageDialog : public wxDialog {
public:
    PGDarkMessageDialog(wxWindow* parent,
                        const wxString& message,
                        const wxString& caption,
                        int style)
        : wxDialog(parent, wxID_ANY, caption)
    {
        auto* mainSizer = new wxBoxSizer(wxVERTICAL);

        auto* text = new wxStaticText(this, wxID_ANY, message);
        text->Wrap(FromDIP(TEXT_WRAP_WIDTH));
        mainSizer->Add(text, 1, wxALL | wxEXPAND, FromDIP(DIALOG_BORDER));

        auto* btnSizer
            = CreateStdDialogButtonSizer(style & (wxOK | wxCANCEL | wxYES | wxNO | wxHELP | wxNO_DEFAULT));
        mainSizer->Add(btnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(DIALOG_BORDER));

        // Yes/No buttons do not end the modal loop by default (OK/Cancel are handled by wxDialog)
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) -> void { EndModal(wxID_YES); }, wxID_YES);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) -> void { EndModal(wxID_NO); }, wxID_NO);

        // Let ESC / the close box act as "No" when there is no cancel button
        if ((style & wxNO) != 0 && (style & wxCANCEL) == 0) {
            SetEscapeId(wxID_NO);
        }

        SetSizerAndFit(mainSizer);
        CentreOnParent();
    }
};

} // namespace

auto PGMessageBox(const wxString& message,
                  const wxString& caption,
                  int style,
                  wxWindow* parent) -> int
{
    if (!PGPatcherGlobals::isDarkMode()) {
        return wxMessageBox(message, caption, style, parent);
    }

    PGDarkMessageDialog dialog(parent, message, caption, style);

    switch (dialog.ShowModal()) {
    case wxID_YES:
        return wxYES;
    case wxID_NO:
        return wxNO;
    case wxID_CANCEL:
        return wxCANCEL;
    case wxID_HELP:
        return wxHELP;
    default:
        return wxOK;
    }
}

// NOLINTEND(cppcoreguidelines-owning-memory)
