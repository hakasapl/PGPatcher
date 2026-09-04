#include "GUI/DialogTextureMapListCtrl.hpp"

#include "GUI/components/PGCustomListctrlChangedEvent.hpp"
#include "GUI/components/PGTextureMapListCtrl.hpp"
#include "GUI/components/PGWrappingStaticText.hpp"
#include "PGLocale.hpp"
#include "pgutil/PGEnums.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include <wx/listbase.h>

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)

namespace {
constexpr int DIALOG_WIDTH = 500;
constexpr int DIALOG_HEIGHT = 400;
constexpr int DIALOG_MIN_HEIGHT = 300;
constexpr int BORDER_SIZE = 10;
constexpr int TYPE_COLUMN_WIDTH = 150;
constexpr int PATH_COLUMN_MIN_WIDTH = 50;
// Initial wrap width, kept just under the client width so the first wrap is never narrower than the final one
constexpr int TEXT_WRAP_WIDTH = DIALOG_WIDTH - (4 * BORDER_SIZE);
} // namespace

DialogTextureMapListCtrl::DialogTextureMapListCtrl(wxWindow* parent,
                                                   const wxString& title,
                                                   const wxString& text)
    : wxDialog(parent,
               wxID_ANY,
               title,
               wxDefaultPosition,
               wxSize(DIALOG_WIDTH,
                      DIALOG_HEIGHT),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_listCtrl(new PGTextureMapListCtrl(this,
                                          wxID_ANY,
                                          wxDefaultPosition,
                                          wxDefaultSize,
                                          wxLC_REPORT | wxLC_EDIT_LABELS | wxLC_NO_HEADER))
{
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Add static text for instructions - wraps to the dialog width so that longer translations stay visible
    m_helpText = new PGWrappingStaticText(this, wxID_ANY, text, TEXT_WRAP_WIDTH);
    mainSizer->Add(m_helpText, 0, wxEXPAND | wxALL, BORDER_SIZE);

    m_listCtrl->AppendColumn("Texture Maps");
    m_listCtrl->AppendColumn("Type", wxLIST_FORMAT_LEFT, TYPE_COLUMN_WIDTH);

    // Bind resize
    Bind(wxEVT_SIZE, [this]([[maybe_unused]] wxSizeEvent& event) -> void {
        updateColumnWidths();
        event.Skip();
    });
    m_listCtrl->Bind(pgEVT_LISTCTRL_CHANGED, [this](PGCustomListctrlChangedEvent& event) -> void {
        updateColumnWidths();
        event.Skip();
    });

    mainSizer->Add(m_listCtrl, 1, wxEXPAND | wxALL, BORDER_SIZE);

    auto* btnSizer = new wxStdDialogButtonSizer();
    btnSizer->AddButton(new wxButton(this, wxID_CANCEL, PGTr("common.cancel", "Cancel")));
    btnSizer->AddButton(new wxButton(this, wxID_OK, PGTr("common.ok", "OK")));
    btnSizer->Realize();

    mainSizer->Add(btnSizer, 0, wxALIGN_RIGHT | wxBOTTOM | wxRIGHT, BORDER_SIZE);

    SetSizeHints(wxSize(DIALOG_WIDTH, DIALOG_MIN_HEIGHT), wxSize(-1, -1));
    SetSizer(mainSizer);
    Layout();
    Fit();
}

auto DialogTextureMapListCtrl::getList() const -> std::vector<std::pair<std::wstring,
                                                                        PGEnums::TextureType>>
{
    std::vector<std::pair<std::wstring, PGEnums::TextureType>> result;

    long item = -1;
    while ((item = m_listCtrl->GetNextItem(item)) != -1) {
        const wxString texturePath = m_listCtrl->GetItemText(item, 0);
        if (texturePath.IsEmpty()) {
            continue; // skip empty line
        }

        const wxString textureTypeStr = m_listCtrl->GetItemText(item, 1);
        const auto textureType = PGEnums::getTexTypeFromStr(textureTypeStr.ToStdString());
        result.emplace_back(texturePath.ToStdWstring(), textureType);
    }

    return result;
}

void DialogTextureMapListCtrl::populateList(const std::vector<std::pair<std::wstring,
                                                                        PGEnums::TextureType>>& items)
{
    m_listCtrl->DeleteAllItems();
    for (const auto& textureRule : items) {
        const auto newIndex = m_listCtrl->InsertItem(m_listCtrl->GetItemCount(), textureRule.first);
        m_listCtrl->SetItem(newIndex, 1, PGEnums::getStrFromTexType(textureRule.second));
    }

    m_listCtrl->InsertItem(m_listCtrl->GetItemCount(), ""); // Add empty line
}

void DialogTextureMapListCtrl::updateColumnWidths()
{
    if (m_listCtrl == nullptr) {
        return;
    }

    // Get current total width of the list control
    const int totalWidth = m_listCtrl->GetClientSize().GetWidth();

    // Get current width of second column (assume fixed)
    int col1Width = 0;
    col1Width = m_listCtrl->GetColumnWidth(1);

    // Set first column width to fill remaining space
    int newCol0Width = totalWidth - col1Width;
    newCol0Width = std::max(newCol0Width, PATH_COLUMN_MIN_WIDTH); // optional minimum width
    m_listCtrl->SetColumnWidth(0, newCol0Width);
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers)
