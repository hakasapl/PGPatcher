#include "GUI/ModSortDialog.hpp"

#include "GUI/components/PGCheckedDragListCtrl.hpp"
#include "GUI/components/PGCheckedDragListCtrlEvtItemChecked.hpp"
#include "GUI/components/PGCheckedDragListCtrlEvtItemDragged.hpp"
#include "GUI/components/PGCheckedDragListCtrlEvtMeshesIgnoredChanged.hpp"
#include "PGConfig.hpp"
#include "PGGlobals.hpp"
#include "PGModManager.hpp"
#include "PGPatcherGlobals.hpp"
#include "pgutil/PGEnums.hpp"

#include <wx/gdicmn.h>
#include <wx/settings.h>
#include <wx/toplevel.h>
#include <wx/wx.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

// class ModSortDialog
ModSortDialog::ModSortDialog()
    : wxDialog(nullptr,
               wxID_ANY,
               "Set Mods",
               wxDefaultPosition,
               wxSize(DEFAULT_WIDTH,
                      DEFAULT_HEIGHT),
               wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP | wxRESIZE_BORDER | wxMINIMIZE_BOX)
{
    auto* pgc = PGPatcherGlobals::getPGC();
    if (pgc == nullptr) {
        throw runtime_error("PGConfig is null");
    }

    // Main sizer for the window
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Create the m_listCtrl
    m_listCtrl = new PGCheckedDragListCtrl(
        this, wxID_ANY, wxDefaultPosition, wxSize(DEFAULT_WIDTH, DEFAULT_HEIGHT), wxLC_REPORT);
    m_listCtrl->InsertColumn(0, "Mod");
    m_listCtrl->InsertColumn(1, "Shader");

    // Listctrl events
    m_listCtrl->Bind(wxEVT_LIST_ITEM_SELECTED, &ModSortDialog::onItemSelected, this);
    m_listCtrl->Bind(wxEVT_LIST_ITEM_DESELECTED, &ModSortDialog::onItemDeselected, this);

    m_listCtrl->Bind(pgEVT_CDLC_ITEM_DRAGGED, &ModSortDialog::onItemDragged, this);
    m_listCtrl->Bind(pgEVT_CDLC_ITEM_CHECKED, &ModSortDialog::onItemChecked, this);
    m_listCtrl->Bind(pgEVT_CDLC_MESHES_IGNORED_CHANGED, &ModSortDialog::onMeshesIgnoredChanged, this);

    m_listCtrl->Bind(wxEVT_SIZE, &ModSortDialog::onListCtrlResize, this);

    // define base item BG color, which should always be the background color of the listbox (theme agnostic)
    s_BASE_ITEM_BG_COLOR = m_listCtrl->GetBackgroundColour();
    s_BASE_ITEM_FG_COLOR = m_listCtrl->GetForegroundColour();

    // Global events
    Bind(wxEVT_CLOSE_WINDOW, &ModSortDialog::onClose, this);

    wxSizer* helpSizer = new wxBoxSizer(wxHORIZONTAL);

    // Add message at the top
    const wxString message
        = "Please sort your mods to determine what mod PGPatcher uses to patch meshes where. Selecting mods will show "
          "conflicts. The mod you have selected wins over mods that are green, and loses over mods that are red. New "
          "mods PGPatcher hasn't seen before are highlighted in purple.";
    auto* messageText = new wxStaticText(this, wxID_ANY, message, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    messageText->Wrap(DEFAULT_WIDTH - (2 * DEFAULT_PADDING) - HELPBTN_SIZE
                      - DEFAULT_PADDING); // Wrap text based on dialog width with some padding
    helpSizer->Add(messageText, 0, wxALL, DEFAULT_BORDER);

    // Add help ? button to the bottom right of the whole window that opens the wiki URL on click
    auto* helpButton = new wxButton(this, wxID_ANY, "?");
    wxFont helpButtonFont = helpButton->GetFont();
    helpButtonFont.SetPointSize(HELPBTN_FONT_SIZE); // Set font size to 12
    helpButtonFont.SetWeight(wxFONTWEIGHT_BOLD);
    helpButton->SetFont(helpButtonFont);

    helpButton->SetToolTip("Open the PGPatcher Mod Window wiki");

    const wxSize helpBtnSize = wxSize(HELPBTN_SIZE, HELPBTN_SIZE);
    helpButton->SetMinSize(helpBtnSize);
    helpButton->SetMaxSize(helpBtnSize);

    helpButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) -> void {
        wxLaunchDefaultBrowser("https://github.com/hakasapl/PGPatcher/wiki/Mod-Window");
    });

    helpSizer->Add(helpButton, 0, wxLEFT | wxTOP | wxBOTTOM, DEFAULT_BORDER);

    mainSizer->Add(helpSizer, 0, wxEXPAND | wxALL, 0);

    // Add "Use MO2 Loose File Order" checkbox
    if (pgc->getParams().ModManager.type == PGModManager::ModManagerType::MODORGANIZER2) {
        // Only show checkbox for MO2 users
        m_checkBoxMO2 = new wxCheckBox(this, wxID_ANY, "Lock to MO2 Loose File Order", wxDefaultPosition);
        m_checkBoxMO2->SetToolTip("Locks order to MO2. Enable/disable is still enabled. Keep in mind that PG conflicts "
                                  "are not the same as loose file conflicts.");
        m_checkBoxMO2->Bind(wxEVT_CHECKBOX, &ModSortDialog::onUseMO2LooseFileOrderChange, this);

        // Add to main sizer
        mainSizer->Add(m_checkBoxMO2, 0, wxALL, DEFAULT_BORDER);
    }

    // Add search box for quick contains-match filtering/selection.
    auto* searchSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* searchLabel = new wxStaticText(this, wxID_ANY, "Search:");
    searchSizer->Add(searchLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, DEFAULT_BORDER);

    m_searchCtrl = new wxTextCtrl(this, wxID_ANY);
    m_searchCtrl->SetHint("Search mods by name...");
    m_searchCtrl->Bind(wxEVT_TEXT, &ModSortDialog::onSearchTextChanged, this);
    searchSizer->Add(m_searchCtrl, 1, wxEXPAND, 0);
    mainSizer->Add(searchSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, DEFAULT_BORDER);

    // FONT for rects
    wxFont rectFont = GetFont(); // start with current font
    static constexpr int RECT_LABEL_FONT_SIZE = 20;
    rectFont.SetPointSize(RECT_LABEL_FONT_SIZE); // increase by 4 points
    rectFont.SetWeight(wxFONTWEIGHT_BOLD);

    // TOP RECTANGLE
    auto* topPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    topPanel->SetForegroundColour(*wxBLACK);
    topPanel->SetBackgroundColour(s_WINNING_MOD_COLOR);
    auto* topLabel
        = new wxStaticText(topPanel, wxID_ANY, "Winning Mods on Top", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
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
    bottomPanel->SetForegroundColour(*wxBLACK);
    bottomPanel->SetBackgroundColour(s_LOSING_MOD_COLOR);
    auto* bottomLabel = new wxStaticText(
        bottomPanel, wxID_ANY, "Losing Mods on Bottom", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);

    bottomLabel->SetFont(rectFont);

    // Center the text in the panel
    auto* bottomSizer = new wxBoxSizer(wxHORIZONTAL);
    bottomSizer->Add(bottomLabel, 1, wxALIGN_CENTER | wxALL, 2);
    bottomPanel->SetSizer(bottomSizer);

    // Add bottom rectangle to main sizer
    mainSizer->Add(bottomPanel, 0, wxEXPAND | wxTOP, 0); // No top border so it touches the list

    // Create button sizer for horizontal layout
    auto* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

    static constexpr int BOTTOM_BUTTON_SPACING = 8;

    // Add "Restore to Default Order" button
    m_restoreButton = new wxButton(this, wxID_ANY, "Restore Default Order");
    buttonSizer->Add(m_restoreButton, 0, wxALL, BOTTOM_BUTTON_SPACING);
    m_restoreButton->Bind(wxEVT_BUTTON, &ModSortDialog::onRestoreDefault, this);
    m_restoreButton->SetToolTip(
        "For MO2 default order is your loose file order. For vortex default order is by shader, "
        "then by name alphabetically.");

    // Add stretchable space
    buttonSizer->AddStretchSpacer(1);

    // Add discard changes button
    m_discardButton = new wxButton(this, wxID_ANY, "Discard Changes");
    buttonSizer->Add(m_discardButton, 0, wxALL, BOTTOM_BUTTON_SPACING);
    m_discardButton->Bind(wxEVT_BUTTON, &ModSortDialog::onDiscardChanges, this);

    m_discardButton->Enable(false);

    // Add cancel button
    auto* cancelButton = new wxButton(this, wxID_CANCEL, "Cancel");
    buttonSizer->Add(cancelButton, 0, wxALL, BOTTOM_BUTTON_SPACING);
    cancelButton->Bind(wxEVT_BUTTON, &ModSortDialog::onBtnClose, this);

    // Add apply button
    m_applyButton = new wxButton(this, wxID_APPLY, "Apply");
    buttonSizer->Add(m_applyButton, 0, wxALL, BOTTOM_BUTTON_SPACING);
    m_applyButton->Bind(wxEVT_BUTTON, &ModSortDialog::onApply, this);

    // Disable apply button by default
    m_applyButton->Enable(false);

    // Add OK button
    auto* okButton = new wxButton(this, wxID_OK, "Okay");
    buttonSizer->Add(okButton, 0, wxALL, BOTTOM_BUTTON_SPACING);
    okButton->Bind(wxEVT_BUTTON, &ModSortDialog::onOkay, this);

    // Add to main sizer
    mainSizer->Add(buttonSizer, 0, wxEXPAND | wxALL, 0);

    // Fill contents
    auto* pgmm = PGGlobals::getPGMM();
    fillListCtrl(pgmm->getModsByPriority(), false);

    // Set checkbox state based on current config
    if (m_checkBoxMO2 != nullptr) {
        m_checkBoxMO2->SetValue(pgc->getParams().ModManager.mo2UseLooseFileOrder);
    }
    setMO2LooseFileOrderCheckboxState();
    rebuildCacheFromListCtrl();
    rebuildListCtrlFromCache();

    // Calculate minimum width for each column
    const int col1Width = calculateColumnWidth(1);
    m_listCtrl->SetColumnWidth(1, col1Width);
    const int scrollBarWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);
    const int totalWidth
        = calculateColumnWidth(0) + col1Width + (DEFAULT_PADDING * 2) + scrollBarWidth; // Extra padding

    // Adjust dialog width to match the total width of columns and padding
    SetSizeHints(MIN_WIDTH, MIN_HEIGHT, wxDefaultCoord, wxDefaultCoord); // Adjust minimum width and height
    SetSize(totalWidth, DEFAULT_HEIGHT); // Set dialog size

    SetSizer(mainSizer);
}

// EVENT HANDLERS

void ModSortDialog::onItemSelected(wxListEvent& event)
{
    highlightConflictingItems(); // Highlight conflicts for the selected mod
    event.Skip();
}

void ModSortDialog::onItemDeselected(wxListEvent& event)
{
    highlightConflictingItems();
    event.Skip();
}

void ModSortDialog::onItemDragged(PGCheckedDragListCtrlEvtItemDragged& event)
{
    syncCacheFromListCtrl();
    updateApplyButtonState();
    event.Skip();
}

void ModSortDialog::onItemChecked(PGCheckedDragListCtrlEvtItemChecked& event)
{
    // Check if lock mo2 order is on
    if (m_checkBoxMO2 != nullptr && m_checkBoxMO2->IsChecked()) {
        // Persist the just-updated visible check/ignore state before we rebuild from MO2 order.
        syncCacheFromListCtrl();

        // reset indices to MO2 state for enabled items
        setMO2LooseFileOrderCheckboxState();
        rebuildCacheFromListCtrl();
        rebuildListCtrlFromCache();
    } else {
        syncCacheFromListCtrl();
    }

    updateApplyButtonState();
    event.Skip();
}

void ModSortDialog::onMeshesIgnoredChanged(PGCheckedDragListCtrlEvtMeshesIgnoredChanged& event)
{
    syncCacheFromListCtrl();
    updateApplyButtonState();
    event.Skip();
}

void ModSortDialog::onListCtrlResize(wxSizeEvent& event)
{
    static constexpr int MIN_COL_WIDTH = 50;

    const int totalWidth = m_listCtrl->GetClientSize().GetWidth();

    // Get the widths of the fixed columns
    const int col1Width = m_listCtrl->GetColumnWidth(1);

    // Calculate remaining width for first column
    int col0Width = totalWidth - col1Width - 2; // optional small padding for borders

    col0Width = std::max(col0Width, MIN_COL_WIDTH); // minimum width to avoid clipping

    m_listCtrl->SetColumnWidth(0, col0Width);

    event.Skip(); // allow default processing
}

void ModSortDialog::onClose([[maybe_unused]] wxCloseEvent& event) { wxTheApp->Exit(); }

void ModSortDialog::onOkay([[maybe_unused]] wxCommandEvent& event)
{
    updateMods();
    EndModal(wxID_OK);
}

void ModSortDialog::onBtnClose([[maybe_unused]] wxCommandEvent& event) { wxTheApp->Exit(); }

void ModSortDialog::onApply([[maybe_unused]] wxCommandEvent& event) { updateMods(); }

void ModSortDialog::onRestoreDefault([[maybe_unused]] wxCommandEvent& event)
{
    // confirm with modal
    const int response
        = wxMessageBox("Are you sure you want to restore default mod order and enable any manually disabled mods?",
                       "Confirm Restore Default Order",
                       wxYES_NO | wxICON_QUESTION,
                       this);

    if (response == wxYES) {
        auto* pgmm = PGGlobals::getPGMM();
        fillListCtrl(pgmm->getModsByDefaultOrder(), true);
        rebuildCacheFromListCtrl();
        rebuildListCtrlFromCache();
    }
}

void ModSortDialog::onDiscardChanges([[maybe_unused]] wxCommandEvent& event)
{
    const int response = wxMessageBox(
        "Are you sure you want to discard all changes?", "Confirm Discard Changes", wxYES_NO | wxICON_QUESTION, this);

    if (response == wxYES) {
        // restore checkbox state
        auto* pgc = PGPatcherGlobals::getPGC();
        if (pgc == nullptr) {
            throw runtime_error("PGConfig is null");
        }

        const auto currentParams = pgc->getParams();
        if (m_checkBoxMO2 != nullptr) {
            m_checkBoxMO2->SetValue(currentParams.ModManager.mo2UseLooseFileOrder);
        }

        auto* pgmm = PGGlobals::getPGMM();

        if (m_checkBoxMO2 != nullptr && m_checkBoxMO2->IsChecked()) {
            // If MO2 loose file order is checked, reset to that
            fillListCtrl(pgmm->getModsByDefaultOrder(), false);
        } else {
            // Otherwise reset to current priority order
            fillListCtrl(pgmm->getModsByPriority(), false);
        }

        rebuildCacheFromListCtrl();
        rebuildListCtrlFromCache();
    }
}

void ModSortDialog::onUseMO2LooseFileOrderChange(wxCommandEvent& event)
{
    syncCacheFromListCtrl();

    const bool searchActive = !getActiveSearchTerm().IsEmpty();
    const bool mo2Locked = (m_checkBoxMO2 != nullptr && m_checkBoxMO2->IsChecked());

    setMO2LooseFileOrderCheckboxState();

    // Only rebuild cache directly from the list when the list is full/unfiltered.
    // Rebuilding from a filtered search view would drop hidden rows and corrupt states.
    if (!searchActive || mo2Locked) {
        rebuildCacheFromListCtrl();
    }

    rebuildListCtrlFromCache();
    updateApplyButtonState();

    event.Skip();
}

void ModSortDialog::onSearchTextChanged(wxCommandEvent& event)
{
    syncCacheFromListCtrl();
    rebuildListCtrlFromCache();
    updateApplyButtonState();

    event.Skip();
}

// HELPERS

void ModSortDialog::setMO2LooseFileOrderCheckboxState()
{
    if (m_checkBoxMO2 == nullptr) {
        return;
    }

    const bool isChecked = m_checkBoxMO2->IsChecked();
    if (isChecked) {
        auto* pgmm = PGGlobals::getPGMM();
        const auto modListByLooseOrder = pgmm->getModsByDefaultOrder();
        fillListCtrl(modListByLooseOrder, false, true);

        // disable restore order button
        m_restoreButton->Enable(false);
    } else {
        // enable restore order button
        m_restoreButton->Enable(true);
    }

    const bool searchActive = !getActiveSearchTerm().IsEmpty();
    m_listCtrl->setDraggingEnabled(!isChecked && !searchActive);
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

void ModSortDialog::highlightConflictingItems()
{
    // clear previous highlights
    clearAllHighlights();

    // Find all selected items
    std::vector<std::wstring> selectedMods;
    long selIdx = -1;
    long selectionIdx = -1;
    while ((selIdx = m_listCtrl->GetNextItem(selIdx, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != wxNOT_FOUND) {
        selectedMods.push_back(m_listCtrl->GetItemText(selIdx).ToStdWstring());

        if (selectionIdx == -1) {
            selectionIdx = selIdx;
        }
    }

    if (selectedMods.empty()) {
        clearAllHighlights();
        return;
    }

    auto* pgmm = PGGlobals::getPGMM();
    for (const auto& selectedMod : selectedMods) {
        // Highlight selected item and its conflicts
        auto mod = pgmm->getMod(selectedMod);
        if (mod == nullptr) {
            continue;
        }

        auto conflictSet = mod->conflicts;

        // convert conflictSet to unordered set of strings
        unordered_set<std::wstring> conflictSetStr;
        for (const auto& conflict : conflictSet) {
            conflictSetStr.insert(conflict->name);
        }

        // Apply highlights
        for (long i = 0; i < m_listCtrl->GetItemCount(); ++i) {
            const std::wstring itemText = m_listCtrl->GetItemText(i).ToStdWstring();
            if (!conflictSetStr.contains(itemText)) {
                continue; // Skip non-conflicting items
            }

            if (std::ranges::find(selectedMods, itemText) == selectedMods.end()) {
                m_listCtrl->SetItemTextColour(i, *wxBLACK);
                if (i < selectionIdx) {
                    m_listCtrl->SetItemBackgroundColour(i, s_LOSING_MOD_COLOR); // Red-ish for conflicts above
                } else {
                    m_listCtrl->SetItemBackgroundColour(i, s_WINNING_MOD_COLOR); // Yellow-ish for conflicts below
                }
            }
        }
    }
}

void ModSortDialog::clearAllHighlights()
{
    for (long i = 0; i < m_listCtrl->GetItemCount(); ++i) {
        const std::wstring itemText = m_listCtrl->GetItemText(i).ToStdWstring();
        if (m_newMods.contains(itemText)) {
            m_listCtrl->SetItemBackgroundColour(i, s_NEW_MOD_COLOR); // Restore original color
            m_listCtrl->SetItemTextColour(i, *wxBLACK); // Ensure text is black for readability
        } else {
            m_listCtrl->SetItemBackgroundColour(i, s_BASE_ITEM_BG_COLOR); // Fallback to base item color
            m_listCtrl->SetItemTextColour(i, s_BASE_ITEM_FG_COLOR);
        }
    }
}

void ModSortDialog::updateMods()
{
    syncCacheFromListCtrl();

    // Reconstruct full visual order: enabled rows first, disabled rows second, both stable by cached order.
    const auto orderedRows = getOrderedCachedRows();

    // Loop through each cached element and update the mod manager directory.
    auto* pgmm = PGGlobals::getPGMM();
    const int itemCount = static_cast<int>(orderedRows.size());
    for (int i = 0; i < itemCount; ++i) {
        const auto& row = *orderedRows.at(static_cast<size_t>(i));
        auto mod = pgmm->getMod(row.modName);
        if (mod == nullptr) {
            continue;
        }

        mod->isEnabled = row.isChecked;

        if (mod->isEnabled) {
            mod->priority = static_cast<int>(itemCount - i);
        }

        mod->areMeshesIgnored = row.areMeshesIgnored;
    }

    // save configs
    auto* pgc = PGPatcherGlobals::getPGC();
    if (pgc == nullptr) {
        throw runtime_error("PGConfig is null");
    }

    if (!PGConfig::saveModConfig()) {
        // critical dialog
        wxMessageBox("Failed to save mod configuration to modrules.json", "Error", wxOK | wxICON_ERROR, this);
    }

    auto currentParams = pgc->getParams();
    currentParams.ModManager.mo2UseLooseFileOrder = (m_checkBoxMO2 != nullptr && m_checkBoxMO2->IsChecked());
    pgc->setParams(currentParams);
    if (!pgc->saveUserConfig()) {
        // critical dialog
        wxMessageBox("Failed to save user configuration to user.json", "Error", wxOK | wxICON_ERROR, this);
    }

    updateApplyButtonState();
}

void ModSortDialog::fillListCtrl(const std::vector<std::shared_ptr<PGModManager::Mod>>& modList,
                                 bool autoEnable,
                                 bool preserveChecks)
{
    // get unordered set of currently checked mods if preserveChecks is true from existing list ctrl
    std::unordered_set<std::wstring> currentlyCheckedMods;
    std::unordered_set<std::wstring> currentlyIgnoredMeshMods;
    if (preserveChecks) {
        if (!m_cachedRows.empty()) {
            for (const auto& row : m_cachedRows) {
                if (row.isChecked) {
                    currentlyCheckedMods.insert(row.modName);
                }
                if (row.areMeshesIgnored) {
                    currentlyIgnoredMeshMods.insert(row.modName);
                }
            }
        } else {
            for (long i = 0; i < m_listCtrl->GetItemCount(); ++i) {
                if (m_listCtrl->isChecked(i)) {
                    currentlyCheckedMods.insert(m_listCtrl->GetItemText(i).ToStdWstring());
                }
                if (m_listCtrl->areMeshesIgnored(i)) {
                    currentlyIgnoredMeshMods.insert(m_listCtrl->GetItemText(i).ToStdWstring());
                }
            }
        }
    }

    // Check if all items in modList are new
    bool disableIsNew = true;
    for (const auto& mod : modList) {
        if (!mod->isNew) {
            disableIsNew = false;
            break;
        }
    }

    m_newMods.clear();
    m_listCtrl->DeleteAllItems();

    std::vector<std::shared_ptr<PGModManager::Mod>> disabledMods;

    long listIdx = 0;

    for (const auto& mod : modList) {
        const auto shaders = mod->shaders;
        if (shaders.empty() && !mod->hasMeshes) {
            // no shaders or meshes, so no need to include in list
            continue;
        }

        bool modEnabled = mod->isEnabled;
        if (mod->isEnabled) {
            // mod is enabled, check if it is currently checked
            if (preserveChecks && !currentlyCheckedMods.contains(mod->name)) {
                // mod was previously unchecked, so disable it
                modEnabled = false;
            }
        } else {
            // see if we need to autoenable (max variant is greater than 1 which is NONE shader)
            if (autoEnable) {
                const bool hasNonNone = std::ranges::any_of(
                    shaders, [](PGEnums::ShapeShader s) -> bool { return s != PGEnums::ShapeShader::NONE; });
                if (hasNonNone) {
                    modEnabled = true;
                }
            }

            // see if we need to preserve checks and if this mod was previously checked
            if (preserveChecks && currentlyCheckedMods.contains(mod->name)) {
                modEnabled = true;
            }
        }

        if (!modEnabled) {
            disabledMods.push_back(mod);
            continue;
        }

        // Anything past this point the mod is assumed to be enabled

        const long index = m_listCtrl->InsertItem(listIdx, mod->name);

        // Shader Column
        m_listCtrl->SetItem(index, 1, constructShaderString(shaders));

        // Set highlight if new
        if (!disableIsNew && mod->isNew) {
            m_listCtrl->SetItemBackgroundColour(index, s_NEW_MOD_COLOR); // Highlight color
            m_listCtrl->SetItemTextColour(index, *wxBLACK); // Ensure text is black for readability
            m_newMods.insert(mod->name); // Store the original color using the mod name
        }

        // Enable checkbox
        m_listCtrl->check(index, true);

        // Set ignore meshes checkbox
        if ((preserveChecks && currentlyIgnoredMeshMods.contains(mod->name))
            || (!preserveChecks && mod->areMeshesIgnored)) {
            // if preserving checks and mod is disabled but was previously ignoring meshes, keep it ignoring meshes
            m_listCtrl->ignoreMeshes(index, true);
        }

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
        if (!disableIsNew && mod->isNew) {
            m_listCtrl->SetItemBackgroundColour(index, s_NEW_MOD_COLOR); // Highlight color
            m_listCtrl->SetItemTextColour(index, *wxBLACK); // Ensure text is black for readability
            m_newMods.insert(mod->name); // Store the original color using the mod name
        }

        // Disable checkbox
        m_listCtrl->check(index, false);

        // Set ignore meshes checkbox
        if ((preserveChecks && currentlyIgnoredMeshMods.contains(mod->name))
            || (!preserveChecks && mod->areMeshesIgnored)) {
            // if preserving checks and mod is disabled but was previously ignoring meshes, keep it ignoring meshes
            m_listCtrl->ignoreMeshes(index, true);
        }

        // iterate listIdx
        listIdx++;
    }

    updateApplyButtonState();
}

void ModSortDialog::rebuildCacheFromListCtrl()
{
    m_cachedRows.clear();
    m_cachedRows.reserve(static_cast<size_t>(m_listCtrl->GetItemCount()));

    for (long i = 0; i < m_listCtrl->GetItemCount(); ++i) {
        const std::wstring modName = m_listCtrl->GetItemText(i, 0).ToStdWstring();
        m_cachedRows.push_back({.modName = modName,
                                .shaderString = m_listCtrl->GetItemText(i, 1),
                                .isChecked = m_listCtrl->isChecked(i),
                                .areMeshesIgnored = m_listCtrl->areMeshesIgnored(i),
                                .isNew = m_newMods.contains(modName)});
    }
}

void ModSortDialog::syncCacheFromListCtrl()
{
    if (m_cachedRows.empty()) {
        rebuildCacheFromListCtrl();
        return;
    }

    std::vector<std::wstring> visibleOrder;
    visibleOrder.reserve(m_listCtrl->GetItemCount());

    std::unordered_set<std::wstring> visibleSet;
    visibleSet.reserve(m_listCtrl->GetItemCount());

    std::unordered_map<std::wstring, CachedModRow> visibleState;
    visibleState.reserve(m_listCtrl->GetItemCount());

    for (long i = 0; i < m_listCtrl->GetItemCount(); ++i) {
        const std::wstring modName = m_listCtrl->GetItemText(i, 0).ToStdWstring();
        visibleOrder.push_back(modName);
        visibleSet.insert(modName);

        visibleState[modName] = {.modName = modName,
                                 .shaderString = m_listCtrl->GetItemText(i, 1),
                                 .isChecked = m_listCtrl->isChecked(i),
                                 .areMeshesIgnored = m_listCtrl->areMeshesIgnored(i)};
    }

    // First, merge visible row state.
    for (auto& row : m_cachedRows) {
        if (!visibleState.contains(row.modName)) {
            continue;
        }

        const auto& visibleRow = visibleState.at(row.modName);
        row.shaderString = visibleRow.shaderString;
        row.isChecked = visibleRow.isChecked;
        row.areMeshesIgnored = visibleRow.areMeshesIgnored;
    }

    if (visibleOrder.size() == m_cachedRows.size()) {
        // Unfiltered mode: list control is authoritative for full order.
        std::unordered_map<std::wstring, CachedModRow> rowMap;
        rowMap.reserve(m_cachedRows.size());
        for (const auto& row : m_cachedRows) {
            rowMap[row.modName] = row;
        }

        std::vector<CachedModRow> newOrder;
        newOrder.reserve(m_cachedRows.size());
        for (const auto& modName : visibleOrder) {
            if (rowMap.contains(modName)) {
                newOrder.push_back(rowMap.at(modName));
            }
        }

        m_cachedRows = std::move(newOrder);
        return;
    }

    // Filtered mode: only reorder visible subset while preserving hidden relative order.
    std::unordered_map<std::wstring, CachedModRow> rowMap;
    rowMap.reserve(m_cachedRows.size());
    for (const auto& row : m_cachedRows) {
        rowMap[row.modName] = row;
    }

    size_t insertPos = m_cachedRows.size();
    for (size_t i = 0; i < m_cachedRows.size(); ++i) {
        if (visibleSet.contains(m_cachedRows.at(i).modName)) {
            insertPos = i;
            break;
        }
    }

    std::erase_if(m_cachedRows,
                  [&visibleSet](const CachedModRow& row) -> bool { return visibleSet.contains(row.modName); });

    insertPos = std::min(insertPos, m_cachedRows.size());

    std::vector<CachedModRow> visibleRows;
    visibleRows.reserve(visibleOrder.size());
    for (const auto& modName : visibleOrder) {
        if (rowMap.contains(modName)) {
            visibleRows.push_back(rowMap.at(modName));
        }
    }

    m_cachedRows.insert(m_cachedRows.begin() + static_cast<std::vector<CachedModRow>::difference_type>(insertPos),
                        visibleRows.begin(),
                        visibleRows.end());
}

void ModSortDialog::rebuildListCtrlFromCache()
{
    const wxString searchTerm = getActiveSearchTerm();

    std::vector<const CachedModRow*> filteredRows;
    filteredRows.reserve(m_cachedRows.size());

    for (const auto& row : m_cachedRows) {
        if (searchTerm.IsEmpty()) {
            filteredRows.push_back(&row);
            continue;
        }

        const wxString modName = wxString(row.modName).Lower();
        if (modName.Contains(searchTerm)) {
            filteredRows.push_back(&row);
        }
    }

    std::vector<const CachedModRow*> activeRows;
    std::vector<const CachedModRow*> inactiveRows;
    activeRows.reserve(filteredRows.size());
    inactiveRows.reserve(filteredRows.size());

    for (const auto* row : filteredRows) {
        if (row->isChecked) {
            activeRows.push_back(row);
        } else {
            inactiveRows.push_back(row);
        }
    }

    bool disableIsNew = true;
    for (const auto* row : filteredRows) {
        if (!row->isNew) {
            disableIsNew = false;
            break;
        }
    }

    m_newMods.clear();
    m_listCtrl->DeleteAllItems();

    long index = 0;
    for (const auto* row : activeRows) {
        const long insertedIndex = m_listCtrl->InsertItem(index, row->modName);
        m_listCtrl->SetItem(insertedIndex, 1, row->shaderString);
        m_listCtrl->check(insertedIndex, true);
        m_listCtrl->ignoreMeshes(insertedIndex, row->areMeshesIgnored);

        if (!disableIsNew && row->isNew) {
            m_listCtrl->SetItemBackgroundColour(insertedIndex, s_NEW_MOD_COLOR);
            m_listCtrl->SetItemTextColour(insertedIndex, *wxBLACK);
            m_newMods.insert(row->modName);
        }

        ++index;
    }

    m_listCtrl->setCutoffLine(static_cast<int>(activeRows.size()));

    for (const auto* row : inactiveRows) {
        const long insertedIndex = m_listCtrl->InsertItem(index, row->modName);
        m_listCtrl->SetItem(insertedIndex, 1, row->shaderString);
        m_listCtrl->check(insertedIndex, false);
        m_listCtrl->ignoreMeshes(insertedIndex, row->areMeshesIgnored);

        if (!disableIsNew && row->isNew) {
            m_listCtrl->SetItemBackgroundColour(insertedIndex, s_NEW_MOD_COLOR);
            m_listCtrl->SetItemTextColour(insertedIndex, *wxBLACK);
            m_newMods.insert(row->modName);
        }

        ++index;
    }

    const bool mo2Locked = m_checkBoxMO2 != nullptr && m_checkBoxMO2->IsChecked();
    if (m_restoreButton != nullptr) {
        m_restoreButton->Enable(!mo2Locked);
    }

    m_listCtrl->setDraggingEnabled(!mo2Locked && searchTerm.IsEmpty());
}

auto ModSortDialog::getActiveSearchTerm() const -> wxString
{
    if (m_searchCtrl == nullptr) {
        return {};
    }

    wxString term = m_searchCtrl->GetValue();
    term.Trim(true);
    term.Trim(false);
    return term.Lower();
}

auto ModSortDialog::getOrderedCachedRows() const -> std::vector<const CachedModRow*>
{
    std::vector<const CachedModRow*> orderedRows;
    orderedRows.reserve(m_cachedRows.size());

    for (const auto& row : m_cachedRows) {
        if (row.isChecked) {
            orderedRows.push_back(&row);
        }
    }
    for (const auto& row : m_cachedRows) {
        if (!row.isChecked) {
            orderedRows.push_back(&row);
        }
    }

    return orderedRows;
}

void ModSortDialog::updateApplyButtonState()
{
    syncCacheFromListCtrl();

    bool btnState = false;

    // Reconstruct full visual order: enabled rows first, disabled rows second, both stable by cached order.
    const auto orderedRows = getOrderedCachedRows();

    // Compare cached UI state against PGModManager baseline.
    auto* pgmm = PGGlobals::getPGMM();
    const int itemCount = static_cast<int>(orderedRows.size());
    for (int i = 0; i < itemCount; ++i) {
        const auto& row = *orderedRows.at(static_cast<size_t>(i));
        auto mod = pgmm->getMod(row.modName);
        if (mod == nullptr) {
            btnState = true;
            break;
        }

        if (mod->isEnabled != row.isChecked) {
            btnState = true;
            break;
        }

        if (mod->isEnabled && mod->priority != static_cast<int>(itemCount - i)) {
            btnState = true;
            break;
        }

        if (mod->areMeshesIgnored != row.areMeshesIgnored) {
            btnState = true;
            break;
        }
    }

    // Check any pgc settings
    auto* pgc = PGPatcherGlobals::getPGC();
    if (pgc == nullptr) {
        throw runtime_error("PGConfig is null");
    }

    const auto currentParams = pgc->getParams();
    if (m_checkBoxMO2 != nullptr && currentParams.ModManager.mo2UseLooseFileOrder != m_checkBoxMO2->IsChecked()) {
        btnState = true;
    }

    m_applyButton->Enable(btnState);
    m_discardButton->Enable(btnState);
}

auto ModSortDialog::constructShaderString(const std::set<PGEnums::ShapeShader>& shaders) -> wxString
{
    wxString shaderStr;
    for (const auto& shader : shaders) {
        if (shader == PGEnums::ShapeShader::NONE) {
            continue;
        }

        if (!shaderStr.empty()) {
            shaderStr += ", ";
        }
        shaderStr += PGEnums::getStrFromShader(shader);
    }
    return shaderStr;
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
