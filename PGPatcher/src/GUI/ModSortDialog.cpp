#include <algorithm>
#include <wx/arrstr.h>

#include "GUI/ModSortDialog.hpp"
#include "GUI/components/CheckedColorDragListCtrl.hpp"
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

    m_listCtrl->Bind(pgEVT_CCDLC_ITEM_DRAGGED, &ModSortDialog::onItemDragged, this);
    m_listCtrl->Bind(pgEVT_CCDLC_ITEM_CHECKED, &ModSortDialog::onItemChecked, this);

    m_listCtrl->Bind(wxEVT_SIZE, &ModSortDialog::onListCtrlResize, this);

    // Add wrapped message at the top
    static const std::wstring message = L"Please sort your mods to determine what mod PG uses to patch meshes where.";
    // Create the wxStaticText and set wrapping
    auto* messageText = new wxStaticText(this, wxID_ANY, message, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);

    // Add the static text to the main sizer
    mainSizer->Add(messageText, 0, wxALL, DEFAULT_BORDER);

    // TOP RECTANGLE
    auto* topPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    topPanel->SetBackgroundColour(*wxGREEN);
    auto* topLabel
        = new wxStaticText(topPanel, wxID_ANY, "Winning Mods", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);

    // FONT for rects
    wxFont rectFont = topLabel->GetFont(); // start with current font
    rectFont.SetPointSize(RECT_LABEL_FONT_SIZE); // increase by 4 points
    rectFont.SetWeight(wxFONTWEIGHT_BOLD);
    topLabel->SetFont(rectFont);

    // Use a box sizer to center the text in the panel
    auto* topSizer = new wxBoxSizer(wxHORIZONTAL);
    topSizer->Add(topLabel, 1, wxALIGN_CENTER | wxALL, 2);
    topPanel->SetSizer(topSizer);

    // Add top rectangle to main sizer
    mainSizer->Add(topPanel, 0, wxEXPAND | wxBOTTOM, 0); // No bottom border so it touches the list

    // Add List control
    mainSizer->Add(m_listCtrl, 1, wxEXPAND | wxALL, 0);

    // BOTTOM RECTANGLE
    auto* bottomPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    bottomPanel->SetBackgroundColour(*wxRED);
    auto* bottomLabel
        = new wxStaticText(bottomPanel, wxID_ANY, "Losing Mods", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);

    bottomLabel->SetFont(rectFont);

    // Center the text in the panel
    auto* bottomSizer = new wxBoxSizer(wxHORIZONTAL);
    bottomSizer->Add(bottomLabel, 1, wxALIGN_CENTER | wxALL, 2);
    bottomPanel->SetSizer(bottomSizer);

    // Add bottom rectangle to main sizer
    mainSizer->Add(bottomPanel, 0, wxEXPAND | wxTOP, 0); // No top border so it touches the list

    // Create button sizer for horizontal layout
    auto* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

    // Add "Restore to Default Order" button
    auto* restoreButton = new wxButton(this, wxID_ANY, "Restore PGPatcher Defaults");
    buttonSizer->Add(restoreButton, 0, wxALL, DEFAULT_BORDER);
    restoreButton->Bind(wxEVT_BUTTON, &ModSortDialog::onRestoreDefault, this);
    restoreButton->SetToolTip("For MO2 default order is your loose file order. For vortex default order is by shader, "
                              "then by name alphabetically.");

    // Add discard changes button
    auto* discardButton = new wxButton(this, wxID_ANY, "Discard Changes");
    buttonSizer->Add(discardButton, 0, wxALL, DEFAULT_BORDER);
    discardButton->Bind(wxEVT_BUTTON, &ModSortDialog::onDiscardChanges, this);

    // Add stretchable space
    buttonSizer->AddStretchSpacer(1);

    // Add apply button
    m_applyButton = new wxButton(this, wxID_APPLY, "Apply");
    buttonSizer->Add(m_applyButton, 0, wxALL, DEFAULT_BORDER);
    m_applyButton->Bind(wxEVT_BUTTON, &ModSortDialog::onApply, this);

    // Disable apply button by default
    m_applyButton->Enable(false);

    // Add OK button
    auto* okButton = new wxButton(this, wxID_OK, "Okay");
    buttonSizer->Add(okButton, 0, wxALL, DEFAULT_BORDER);
    okButton->Bind(wxEVT_BUTTON, &ModSortDialog::onOkay, this);

    // Add to main sizer
    mainSizer->Add(buttonSizer, 0, wxEXPAND | wxALL, 0);

    Bind(wxEVT_CLOSE_WINDOW, &ModSortDialog::onClose, this);

    // Fill contents
    fillListCtrl(PGGlobals::getMMD()->getModsByPriority(), false);

    // Calculate minimum width for each column
    const int col1Width = calculateColumnWidth(0);
    const int col2Width = calculateColumnWidth(1);
    m_listCtrl->SetColumnWidth(1, col1Width);
    const int scrollBarWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);
    const int totalWidth = col1Width + col2Width + (DEFAULT_PADDING * 2) + scrollBarWidth; // Extra padding

    // Adjust dialog width to match the total width of columns and padding
    SetSizeHints(MIN_WIDTH, MIN_HEIGHT, wxDefaultCoord, wxDefaultCoord); // Adjust minimum width and height
    SetSize(totalWidth, DEFAULT_HEIGHT); // Set dialog size

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

    // Find index of selected item
    long selectedIndex = -1;
    for (long i = 0; i < m_listCtrl->GetItemCount(); ++i) {
        if (m_listCtrl->GetItemText(i).ToStdWstring() == selectedMod) {
            selectedIndex = i;
            break;
        }
    }

    if (selectedIndex == -1) {
        return; // Selected item not found
    }

    // Apply highlights
    for (long i = 0; i < m_listCtrl->GetItemCount(); ++i) {
        const std::wstring itemText = m_listCtrl->GetItemText(i).ToStdWstring();
        if (!conflictSetStr.contains(itemText)) {
            continue; // Skip non-conflicting items
        }

        if (itemText != selectedMod) {
            if (i < selectedIndex) {
                m_listCtrl->SetItemBackgroundColour(i, s_LOSING_MOD_COLOR); // Red-ish for conflicts above
            } else {
                m_listCtrl->SetItemBackgroundColour(i, s_WINNING_MOD_COLOR); // Yellow-ish for conflicts below
            }
        }
    }
}

void ModSortDialog::onListCtrlResize(wxSizeEvent& event)
{
    const int totalWidth = m_listCtrl->GetClientSize().GetWidth();

    // Get the widths of the fixed columns
    const int col1Width = m_listCtrl->GetColumnWidth(1);
    const int col2Width = m_listCtrl->GetColumnWidth(2);

    // Calculate remaining width for first column
    int col0Width = totalWidth - col1Width - col2Width - 2; // optional small padding for borders

    col0Width = std::max(col0Width, MIN_COL_WIDTH); // minimum width to avoid clipping

    m_listCtrl->SetColumnWidth(0, col0Width);

    event.Skip(); // allow default processing
}

void ModSortDialog::onItemDragged(ItemDraggedEvent& event)
{
    updateApplyButtonState();
    event.Skip();
}

void ModSortDialog::onItemChecked(ItemCheckedEvent& event)
{
    updateApplyButtonState();
    event.Skip();
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

void ModSortDialog::onClose([[maybe_unused]] wxCloseEvent& event)
{
    ParallaxGenHandlers::nonBlockingExit();
    wxTheApp->Exit();
}

void ModSortDialog::onOkay([[maybe_unused]] wxCommandEvent& event)
{
    updateMods();
    EndModal(wxID_OK);
}

void ModSortDialog::onApply([[maybe_unused]] wxCommandEvent& event) { updateMods(); }

void ModSortDialog::onRestoreDefault([[maybe_unused]] wxCommandEvent& event)
{
    // confirm with modal
    const int response
        = wxMessageBox("Are you sure you want to restore default mod order and enable any manually disabled mods?",
            "Confirm Restore Default Order", wxYES_NO | wxICON_QUESTION, this);

    if (response == wxYES) {
        fillListCtrl(PGGlobals::getMMD()->getModsByDefaultOrder(), true);
    }
}

void ModSortDialog::onDiscardChanges([[maybe_unused]] wxCommandEvent& event)
{
    const int response = wxMessageBox(
        "Are you sure you want to discard all changes?", "Confirm Discard Changes", wxYES_NO | wxICON_QUESTION, this);

    if (response == wxYES) {
        fillListCtrl(PGGlobals::getMMD()->getModsByPriority(), false);
    }
}

void ModSortDialog::updateMods()
{
    // loop through each element in the list ctrl and update the mod manager directory
    auto* mmd = PGGlobals::getMMD();
    const long itemCount = m_listCtrl->GetItemCount();
    for (long i = 0; i < itemCount; ++i) {
        const std::wstring modName = m_listCtrl->GetItemText(i, 0).ToStdWstring();
        auto mod = mmd->getMod(modName);
        mod->isEnabled = m_listCtrl->isChecked(i);

        if (mod != nullptr && mod->isEnabled) {
            mod->priority = static_cast<int>(itemCount - i);
        }
    }

    updateApplyButtonState();
}

void ModSortDialog::updateApplyButtonState()
{
    bool btnState = false;

    // loop through each element in the list ctrl and update the mod manager directory
    auto* mmd = PGGlobals::getMMD();
    const long itemCount = m_listCtrl->GetItemCount();
    for (long i = 0; i < itemCount; ++i) {
        const std::wstring modName = m_listCtrl->GetItemText(i, 0).ToStdWstring();
        auto mod = mmd->getMod(modName);

        if (mod->isEnabled != m_listCtrl->isChecked(i)) {
            btnState = true;
            break;
        }

        if (mod->isEnabled && mod->priority != static_cast<int>(itemCount - i)) {
            btnState = true;
            break;
        }
    }

    m_applyButton->Enable(btnState);
}

void ModSortDialog::fillListCtrl(
    const std::vector<std::shared_ptr<ModManagerDirectory::Mod>>& modList, const bool& autoEnable)
{
    m_listCtrl->DeleteAllItems();

    long listIdx = 0;

    std::vector<std::shared_ptr<ModManagerDirectory::Mod>> disabledMods;

    for (const auto& mod : modList) {
        const auto shaders = mod->shaders;
        if (shaders.empty()) {
            // no shaders, so no need to include in list
            continue;
        }

        if (!mod->isEnabled) {
            bool modEnabled = false;

            // see if we need to autoenable (max variant is greater than 1 which is NONE shader)
            if (autoEnable) {
                const auto maxVariant = shaders.empty() ? 0 : static_cast<int>(*std::ranges::max_element(shaders));
                if (maxVariant > 1) {
                    modEnabled = true;
                }
            }

            if (!modEnabled) {
                disabledMods.push_back(mod);
                continue;
            }
        }

        // Anything past this point the mod is assumed to be enabled

        const long index = m_listCtrl->InsertItem(listIdx, mod->name);

        // Shader Column
        m_listCtrl->SetItem(index, 1, constructShaderString(shaders));

        // Set highlight if new
        if (mod->isNew) {
            m_listCtrl->SetItemBackgroundColour(index, s_NEW_MOD_COLOR); // Highlight color
            m_originalBackgroundColors[mod->name] = s_NEW_MOD_COLOR; // Store the original color using the mod name
        } else {
            m_originalBackgroundColors[mod->name] = *wxWHITE; // Store the original color using the mod name
        }

        // Enable checkbox
        m_listCtrl->check(index, true);

        // iterate listIdx
        listIdx++;
    }

    // Set cutoff line
    m_listCtrl->setCutoffLine(listIdx);

    // loop through inactive mods
    for (const auto& mod : disabledMods) {
        const long index = m_listCtrl->InsertItem(listIdx, mod->name);

        // Shader Column
        m_listCtrl->SetItem(index, 1, constructShaderString(mod->shaders));

        // Set highlight if new
        if (mod->isNew) {
            m_listCtrl->SetItemBackgroundColour(index, s_NEW_MOD_COLOR); // Highlight color
            m_originalBackgroundColors[mod->name] = s_NEW_MOD_COLOR; // Store the original color using the mod name
        } else {
            m_originalBackgroundColors[mod->name] = *wxWHITE; // Store the original color using the mod name
        }

        // Disable checkbox
        m_listCtrl->check(index, false);

        // iterate listIdx
        listIdx++;
    }

    updateApplyButtonState();
}

auto ModSortDialog::constructShaderString(const std::set<NIFUtil::ShapeShader>& shaders) -> std::string
{
    std::string shaderStr;
    for (const auto& shader : shaders) {
        if (!shaderStr.empty()) {
            shaderStr += ", ";
        }
        shaderStr += NIFUtil::getStrFromShader(shader);
    }
    return shaderStr;
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
