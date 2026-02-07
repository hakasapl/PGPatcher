#include "GUI/DialogRecTypeSelector.hpp"

#include "ParallaxGenPlugin.hpp"

#include <algorithm>
#include <unordered_set>

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)

DialogRecTypeSelector::DialogRecTypeSelector(wxWindow* parent, const wxString& title)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(300, 400), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    m_listCtrl = new wxListCtrl(
        this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_ALIGN_LEFT | wxLC_NO_HEADER);
    m_listCtrl->EnableCheckBoxes(true);

    m_listCtrl->AppendColumn("Record Type", wxLIST_FORMAT_LEFT, wxLIST_AUTOSIZE_USEHEADER);
    m_listCtrl->SetColumnWidth(0, wxLIST_AUTOSIZE_USEHEADER);

    // Bind right-click for context menu
    m_listCtrl->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, [this](wxListEvent&) {
        wxMenu menu;
        auto* enableItem = menu.Append(1, "Enable");
        auto* disableItem = menu.Append(2, "Disable");

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

    mainSizer->Add(m_listCtrl, 1, wxEXPAND | wxALL, 10);

    auto* btnSizer = new wxStdDialogButtonSizer();
    btnSizer->AddButton(new wxButton(this, wxID_CANCEL));
    btnSizer->AddButton(new wxButton(this, wxID_OK));
    btnSizer->Realize();

    mainSizer->Add(btnSizer, 0, wxALIGN_RIGHT | wxBOTTOM | wxRIGHT, 10);

    SetSizeHints(wxSize(300, 300), wxSize(300, -1));
    SetSizer(mainSizer);
}

void DialogRecTypeSelector::populateList(const std::unordered_set<ParallaxGenPlugin::ModelRecordType>& selectedRecTypes)
{
    long index = 0;
    for (const auto& entry : ParallaxGenPlugin::getAvailableRecTypeStrs()) {
        index = m_listCtrl->InsertItem(index, wxString(entry));
        const bool isChecked = selectedRecTypes.contains(
            ParallaxGenPlugin::getRecTypeFromString(entry)); // check if this rec type is in the selected set

        m_listCtrl->CheckItem(index, isChecked);
        ++index;
    }

    // Set height of dialog to show all items without scrolling (with some padding)
    wxRect rect;
    m_listCtrl->GetItemRect(0, rect, wxLIST_RECT_BOUNDS);
    const int itemHeight = rect.GetHeight(); // add some padding
    const int desiredHeight
        = static_cast<int>(m_listCtrl->GetItemCount() * itemHeight) + 100; // 100 for padding and buttons
    SetSize(wxSize(GetSize().x, std::min(desiredHeight, 800))); // cap height at 600 to avoid excessively large dialog
}

auto DialogRecTypeSelector::getSelectedRecordTypes() const -> std::unordered_set<ParallaxGenPlugin::ModelRecordType>
{
    std::unordered_set<ParallaxGenPlugin::ModelRecordType> result;

    long item = -1;
    while ((item = m_listCtrl->GetNextItem(item)) != -1) {
        if (m_listCtrl->IsItemChecked(item)) {
            const wxString code = m_listCtrl->GetItemText(item);
            const auto recType = ParallaxGenPlugin::getRecTypeFromString(code.ToStdString());
            result.insert(recType);
        }
    }

    return result;
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)
