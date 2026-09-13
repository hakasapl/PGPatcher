#include "GUI/DialogModifiableListCtrl.hpp"

#include "GUI/components/PGCustomListctrlChangedEvent.hpp"
#include "GUI/components/PGModifiableListCtrl.hpp"
#include "GUI/components/PGWrappingStaticText.hpp"
#include "PGLocale.hpp"
#include "PGUI.hpp"

#include <string>
#include <vector>

// Disable owning memory checks because wxWidgets will take care of deleting the objects.
// Disable convert member functions to static because these functions need to be non-static for wxWidgets.
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

namespace {
// Sizes in DIPs (pixels at 100% scaling), scaled to the monitor's DPI with FromDIP() where they are used
constexpr int dialogWidthDIP = 300;
constexpr int dialogMinHeight = 300;
constexpr int borderSizeDIP = 10;
// Initial wrap width, kept just under the client width so the first wrap is never narrower than the final one.
constexpr int textWrapWidth = dialogWidthDIP - (4 * borderSizeDIP);
} // namespace

DialogModifiableListCtrl::DialogModifiableListCtrl(wxWindow* parent,
                                                   const wxString& title,
                                                   const wxString& text)
    : wxDialog(parent,
               wxID_ANY,
               title,
               wxDefaultPosition,
               wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_listCtrl(new PGModifiableListCtrl(this,
                                          wxID_ANY,
                                          wxDefaultPosition,
                                          wxDefaultSize,
                                          wxLC_REPORT | wxLC_EDIT_LABELS | wxLC_NO_HEADER))
    , m_helpText(new PGWrappingStaticText(this,
                                          wxID_ANY,
                                          text,
                                          FromDIP(textWrapWidth)))
{
    SetIcons(PGUI::getAppIcons());

    // Pixel sizes are defined for 100% scaling, so scale them to the DPI of the monitor showing the dialog.
    const int borderSize = FromDIP(borderSizeDIP);

    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Add static text for instructions - wraps to the dialog width so that longer translations stay visible.

    mainSizer->Add(m_helpText, 0, wxEXPAND | wxALL, borderSize);

    m_listCtrl->AppendColumn("Item", wxLIST_FORMAT_LEFT, wxLIST_AUTOSIZE_USEHEADER);
    m_listCtrl->SetColumnWidth(0, wxLIST_AUTOSIZE_USEHEADER);

    // Bind resize.
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) -> void {
        updateColumnWidth();
        event.Skip();
    });
    m_listCtrl->Bind(pgEVT_LISTCTRL_CHANGED, [this](PGCustomListctrlChangedEvent& event) -> void {
        updateColumnWidth();
        event.Skip();
    });

    mainSizer->Add(m_listCtrl, 1, wxEXPAND | wxALL, borderSize);

    auto* btnSizer = new wxStdDialogButtonSizer();
    btnSizer->AddButton(new wxButton(this, wxID_CANCEL, pgTr("common.cancel")));
    btnSizer->AddButton(new wxButton(this, wxID_OK, pgTr("common.ok")));
    btnSizer->Realize();

    mainSizer->Add(btnSizer, 0, wxALIGN_RIGHT | wxBOTTOM | wxRIGHT, borderSize);

    SetSizeHints(FromDIP(wxSize(dialogWidthDIP, dialogMinHeight)), wxSize(-1, -1));
    SetSizer(mainSizer);
    Layout();
    Fit();
}

auto DialogModifiableListCtrl::getList() const -> std::vector<std::wstring>
{
    std::vector<std::wstring> result;

    long item = -1;
    while ((item = m_listCtrl->GetNextItem(item)) != -1) {
        const wxString text = m_listCtrl->GetItemText(item);
        if (!text.IsEmpty())
            result.push_back(text.ToStdWstring());
    }

    return result;
}

void DialogModifiableListCtrl::populateList(const std::vector<std::wstring>& items)
{
    m_listCtrl->DeleteAllItems();

    long index = 0;
    for (const auto& item : items) {
        m_listCtrl->InsertItem(index, wxString(item));
        ++index;
    }

    m_listCtrl->InsertItem(m_listCtrl->GetItemCount(), "");
}

void DialogModifiableListCtrl::updateColumnWidth()
{
    if (m_listCtrl == nullptr)
        return;

    if (m_listCtrl->GetColumnCount() > 0) {
        const int clientWidth = m_listCtrl->GetClientSize().GetWidth();
        m_listCtrl->SetColumnWidth(0, clientWidth);
    }
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
