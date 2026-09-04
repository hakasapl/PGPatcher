#include "GUI/DialogRecTypeSelector.hpp"

#include "GUI/components/PGWrappingStaticText.hpp"
#include "PGLocale.hpp"
#include "PGPlugin.hpp"

#include <algorithm>
#include <unordered_set>

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)

namespace {
constexpr int DIALOG_WIDTH = 300;
constexpr int DIALOG_HEIGHT = 400;
constexpr int DIALOG_MIN_HEIGHT = 300;
constexpr int DIALOG_MAX_HEIGHT = 1000;
constexpr int BORDER_SIZE = 10;
// Initial wrap width, kept just under the client width so the first wrap is never narrower than the final one
constexpr int TEXT_WRAP_WIDTH = DIALOG_WIDTH - (4 * BORDER_SIZE);
} // namespace

DialogRecTypeSelector::DialogRecTypeSelector(wxWindow* parent,
                                             const wxString& title)
    : wxDialog(parent,
               wxID_ANY,
               title,
               wxDefaultPosition,
               wxSize(DIALOG_WIDTH,
                      DIALOG_HEIGHT),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Add static text for instructions - wraps to the dialog width so that longer translations stay visible
    auto* instructionText = new PGWrappingStaticText(
        this,
        wxID_ANY,
        PGTr("dialogs.recTypeSelector.description",
             "Unchecking a record type will exclude it and its associated meshes from being patched. Only record types "
             "with models are shown."),
        TEXT_WRAP_WIDTH);
    mainSizer->Add(instructionText, 0, wxEXPAND | wxALL, BORDER_SIZE);

    m_listCtrl = new wxListCtrl(
        this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_ALIGN_LEFT | wxLC_NO_HEADER);
    m_listCtrl->EnableCheckBoxes(true);

    m_listCtrl->AppendColumn("Record Type", wxLIST_FORMAT_LEFT, wxLIST_AUTOSIZE_USEHEADER);
    m_listCtrl->SetColumnWidth(0, wxLIST_AUTOSIZE_USEHEADER);

    // Bind right-click for context menu
    m_listCtrl->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, [this](wxListEvent&) {
        wxMenu menu;
        auto* enableItem = menu.Append(1, PGTr("common.enable", "Enable"));
        auto* disableItem = menu.Append(2, PGTr("common.disable", "Disable"));

        // Check selection states
        bool allEnabled = true;
        bool allDisabled = true;

        long item = -1;
        while ((item = m_listCtrl->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
            if (!m_listCtrl->IsItemChecked(item)) {
                allEnabled = false;
            }
            if (m_listCtrl->IsItemChecked(item)) {
                allDisabled = false;
            }
        }

        // Disable menu items if action is not needed
        enableItem->Enable(!allEnabled);
        disableItem->Enable(!allDisabled);

        menu.Bind(wxEVT_COMMAND_MENU_SELECTED, [this](wxCommandEvent& e) {
            const bool check = (e.GetId() == 1);
            long item = -1;
            while ((item = m_listCtrl->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
                m_listCtrl->CheckItem(item, check);
            }
        });

        PopupMenu(&menu);
    });

    // Bind resize
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        if (m_listCtrl->GetColumnCount() > 0) {
            const int clientWidth = m_listCtrl->GetClientSize().GetWidth();
            m_listCtrl->SetColumnWidth(0, clientWidth);
        }
        event.Skip(); // important
    });

    mainSizer->Add(m_listCtrl, 1, wxEXPAND | wxALL, BORDER_SIZE);

    auto* btnSizer = new wxStdDialogButtonSizer();
    btnSizer->AddButton(new wxButton(this, wxID_CANCEL, PGTr("common.cancel", "Cancel")));
    btnSizer->AddButton(new wxButton(this, wxID_OK, PGTr("common.ok", "OK")));
    btnSizer->Realize();

    mainSizer->Add(btnSizer, 0, wxALIGN_RIGHT | wxBOTTOM | wxRIGHT, BORDER_SIZE);

    SetSizeHints(wxSize(DIALOG_WIDTH, DIALOG_MIN_HEIGHT), wxSize(DIALOG_WIDTH, -1));
    SetSizer(mainSizer);
    Layout();
    Fit();
}

void DialogRecTypeSelector::populateList(const std::unordered_set<PGPlugin::ModelRecordType>& selectedRecTypes)
{
    long index = 0;
    for (const auto& entry : PGPlugin::getAvailableRecTypeStrs()) {
        index = m_listCtrl->InsertItem(index, wxString(entry));
        const bool isChecked = selectedRecTypes.contains(
            PGPlugin::getRecTypeFromString(entry)); // check if this rec type is in the selected set

        m_listCtrl->CheckItem(index, isChecked);
        ++index;
    }

    // Set height of dialog to show all items without scrolling (with some padding)
    Layout();

    wxRect rect;
    if (!m_listCtrl->GetItemRect(0, rect, wxLIST_RECT_BOUNDS)) {
        return;
    }

    // Everything that is not the list itself (instruction text, buttons, borders) - measured rather than assumed
    // because the instruction text needs a different number of lines in each language
    const int chromeHeight = GetSize().GetHeight() - m_listCtrl->GetSize().GetHeight();
    const int itemHeight = rect.GetHeight();
    const int desiredHeight = static_cast<int>(m_listCtrl->GetItemCount() * itemHeight) + chromeHeight + BORDER_SIZE;
    // Cap the height to avoid an excessively large dialog
    SetSize(wxSize(GetSize().x, std::min(desiredHeight, DIALOG_MAX_HEIGHT)));
}

auto DialogRecTypeSelector::getSelectedRecordTypes() const -> std::unordered_set<PGPlugin::ModelRecordType>
{
    std::unordered_set<PGPlugin::ModelRecordType> result;

    long item = -1;
    while ((item = m_listCtrl->GetNextItem(item)) != -1) {
        if (m_listCtrl->IsItemChecked(item)) {
            const wxString code = m_listCtrl->GetItemText(item);
            const auto recType = PGPlugin::getRecTypeFromString(code.ToStdString());
            result.insert(recType);
        }
    }

    return result;
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)
