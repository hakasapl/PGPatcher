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
    , m_sortAscending(true)
{
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);
    // Create the m_listCtrl
    m_listCtrl = new CheckedColorDragListCtrl(
        this, wxID_ANY, wxDefaultPosition, wxSize(DEFAULT_WIDTH, DEFAULT_HEIGHT), wxLC_REPORT);
    m_listCtrl->InsertColumn(0, "Mod");
    m_listCtrl->InsertColumn(1, "Shader");
    m_listCtrl->InsertColumn(2, "Priority");

    m_listCtrl->Bind(wxEVT_LIST_ITEM_SELECTED, &ModSortDialog::onItemSelected, this);
    m_listCtrl->Bind(wxEVT_LIST_ITEM_DESELECTED, &ModSortDialog::onItemDeselected, this);
    m_listCtrl->Bind(wxEVT_LIST_COL_CLICK, &ModSortDialog::onColumnClick, this);
    m_listCtrl->Bind(wxEVT_LIST_ITEM_DRAGGED, &ModSortDialog::resetIndices, this);

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

        // Priority Column
        if (mods[i]->priority > 0) {
            m_listCtrl->SetItem(index, 2, to_string(mods[i]->priority));
        } else if (!foundCutoff) {
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

    // Calculate minimum width for each column
    const int col1Width = calculateColumnWidth(0);
    m_listCtrl->SetColumnWidth(0, col1Width);
    const int col2Width = calculateColumnWidth(1);
    m_listCtrl->SetColumnWidth(1, col2Width);
    const int col3Width = calculateColumnWidth(2);
    m_listCtrl->SetColumnWidth(2, col3Width);

    const int scrollBarWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);
    const int totalWidth = col1Width + col2Width + col3Width + (DEFAULT_PADDING * 2) + scrollBarWidth; // Extra padding

    // Add wrapped message at the top
    static const std::wstring message
        = L"Please sort your mods to determine what mod PG uses to patch meshes where conflicts occur. Mods with "
          L"higher priority number win over lower priority mods.";
    // Create the wxStaticText and set wrapping
    auto* messageText = new wxStaticText(this, wxID_ANY, message, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    messageText->Wrap(totalWidth - (DEFAULT_PADDING * 2)); // Adjust wrapping width

    // Let wxWidgets automatically calculate the best height based on wrapped text
    messageText->SetMinSize(wxSize(totalWidth - (DEFAULT_PADDING * 2), messageText->GetBestSize().y));

    // Add the static text to the main sizer
    mainSizer->Add(messageText, 0, wxALL, DEFAULT_BORDER);

    // Add checkbox to show/hide disabled mods
    auto* disableModCheckbox = new wxCheckBox(this, wxID_ANY, "Show Disabled Mods");
    disableModCheckbox->Bind(wxEVT_CHECKBOX, &ModSortDialog::onShowDisabledModsToggled, this);
    mainSizer->Add(disableModCheckbox, 0, wxALL, DEFAULT_BORDER);

    // Adjust dialog width to match the total width of columns and padding
    SetSizeHints(totalWidth, DEFAULT_HEIGHT, totalWidth, wxDefaultCoord); // Adjust minimum width and height
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

// HELPERS

void ModSortDialog::resetIndices([[maybe_unused]] ItemDraggedEvent& event)
{
    // loop through each item in list and set col 3
    size_t priority = 0;

    long minI = 0;
    long maxI = m_listCtrl->GetItemCount() - 1;
    long step = 1;
    if (!m_sortAscending) {
        minI = maxI;
        maxI = 0;
        step = -1;
    }

    for (long i = minI; i != maxI; i += step) {
        if (!m_listCtrl->isChecked(i)) {
            continue; // Skip disabled items
        }

        m_listCtrl->SetItem(i, 2, std::to_string(++priority));
    }
}

void ModSortDialog::onColumnClick(wxListEvent& event)
{
    const int column = event.GetColumn();

    // Toggle sort order if the same column is clicked, otherwise reset to ascending
    if (column == 2) {
        // Only sort priority col
        m_sortAscending = !m_sortAscending;

        // Reverse the order of every item
        reverseListOrder();
    }

    event.Skip();
}

void ModSortDialog::reverseListOrder()
{
    // Store all items in a vector
    std::vector<std::vector<wxString>> items;
    std::vector<wxColour> backgroundColors;

    for (long i = 0; i < m_listCtrl->GetItemCount(); ++i) {
        std::vector<wxString> row;
        row.reserve(m_listCtrl->GetColumnCount());
        for (int col = 0; col < m_listCtrl->GetColumnCount(); ++col) {
            row.push_back(m_listCtrl->GetItemText(i, col));
        }
        items.push_back(row);

        // Store original background color using the mod name
        const std::wstring itemText = row[0].ToStdWstring();
        auto it = m_originalBackgroundColors.find(itemText);
        if (it != m_originalBackgroundColors.end()) {
            backgroundColors.push_back(it->second);
        } else {
            backgroundColors.push_back(*wxWHITE); // Default color if not found
        }
    }

    // Clear the m_listCtrl
    m_listCtrl->DeleteAllItems();

    // Insert items back in reverse order and set background colors
    for (size_t i = 0; i < items.size(); ++i) {
        const long newIndex = m_listCtrl->InsertItem(m_listCtrl->GetItemCount(), items[items.size() - 1 - i][0]);
        for (int col = 1; col < m_listCtrl->GetColumnCount(); ++col) {
            m_listCtrl->SetItem(newIndex, col, items[items.size() - 1 - i][col]);
        }

        // Set background color for the new item
        m_listCtrl->SetItemBackgroundColour(newIndex, backgroundColors[items.size() - 1 - i]);
    }
}

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

    if (!m_sortAscending) {
        // reverse items if descending
        std::ranges::reverse(sortedItems);
    }

    return sortedItems;
}

void ModSortDialog::onClose([[maybe_unused]] wxCloseEvent& event)
{
    ParallaxGenHandlers::nonBlockingExit();
    wxTheApp->Exit();
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
