#include <algorithm>

#include "GUI/ModSortDialog.hpp"
#include "ModManagerDirectory.hpp"
#include "PGGlobals.hpp"
#include "ParallaxGenHandlers.hpp"
#include "util/NIFUtil.hpp"

using namespace std;

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

// class ModSortDialog
ModSortDialog::ModSortDialog()
    : wxDialog(nullptr, wxID_ANY, "Set Mod Priority", wxDefaultPosition, wxSize(DEFAULT_WIDTH, DEFAULT_HEIGHT),
          wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP | wxRESIZE_BORDER)
{
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);
    // Create the m_listCtrl
    m_listCtrl = new CheckedColorDragListCtrl(
        this, wxID_ANY, wxDefaultPosition, wxSize(DEFAULT_WIDTH, DEFAULT_HEIGHT), wxLC_REPORT);
    m_listCtrl->InsertColumn(0, "Mod");
    m_listCtrl->InsertColumn(1, "Shader");

    m_listCtrl->Bind(wxEVT_LIST_ITEM_SELECTED, &ModSortDialog::onItemSelected, this);
    m_listCtrl->Bind(wxEVT_LIST_ITEM_DESELECTED, &ModSortDialog::onItemDeselected, this);

    const auto mods = PGGlobals::getMMD()->getModsByPriority();
    bool foundCutoff = false;
    for (size_t i = 0; i < mods.size(); ++i) {
        const long index = m_listCtrl->InsertItem(static_cast<long>(i), mods[i]->name);

        // Shader Column
        string shaderStr;
        for (const auto& shader : mods[i]->shaders) {
            if (!shaderStr.empty()) {
                shaderStr += ", ";
            }
            shaderStr += NIFUtil::getStrFromShader(shader);
        }
        m_listCtrl->SetItem(index, 1, shaderStr);

        // Priority to find cutoff
        if (mods[i]->priority < 0 && !foundCutoff) {
            m_listCtrl->setCutoffLine(static_cast<int>(i));
            foundCutoff = true;
        }

        // Set highlight if new
        if (mods[i]->isNew) {
            m_listCtrl->SetItemBackgroundColour(index, *wxGREEN); // Highlight color
            m_originalBackgroundColors[mods[i]->name] = *wxGREEN; // Store the original color using the mod name
        } else {
            m_originalBackgroundColors[mods[i]->name] = *wxWHITE; // Store the original color using the mod name
        }

        // Check if enabled
        m_listCtrl->check(index, mods[i]->isEnabled);
    }

    m_listCtrl->Bind(wxEVT_SIZE, &ModSortDialog::onListCtrlResize, this);

    // Calculate minimum width for each column
    const int col1Width = calculateColumnWidth(0);
    const int col2Width = calculateColumnWidth(1);
    m_listCtrl->SetColumnWidth(1, col1Width);
    const int scrollBarWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);
    const int totalWidth = col1Width + col2Width + (DEFAULT_PADDING * 2) + scrollBarWidth; // Extra padding

    // Add wrapped message at the top
    static const std::wstring message = L"Please sort your mods to determine what mod PG uses to patch meshes where "
                                        L"conflicts occur. Mods on top of the list win over mods below them.";
    // Create the wxStaticText and set wrapping
    auto* messageText = new wxStaticText(this, wxID_ANY, message, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    messageText->Wrap(totalWidth - (DEFAULT_PADDING * 2)); // Adjust wrapping width

    // Let wxWidgets automatically calculate the best height based on wrapped text
    messageText->SetMinSize(wxSize(totalWidth - (DEFAULT_PADDING * 2), messageText->GetBestSize().y));

    // Add the static text to the main sizer
    mainSizer->Add(messageText, 0, wxALL, DEFAULT_BORDER);

    // Adjust dialog width to match the total width of columns and padding
    SetSizeHints(totalWidth, wxDefaultCoord, wxDefaultCoord, wxDefaultCoord); // Adjust minimum width and height
    SetSize(totalWidth, DEFAULT_HEIGHT); // Set dialog size

    mainSizer->Add(m_listCtrl, 1, wxEXPAND | wxALL, DEFAULT_BORDER);

    // Add OK button
    auto* okButton = new wxButton(this, wxID_OK, "OK");
    mainSizer->Add(okButton, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, DEFAULT_BORDER);
    Bind(wxEVT_CLOSE_WINDOW, &ModSortDialog::onClose, this);

    SetSizer(mainSizer);
}

// EVENT HANDLERS

void ModSortDialog::onItemSelected(wxListEvent& event)
{
    const long index = event.GetIndex();
    const std::wstring selectedMod = m_listCtrl->GetItemText(index).ToStdWstring();

    if (index == -1) {
        clearAllHighlights(); // Clear all highlights when no item is selected
    } else {
        highlightConflictingItems(selectedMod); // Highlight conflicts for the selected mod
    }
}

void ModSortDialog::onItemDeselected(wxListEvent& event)
{
    // Check if no items are selected
    long selectedItem = -1;
    bool isAnyItemSelected = false;
    while (
        (selectedItem = m_listCtrl->GetNextItem(selectedItem, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != wxNOT_FOUND) {
        isAnyItemSelected = true;
        break;
    }

    if (!isAnyItemSelected) {
        clearAllHighlights(); // Clear highlights if no items are selected
    }

    event.Skip();
}

void ModSortDialog::clearAllHighlights()
{
    for (long i = 0; i < m_listCtrl->GetItemCount(); ++i) {
        const std::wstring itemText = m_listCtrl->GetItemText(i).ToStdWstring();
        auto it = m_originalBackgroundColors.find(itemText);
        if (it != m_originalBackgroundColors.end()) {
            m_listCtrl->SetItemBackgroundColour(i, it->second); // Restore original color
        } else {
            m_listCtrl->SetItemBackgroundColour(i, *wxWHITE); // Fallback to white
        }
    }
}

void ModSortDialog::highlightConflictingItems(const std::wstring& selectedMod)
{
    // Clear previous highlights and restore original colors
    for (long i = 0; i < m_listCtrl->GetItemCount(); ++i) {
        const std::wstring itemText = m_listCtrl->GetItemText(i).ToStdWstring();
        auto it = m_originalBackgroundColors.find(itemText);
        if (it != m_originalBackgroundColors.end()) {
            m_listCtrl->SetItemBackgroundColour(i, it->second); // Restore original color
        } else {
            m_listCtrl->SetItemBackgroundColour(i, *wxWHITE); // Fallback to white if not found
        }
    }

    // Highlight selected item and its conflicts
    auto* mmd = PGGlobals::getMMD();
    auto conflictSet = mmd->getMod(selectedMod)->conflicts;
    // convert conflictSet to unordered set of strings
    unordered_set<std::wstring> conflictSetStr;
    for (const auto& conflict : conflictSet) {
        conflictSetStr.insert(conflict->name);
    }

    for (long i = 0; i < m_listCtrl->GetItemCount(); ++i) {
        const std::wstring itemText = m_listCtrl->GetItemText(i).ToStdWstring();
        if (itemText == selectedMod || conflictSetStr.contains(itemText)) {
            m_listCtrl->SetItemBackgroundColour(i, *wxYELLOW); // Highlight color
        }
    }
}

void ModSortDialog::onShowDisabledModsToggled(wxCommandEvent& event) { }

void ModSortDialog::onListCtrlResize(wxSizeEvent& event)
{
    const int totalWidth = m_listCtrl->GetClientSize().GetWidth();

    // Get the widths of the fixed columns
    const int col1Width = m_listCtrl->GetColumnWidth(1);
    const int col2Width = m_listCtrl->GetColumnWidth(2);

    // Calculate remaining width for first column
    int col0Width = totalWidth - col1Width - col2Width - 2; // optional small padding for borders

    col0Width = std::max(col0Width, 50); // minimum width to avoid clipping

    m_listCtrl->SetColumnWidth(0, col0Width);

    event.Skip(); // allow default processing
}

// HELPERS

auto ModSortDialog::calculateColumnWidth(int colIndex) -> int
{
    int maxWidth = 0;
    wxClientDC dc(m_listCtrl);
    dc.SetFont(m_listCtrl->GetFont());

    for (int i = 0; i < m_listCtrl->GetItemCount(); ++i) {
        const wxString itemText = m_listCtrl->GetItemText(i, colIndex);
        int width = 0;
        int height = 0;
        dc.GetTextExtent(itemText, &width, &height);
        maxWidth = std::max(width, maxWidth);
    }
    return maxWidth + DEFAULT_PADDING; // Add some padding
}

auto ModSortDialog::getSortedItems() const -> std::vector<std::wstring>
{
    std::vector<std::wstring> sortedItems;
    sortedItems.reserve(m_listCtrl->GetItemCount());
    for (long i = 0; i < m_listCtrl->GetItemCount(); ++i) {
        sortedItems.push_back(m_listCtrl->GetItemText(i).ToStdWstring());
    }

    return sortedItems;
}

void ModSortDialog::onClose([[maybe_unused]] wxCloseEvent& event)
{
    ParallaxGenHandlers::nonBlockingExit();
    wxTheApp->Exit();
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
