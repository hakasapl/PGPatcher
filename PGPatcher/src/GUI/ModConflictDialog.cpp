#include "GUI/ModConflictDialog.hpp"

#include "ParallaxGenHandlers.hpp"

using namespace std;

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

// class ModSortDialog
ModConflictDialog::ModConflictDialog(
    const std::unordered_map<std::unordered_set<std::wstring>, std::wstring, WStringSetHash>& conflicts)
    : wxDialog(nullptr, wxID_ANY, "Resolve Mod Conflicts", wxDefaultPosition, wxSize(DEFAULT_WIDTH, DEFAULT_HEIGHT),
          wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP | wxRESIZE_BORDER)
{
    auto* topSizer = new wxBoxSizer(wxVERTICAL); // sizer for the dialog
    auto* scrollWin = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    scrollWin->SetScrollRate(SCROLL_SIZE, SCROLL_SIZE);

    auto* scrollSizer = new wxBoxSizer(wxVERTICAL); // sizer for scrollWin

    // Loop through conflicts
    for (const auto& [modSet, winningMod] : conflicts) {
        auto* box = new wxStaticBox(scrollWin, wxID_ANY, "");
        auto* boxSizer = new wxStaticBoxSizer(box, wxVERTICAL);

        bool first = true;
        for (const auto& mod : modSet) {
            const long style = first ? wxRB_GROUP : 0;
            first = false;

            auto* curRB = new wxRadioButton(scrollWin, wxID_ANY, mod, wxDefaultPosition, wxDefaultSize, style);
            if (mod == winningMod) {
                curRB->SetValue(true);
            } else {
                curRB->SetValue(false);
            }

            m_rbTracker[modSet].push_back(curRB);

            boxSizer->Add(curRB, 0, wxALL, DEFAULT_BORDER);
        }

        scrollSizer->Add(boxSizer, 0, wxALL | wxEXPAND, DEFAULT_BORDER);
    }

    scrollWin->SetSizer(scrollSizer);
    scrollWin->FitInside();

    topSizer->Add(scrollWin, 1, wxEXPAND | wxALL, DEFAULT_BORDER);

    // OK button
    auto* okButton = new wxButton(this, wxID_OK, "OK");
    topSizer->Add(okButton, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, DEFAULT_BORDER);

    SetSizer(topSizer);
    Layout();
}

auto ModConflictDialog::getResolvedConflicts() const
    -> std::unordered_map<std::unordered_set<std::wstring>, std::wstring, WStringSetHash>
{
    unordered_map<unordered_set<wstring>, wstring, WStringSetHash> resolvedConflicts;

    // Logic to resolve conflicts and populate resolvedConflicts
    for (const auto& [modSet, radioButtons] : m_rbTracker) {
        for (const auto& rb : radioButtons) {
            if (rb->GetValue()) {
                resolvedConflicts[modSet] = rb->GetLabel().ToStdWstring();
                break;
            }
        }
    }

    return resolvedConflicts;
}

void ModConflictDialog::onClose( // NOLINT(readability-convert-member-functions-to-static)
    [[maybe_unused]] wxCloseEvent& event)
{
    ParallaxGenHandlers::nonBlockingExit();
    wxTheApp->Exit();
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
