#include "GUI/DialogTextureMapListCtrl.hpp"

#include "GUI/components/PGCustomListctrlChangedEvent.hpp"
#include "GUI/components/PGTextureMapListCtrl.hpp"
#include "GUI/components/PGWrappingStaticText.hpp"
#include "PGLocale.hpp"
#include "PGUI.hpp"
#include "pgutil/PGEnums.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include <wx/listbase.h>

// Disable owning memory checks because wxWidgets will take care of deleting the objects.
// Disable convert member functions to static because these functions need to be non-static for wxWidgets.
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

namespace {
// Sizes in DIPs (pixels at 100% scaling), scaled to the monitor's DPI with FromDIP() where they are used
constexpr int dialogWidthDIP = 500;
constexpr int dialogMinHeight = 300;
constexpr int borderSizeDIP = 10;
constexpr int typeColumnWidth = 150;
constexpr int pathColumnMinWidth = 50;
// Initial wrap width, kept just under the client width so the first wrap is never narrower than the final one.
constexpr int textWrapWidth = dialogWidthDIP - (4 * borderSizeDIP);
} // namespace

DialogTextureMapListCtrl::DialogTextureMapListCtrl(wxWindow* parent,
                                                   const wxString& title,
                                                   const wxString& text)
    : wxDialog(parent,
               wxID_ANY,
               title,
               wxDefaultPosition,
               wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_listCtrl(new PGTextureMapListCtrl(this,
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

    m_listCtrl->AppendColumn("Texture Maps");
    m_listCtrl->AppendColumn("Type", wxLIST_FORMAT_LEFT, FromDIP(typeColumnWidth));

    // Bind resize.
    Bind(wxEVT_SIZE, [this]([[maybe_unused]] wxSizeEvent& event) -> void {
        updateColumnWidths();
        event.Skip();
    });
    m_listCtrl->Bind(pgEVT_LISTCTRL_CHANGED, [this](PGCustomListctrlChangedEvent& event) -> void {
        updateColumnWidths();
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

auto DialogTextureMapListCtrl::getList() const -> std::vector<std::pair<std::wstring,
                                                                        PGEnums::TextureType>>
{
    std::vector<std::pair<std::wstring, PGEnums::TextureType>> result;

    long item = -1;
    while ((item = m_listCtrl->GetNextItem(item)) != -1) {
        const wxString texturePath = m_listCtrl->GetItemText(item, 0);
        if (texturePath.IsEmpty())
            continue; // skip empty line

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
    if (m_listCtrl == nullptr)
        return;

    // Get current total width of the list control.
    const int totalWidth = m_listCtrl->GetClientSize().GetWidth();

    // Get current width of second column (assume fixed).
    int col1Width = 0;
    col1Width = m_listCtrl->GetColumnWidth(1);

    // Set first column width to fill remaining space.
    int newCol0Width = totalWidth - col1Width;
    newCol0Width = std::max(newCol0Width, FromDIP(pathColumnMinWidth)); // optional minimum width
    m_listCtrl->SetColumnWidth(0, newCol0Width);
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
