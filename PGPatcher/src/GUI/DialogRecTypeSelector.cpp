#include "GUI/DialogRecTypeSelector.hpp"

#include "GUI/components/PGWrappingStaticText.hpp"
#include "PGLocale.hpp"
#include "PGPlugin.hpp"
#include "PGUI.hpp"

#include <algorithm>
#include <unordered_set>

// Disable owning memory checks because wxWidgets will take care of deleting the objects.
// Disable convert member functions to static because these functions need to be non-static for wxWidgets.
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

namespace {
// Sizes in DIPs (pixels at 100% scaling), scaled to the monitor's DPI with FromDIP() where they are used
constexpr int dialogWidthDIP = 300;
constexpr int dialogMinHeight = 300;
constexpr int dialogMaxHeight = 1000;
constexpr int borderSizeDIP = 10;
// Initial wrap width, kept just under the client width so the first wrap is never narrower than the final one.
constexpr int textWrapWidth = dialogWidthDIP - (4 * borderSizeDIP);
} // namespace

DialogRecTypeSelector::DialogRecTypeSelector(wxWindow* parent,
                                             const wxString& title)
    : wxDialog(parent,
               wxID_ANY,
               title,
               wxDefaultPosition,
               wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetIcons(PGUI::appIcons());

    // Pixel sizes are defined for 100% scaling, so scale them to the DPI of the monitor showing the dialog.
    const int borderSize = FromDIP(borderSizeDIP);

    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Add static text for instructions - wraps to the dialog width so that longer translations stay visible.
    auto* instructionText
        = new PGWrappingStaticText(this, wxID_ANY, pgTr("dialogs.recTypeSelector.description"), FromDIP(textWrapWidth));
    mainSizer->Add(instructionText, 0, wxEXPAND | wxALL, borderSize);

    m_listCtrl = new wxListCtrl(
        this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_ALIGN_LEFT | wxLC_NO_HEADER);
    m_listCtrl->EnableCheckBoxes(true);

    m_listCtrl->AppendColumn("Record Type", wxLIST_FORMAT_LEFT, wxLIST_AUTOSIZE_USEHEADER);
    m_listCtrl->SetColumnWidth(0, wxLIST_AUTOSIZE_USEHEADER);

    // Bind right-click for context menu.
    m_listCtrl->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, [this](wxListEvent&) {
        wxMenu menu;
        auto* enableItem = menu.Append(1, pgTr("common.enable"));
        auto* disableItem = menu.Append(2, pgTr("common.disable"));

        // Check selection states.
        bool allEnabled = true;
        bool allDisabled = true;

        long item = -1;
        while ((item = m_listCtrl->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
            if (!m_listCtrl->IsItemChecked(item))
                allEnabled = false;
            if (m_listCtrl->IsItemChecked(item))
                allDisabled = false;
        }

        // Disable menu items if action is not needed.
        enableItem->Enable(!allEnabled);
        disableItem->Enable(!allDisabled);

        menu.Bind(wxEVT_COMMAND_MENU_SELECTED, [this](wxCommandEvent& e) {
            const bool check = (e.GetId() == 1);
            long item = -1;
            while ((item = m_listCtrl->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1)
                m_listCtrl->CheckItem(item, check);
        });

        PopupMenu(&menu);
    });

    // Bind resize.
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        if (m_listCtrl->GetColumnCount() > 0) {
            const int clientWidth = m_listCtrl->GetClientSize().GetWidth();
            m_listCtrl->SetColumnWidth(0, clientWidth);
        }
        event.Skip(); // important
    });

    mainSizer->Add(m_listCtrl, 1, wxEXPAND | wxALL, borderSize);

    auto* btnSizer = new wxStdDialogButtonSizer();
    btnSizer->AddButton(new wxButton(this, wxID_CANCEL, pgTr("common.cancel")));
    btnSizer->AddButton(new wxButton(this, wxID_OK, pgTr("common.ok")));
    btnSizer->Realize();

    mainSizer->Add(btnSizer, 0, wxALIGN_RIGHT | wxBOTTOM | wxRIGHT, borderSize);

    const int dialogWidth = FromDIP(dialogWidthDIP);
    SetSizeHints(wxSize(dialogWidth, FromDIP(dialogMinHeight)), wxSize(dialogWidth, -1));
    SetSizer(mainSizer);
    Layout();
    Fit();
}

void DialogRecTypeSelector::populateList(const std::unordered_set<PGPlugin::ModelRecordType>& selectedRecTypes)
{
    long index = 0;
    for (const auto& entry : PGPlugin::availableRecTypeStrs()) {
        index = m_listCtrl->InsertItem(index, wxString(entry));
        const bool isChecked = selectedRecTypes.contains(
            PGPlugin::recTypeFromString(entry)); // check if this rec type is in the selected set

        m_listCtrl->CheckItem(index, isChecked);
        ++index;
    }

    // Set height of dialog to show all items without scrolling (with some padding).
    Layout();

    wxRect rect;
    if (!m_listCtrl->GetItemRect(0, rect, wxLIST_RECT_BOUNDS))
        return;

    // Everything that is not the list itself (instruction text, buttons, borders) - measured rather than assumed
    // because the instruction text needs a different number of lines in each language.
    const int chromeHeight = GetSize().GetHeight() - m_listCtrl->GetSize().GetHeight();
    const int itemHeight = rect.GetHeight();
    const int desiredHeight = (m_listCtrl->GetItemCount() * itemHeight) + chromeHeight + FromDIP(borderSizeDIP);
    // Cap the height to avoid an excessively large dialog.
    SetSize(wxSize(GetSize().x, std::min(desiredHeight, FromDIP(dialogMaxHeight))));
}

std::unordered_set<PGPlugin::ModelRecordType> DialogRecTypeSelector::selectedRecordTypes() const
{
    std::unordered_set<PGPlugin::ModelRecordType> result;

    long item = -1;
    while ((item = m_listCtrl->GetNextItem(item)) != -1) {
        if (m_listCtrl->IsItemChecked(item)) {
            const wxString code = m_listCtrl->GetItemText(item);
            const auto recType = PGPlugin::recTypeFromString(code.ToStdString());
            result.insert(recType);
        }
    }

    return result;
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
