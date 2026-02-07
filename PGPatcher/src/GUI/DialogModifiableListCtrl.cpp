#include "GUI/DialogModifiableListCtrl.hpp"

#include "GUI/components/PGCustomListctrlChangedEvent.hpp"
#include "GUI/components/PGModifiableListCtrl.hpp"

#include <string>
#include <vector>

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)

DialogModifiableListCtrl::DialogModifiableListCtrl(wxWindow* parent, const wxString& title, const wxString& text)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(300, 400), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_listCtrl(new PGModifiableListCtrl(
          this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_EDIT_LABELS | wxLC_NO_HEADER))
{
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Add static text for instructions
    m_helpText = new wxStaticText(this, wxID_ANY, text);
    // wrap text around 300 px
    m_helpText->Wrap(260);
    m_helpText->SetMinSize(wxSize(-1, 60)); // TODO can this be dynamic?
    mainSizer->Add(m_helpText, 0, wxALL, 10);

    m_listCtrl->AppendColumn("Item", wxLIST_FORMAT_LEFT, wxLIST_AUTOSIZE_USEHEADER);
    m_listCtrl->SetColumnWidth(0, wxLIST_AUTOSIZE_USEHEADER);

    // Bind resize
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) -> void {
        updateColumnWidth();
        event.Skip();
    });
    m_listCtrl->Bind(pgEVT_LISTCTRL_CHANGED, [this](PGCustomListctrlChangedEvent& event) -> void {
        updateColumnWidth();
        event.Skip();
    });

    mainSizer->Add(m_listCtrl, 1, wxEXPAND | wxALL, 10);

    auto* btnSizer = new wxStdDialogButtonSizer();
    btnSizer->AddButton(new wxButton(this, wxID_CANCEL));
    btnSizer->AddButton(new wxButton(this, wxID_OK));
    btnSizer->Realize();

    mainSizer->Add(btnSizer, 0, wxALIGN_RIGHT | wxBOTTOM | wxRIGHT, 10);

    SetSizeHints(wxSize(300, 300), wxSize(-1, -1));
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
        if (!text.IsEmpty()) {
            result.push_back(text.ToStdWstring());
        }
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
    if (m_listCtrl == nullptr) {
        return;
    }

    if (m_listCtrl->GetColumnCount() > 0) {
        const int clientWidth = m_listCtrl->GetClientSize().GetWidth();
        m_listCtrl->SetColumnWidth(0, clientWidth);
    }
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)
