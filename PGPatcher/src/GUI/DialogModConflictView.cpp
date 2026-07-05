#include "GUI/DialogModConflictView.hpp"

#include "PGGlobals.hpp"
#include "PGModManager.hpp"
#include "PGPatcher.hpp"
#include "patchers/base/PatcherUtil.hpp"
#include "pgutil/PGEnums.hpp"
#include "util/StringUtil.hpp"

#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/listctrl.h>
#include <wx/splitter.h>
#include <wx/wx.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;
using namespace StringUtil;

// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

DialogModConflictView::DialogModConflictView(const unordered_set<wstring>& filterMods,
                                             bool showAllMeshes)
    : wxDialog(nullptr,
               wxID_ANY,
               "Conflict Viewer",
               wxDefaultPosition,
               wxSize(DEFAULT_WIDTH,
                      DEFAULT_HEIGHT),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMINIMIZE_BOX)
    , m_filterMods(filterMods)
    , m_showAllMeshes(showAllMeshes)
{
    // Take a fresh snapshot of the mesh patch metadata based on current mod state.
    // This ensures we reflect any mod priority changes made without saving.
    m_patchMeta = PGPatcher::getPatchMeta();

    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // ---- Filter / mod indicator -------------------------------------------
    m_filterLabel = new wxStaticText(this, wxID_ANY, wxEmptyString);
    if (!m_showAllMeshes) {
        if (m_filterMods.empty()) {
            m_filterLabel->SetLabel("Showing all conflicts");
        } else {
            wxString names;
            for (const auto& mod : m_filterMods) {
                if (!names.IsEmpty()) {
                    names += ", ";
                }
                names += wxString(mod);
            }
            m_filterLabel->SetLabel(wxString::Format("Viewing conflicts for mod(s): %s", names));
        }
        mainSizer->AddSpacer(10);
        mainSizer->Add(m_filterLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, DEFAULT_BORDER);
    } else {
        m_filterLabel->Show(false);
    }

    // ---- Search bar --------------------------------------------------------
    auto* searchSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* searchLabel = new wxStaticText(this, wxID_ANY, "Search:");
    m_meshSearchCtrl = new wxTextCtrl(this, wxID_ANY);
    m_meshSearchCtrl->SetHint("Search by mesh path...");
    m_meshSearchCtrl->Bind(wxEVT_TEXT, &DialogModConflictView::onSearchChanged, this);
    searchSizer->Add(searchLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, DEFAULT_BORDER);
    searchSizer->Add(m_meshSearchCtrl, 1, wxEXPAND);

    m_showDisabledCheckbox = new wxCheckBox(this, wxID_ANY, "Show conflicts from disabled mods");
    m_showDisabledCheckbox->SetValue(false); // default: hide disabled-mod matches
    m_showDisabledCheckbox->Bind(wxEVT_CHECKBOX, &DialogModConflictView::onShowDisabledChanged, this);
    searchSizer->Add(m_showDisabledCheckbox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, DEFAULT_BORDER * 2);
    mainSizer->Add(searchSizer, 0, wxEXPAND | wxALL, DEFAULT_BORDER);

    // ---- Three-panel split area --------------------------------------------
    // outerSplitter: meshPanel (left) | innerSplitter (right)
    // innerSplitter: shapePanel (left) | matchPanel (right)
    auto* outerSplitter
        = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
    auto* innerSplitter
        = new wxSplitterWindow(outerSplitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);

    // -- Mesh panel ----------------------------------------------------------
    auto* meshPanel = new wxPanel(outerSplitter);
    auto* meshSizer = new wxBoxSizer(wxVERTICAL);

    auto* meshLabel = new wxStaticText(meshPanel, wxID_ANY, "Meshes");
    wxFont boldFont = meshLabel->GetFont();
    boldFont.SetWeight(wxFONTWEIGHT_BOLD);
    meshLabel->SetFont(boldFont);
    meshSizer->Add(meshLabel, 0, wxALL, 2);

    m_meshListCtrl
        = new wxListCtrl(meshPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_meshListCtrl->InsertColumn(0, "Mesh Path");
    m_meshListCtrl->Bind(wxEVT_LIST_ITEM_SELECTED, &DialogModConflictView::onMeshSelected, this);
    m_meshListCtrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, &DialogModConflictView::onMeshActivated, this);
    m_meshListCtrl->Bind(wxEVT_CONTEXT_MENU, &DialogModConflictView::onMeshContextMenu, this);
    m_meshListCtrl->Bind(wxEVT_SIZE, &DialogModConflictView::onMeshListResize, this);
    meshSizer->Add(m_meshListCtrl, 1, wxEXPAND);
    meshPanel->SetSizer(meshSizer);

    // -- Shape panel ---------------------------------------------------------
    auto* shapePanel = new wxPanel(innerSplitter);
    auto* shapeSizer = new wxBoxSizer(wxVERTICAL);

    auto* shapeLabel = new wxStaticText(shapePanel, wxID_ANY, "Shapes");
    shapeLabel->SetFont(boldFont);
    shapeSizer->Add(shapeLabel, 0, wxALL, 2);

    m_shapeListCtrl
        = new wxListCtrl(shapePanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_shapeListCtrl->InsertColumn(0, "Shape");
    m_shapeListCtrl->Bind(wxEVT_LIST_ITEM_SELECTED, &DialogModConflictView::onShapeSelected, this);
    m_shapeListCtrl->Bind(wxEVT_CONTEXT_MENU, &DialogModConflictView::onShapeContextMenu, this);
    m_shapeListCtrl->Bind(wxEVT_SIZE, &DialogModConflictView::onShapeListResize, this);
    shapeSizer->Add(m_shapeListCtrl, 1, wxEXPAND);
    shapePanel->SetSizer(shapeSizer);

    // -- Match panel ---------------------------------------------------------
    auto* matchPanel = new wxPanel(innerSplitter);
    auto* matchSizer = new wxBoxSizer(wxVERTICAL);

    auto* matchLabel = new wxStaticText(matchPanel, wxID_ANY, "Matches");
    matchLabel->SetFont(boldFont);
    matchSizer->Add(matchLabel, 0, wxALL, 2);

    // Plugin use filter dropdown (above match list)
    auto* pluginUseSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* pluginUseLabel = new wxStaticText(matchPanel, wxID_ANY, "Filter by Plugin Use:");
    m_pluginUseCombo = new wxComboBox(
        matchPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY | wxCB_DROPDOWN);
    m_pluginUseCombo->Append("(No Plugin Use Selected)");
    m_pluginUseCombo->SetSelection(0);
    m_pluginUseCombo->Bind(wxEVT_COMBOBOX, &DialogModConflictView::onPluginUseSelected, this);
    pluginUseSizer->Add(pluginUseLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, DEFAULT_BORDER);
    pluginUseSizer->Add(m_pluginUseCombo, 1, wxEXPAND);
    matchSizer->Add(pluginUseSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, DEFAULT_BORDER);

    m_matchListCtrl
        = new wxListCtrl(matchPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_matchListCtrl->InsertColumn(0, "Mod");
    m_matchListCtrl->InsertColumn(1, "Shader");
    m_matchListCtrl->InsertColumn(2, "Matched File");
    m_matchListCtrl->Bind(wxEVT_SIZE, &DialogModConflictView::onMatchListResize, this);
    m_matchListCtrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, &DialogModConflictView::onMatchActivated, this);
    m_matchListCtrl->Bind(wxEVT_CONTEXT_MENU, &DialogModConflictView::onMatchContextMenu, this);
    matchSizer->Add(m_matchListCtrl, 1, wxEXPAND);
    matchPanel->SetSizer(matchSizer);

    // -- Wire up splitters ---------------------------------------------------
    innerSplitter->SplitVertically(shapePanel, matchPanel, MID_PANE_WIDTH);
    outerSplitter->SplitVertically(meshPanel, innerSplitter, LEFT_PANE_WIDTH);
    outerSplitter->SetMinimumPaneSize(100);
    innerSplitter->SetMinimumPaneSize(80);

    mainSizer->Add(outerSplitter, 1, wxEXPAND | wxALL, DEFAULT_BORDER);

    // ---- Close button ------------------------------------------------------
    auto* closeButton = new wxButton(this, wxID_CLOSE, "Close");
    closeButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent& /*event*/) {
        if (IsModal()) {
            cleanupTempFiles();
            EndModal(wxID_CLOSE);
            return;
        }
        Close();
    });
    mainSizer->Add(closeButton, 0, wxALIGN_LEFT | wxALL, DEFAULT_BORDER);

    // Bind window close event for cleanup when closed via other means (e.g., X button).
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        cleanupTempFiles();
        if (IsModal()) {
            event.Skip();
            return;
        }

        Destroy();
    });

    SetSizer(mainSizer);
    SetMinSize(wxSize(DEFAULT_WIDTH, DEFAULT_HEIGHT));

    rebuildMeshList();
}

// ============================================================================
// Helpers
// ============================================================================

auto DialogModConflictView::PluginUseInfo::displayString() const -> wxString
{
    wxString label = wxString(formKey.modKey);
    label += wxString::Format(L":%06X:", formKey.formID);
    label += wxString::FromUTF8(formKey.subMODL);
    return label;
}

auto DialogModConflictView::isMatchVisible(const MatchView& match) const -> bool
{
    if (m_showDisabledCheckbox->IsChecked()) {
        return true; // show everything
    }
    if (match.mod == nullptr) {
        return false; // hide untracked/vanilla when checkbox is off
    }
    const shared_lock lock(match.mod->mutex);
    return match.mod->isEnabled;
}

auto DialogModConflictView::buildDisplayMatches(
    const PGPatcher::MeshShapeMeta& shapeMeta,
    const optional<PGMeshPermutationTracker::FormKey>& selectedFormKey) const -> vector<MatchView>
{
    vector<MatchView> matches;

    if (selectedFormKey.has_value()) {
        const auto matchIt = shapeMeta.matches.find(selectedFormKey.value());
        if (matchIt != shapeMeta.matches.end()) {
            matches.reserve(matchIt->second.size());
            for (const auto& match : matchIt->second) {
                matches.push_back(match);
            }
        }
        return matches;
    }

    for (const auto& [formKey, shapeMatches] : shapeMeta.matches) {
        (void)formKey;
        for (const auto& match : shapeMatches) {
            bool duplicate = false;
            for (const auto& existing : matches) {
                if (existing.mod == match.mod && existing.shader == match.shader
                    && existing.matchedPath == match.matchedPath) {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate) {
                matches.push_back(match);
            }
        }
    }

    return matches;
}

auto DialogModConflictView::shapeHasActualConflict(const vector<MatchView>& matches) const -> bool
{
    // Count distinct visible sources (mods + untracked treated as one source)
    unordered_set<shared_ptr<PGModManager::Mod>, PGModManager::Mod::ModHash> visibleMods;
    bool hasUntracked = false;
    for (const auto& match : matches) {
        if (!isMatchVisible(match)) {
            continue;
        }
        if (match.mod == nullptr) {
            hasUntracked = true;
        } else {
            visibleMods.insert(match.mod);
        }
    }
    return (visibleMods.size() + (hasUntracked ? 1 : 0)) >= 2;
}

auto DialogModConflictView::meshPassesModFilter(const PGPatcher::MeshMeta& meshMeta) const -> bool
{
    for (const auto& [shapeKey, shapeInfo] : meshMeta.shapeMeta) {
        (void)shapeKey;
        if (shapePassesIntersectionFilter(shapeInfo)) {
            return true;
        }
    }
    return false;
}

auto DialogModConflictView::shapePassesIntersectionFilter(const PGPatcher::MeshShapeMeta& shape) const -> bool
{
    vector<MatchView> matches;
    for (const auto& [formKey, shapeMatches] : shape.matches) {
        (void)formKey;
        for (const auto& match : shapeMatches) {
            matches.push_back({match.mod, match.shader, match.matchedPath});
        }
    }

    if (!shapeHasActualConflict(matches)) {
        return false;
    }
    if (m_filterMods.empty()) {
        return true;
    }
    // Every mod in the filter set must have a visible match on this shape
    for (const auto& modName : m_filterMods) {
        bool found = false;
        for (const auto& match : matches) {
            if (isMatchVisible(match) && match.mod != nullptr && match.mod->name == modName) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

auto DialogModConflictView::computeWinningMatchIdx(const vector<MatchView>& matches) -> int
{
    int maxPriority = -1;
    int winnerIdx = -1;

    for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
        const auto& match = matches[static_cast<size_t>(i)];
        if (match.mod == nullptr) {
            continue;
        }

        bool isEnabled = false;
        int curPriority = -1;
        {
            const shared_lock lock(match.mod->mutex);
            isEnabled = match.mod->isEnabled;
            curPriority = match.mod->priority;
        }

        if (!isEnabled) {
            continue;
        }

        // Match the patching logic: if priority is not less than current max, update winner
        // This means last match with max priority wins (same as getWinningMatch in PGPatcher)
        if (curPriority < maxPriority) {
            continue; // skip if lower priority
        }

        maxPriority = curPriority;
        winnerIdx = i;
    }

    return winnerIdx;
}

void DialogModConflictView::rebuildMeshList()
{
    Freeze();
    m_meshListCtrl->DeleteAllItems();
    m_shapeListCtrl->DeleteAllItems();
    m_matchListCtrl->DeleteAllItems();
    m_filteredMeshes.clear();
    m_filteredMeshLabels.clear();
    m_filteredMeshLabels.reserve(m_patchMeta.size());

    const wxString searchTerm = m_meshSearchCtrl->GetValue().Lower();

    for (const auto& [meshPath, meshData] : m_patchMeta) {
        // In "show all" mode, don't filter by mod.  In normal mode, apply mod filter.
        if (!m_showAllMeshes && !meshPassesModFilter(meshData)) {
            continue;
        }

        const wxString meshStr = wxString(meshPath.wstring());
        if (!searchTerm.IsEmpty() && !meshStr.Lower().Contains(searchTerm)) {
            continue;
        }

        m_filteredMeshes.push_back(meshPath);
        m_filteredMeshLabels.push_back(meshStr);
    }

    // Stable sort so the list order is deterministic
    vector<size_t> sortedIndices;
    sortedIndices.reserve(m_filteredMeshes.size());
    for (size_t i = 0; i < m_filteredMeshes.size(); ++i) {
        sortedIndices.push_back(i);
    }

    sort(sortedIndices.begin(), sortedIndices.end(), [&](size_t a, size_t b) {
        return m_filteredMeshes[a] < m_filteredMeshes[b];
    });

    vector<filesystem::path> sortedMeshes;
    vector<wxString> sortedLabels;
    sortedMeshes.reserve(m_filteredMeshes.size());
    sortedLabels.reserve(m_filteredMeshLabels.size());
    for (const size_t idx : sortedIndices) {
        sortedMeshes.push_back(std::move(m_filteredMeshes[idx]));
        sortedLabels.push_back(std::move(m_filteredMeshLabels[idx]));
    }

    m_filteredMeshes = std::move(sortedMeshes);
    m_filteredMeshLabels = std::move(sortedLabels);

    for (size_t i = 0; i < m_filteredMeshes.size(); ++i) {
        m_meshListCtrl->InsertItem(m_meshListCtrl->GetItemCount(), m_filteredMeshLabels[i]);
    }

    Thaw();
}

void DialogModConflictView::populateShapeList(long meshIdx)
{
    m_shapeListCtrl->DeleteAllItems();
    m_matchListCtrl->DeleteAllItems();

    if (meshIdx < 0 || static_cast<size_t>(meshIdx) >= m_filteredMeshes.size()) {
        return;
    }

    const auto& meshPath = m_filteredMeshes[static_cast<size_t>(meshIdx)];
    const auto meshIt = m_patchMeta.find(meshPath);
    if (meshIt == m_patchMeta.end()) {
        return;
    }

    const auto& meshData = meshIt->second;

    // Sort shapes by map key so the list is stable.
    vector<pair<int, const PGPatcher::MeshShapeMeta*>> sortedShapes;
    sortedShapes.reserve(meshData.shapeMeta.size());
    for (const auto& [shapeKey, shapeInfo] : meshData.shapeMeta) {
        // In "show all" mode, skip mod filter. In normal mode, apply intersection filter.
        if (!m_showAllMeshes && !m_filterMods.empty() && !shapePassesIntersectionFilter(shapeInfo)) {
            continue;
        }
        sortedShapes.emplace_back(static_cast<int>(shapeKey), &shapeInfo);
    }
    sort(sortedShapes.begin(), sortedShapes.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [idx3D, shapeInfo] : sortedShapes) {
        (void)idx3D;
        const wxString baseShapeName
            = shapeInfo->shapeName.empty() ? wxString("[Unnamed Shape]") : wxString::FromUTF8(shapeInfo->shapeName);
        const wxString shapeLabelText = wxString::Format("%s (%u)", baseShapeName, shapeInfo->blockID);

        const long row = m_shapeListCtrl->InsertItem(m_shapeListCtrl->GetItemCount(), shapeLabelText);
        m_shapeListCtrl->SetItemData(row, static_cast<long>(idx3D));
    }
}

void DialogModConflictView::populatePluginUseList(const filesystem::path& meshPath)
{
    m_currentPluginUses.clear();
    while (m_pluginUseCombo->GetCount() > 1) {
        m_pluginUseCombo->Delete(1);
    }

    m_pluginUseCombo->SetSelection(0);
    m_selectedPluginUseIdx = -1;

    const auto meshIt = m_patchMeta.find(meshPath);
    if (meshIt == m_patchMeta.end()) {
        return;
    }

    const auto& meshMeta = meshIt->second;
    if (meshMeta.formKeys.empty()) {
        return;
    }

    for (const auto& formKey : meshMeta.formKeys) {
        PluginUseInfo info;
        info.formKey = formKey;
        m_currentPluginUses.push_back(std::move(info));
        m_pluginUseCombo->Append(m_currentPluginUses.back().displayString());
    }
}

void DialogModConflictView::populateMatchList(const filesystem::path& meshPath,
                                              size_t idx3D)
{
    m_matchListCtrl->DeleteAllItems();

    const auto meshIt = m_patchMeta.find(meshPath);
    if (meshIt == m_patchMeta.end()) {
        return;
    }

    const auto& meshMeta = meshIt->second;
    const auto shapeIt = meshMeta.shapeMeta.find(idx3D);
    if (shapeIt == meshMeta.shapeMeta.end()) {
        return;
    }

    const auto& shapeMeta = shapeIt->second;

    optional<PGMeshPermutationTracker::FormKey> selectedFormKey;
    if (m_selectedPluginUseIdx >= 0 && static_cast<size_t>(m_selectedPluginUseIdx) < m_currentPluginUses.size()) {
        selectedFormKey = m_currentPluginUses[static_cast<size_t>(m_selectedPluginUseIdx)].formKey;
    }

    // Determine which matches to display based on plugin use filter.
    // The actual ordering is delegated to PGPatcher::sortMatches() using the live mod order.
    vector<MatchView> matches = buildDisplayMatches(shapeMeta, selectedFormKey);

    const auto modPriorityList = PGGlobals::isPGMMSet() ? PGGlobals::getPGMM()->getModsByPriority()
                                                        : std::vector<std::shared_ptr<PGModManager::Mod>> {};
    PGPatcher::sortMatches(matches, modPriorityList);

    // Handle case where shape has no matches (common in showAllMeshes mode)
    if (matches.empty()) {
        if (m_showAllMeshes) {
            // In "show all" mode, show explanatory text
            const long row = m_matchListCtrl->InsertItem(m_matchListCtrl->GetItemCount(),
                                                         wxString("[No matches - this shape cannot be patched]"));
            m_matchListCtrl->SetItemTextColour(row, wxColour(160, 160, 160));
        }
        return;
    }

    // Populate the list in helper-defined order.
    for (size_t i = 0; i < matches.size(); ++i) {
        const auto& match = matches[i];
        if (!isMatchVisible(match)) {
            continue;
        }

        const wxString modName = match.mod != nullptr ? wxString(match.mod->name) : wxString("[Untracked Mod/Vanilla]");
        const wxString shaderStr = wxString::FromUTF8(PGEnums::getStrFromShader(match.shader));
        const wxString matchedFile = wxString(match.matchedPath.wstring());

        const long row = m_matchListCtrl->InsertItem(m_matchListCtrl->GetItemCount(), modName);
        m_matchListCtrl->SetItem(row, 1, shaderStr);
        m_matchListCtrl->SetItem(row, 2, matchedFile);

        // Highlight the first entry only when plugin use is selected and the winning mod is enabled.
        if (m_selectedPluginUseIdx >= 0 && i == 0 && match.mod != nullptr) {
            bool isEnabled = false;
            {
                const shared_lock lock(match.mod->mutex);
                isEnabled = match.mod->isEnabled;
            }

            if (isEnabled) {
                m_matchListCtrl->SetItemBackgroundColour(row, s_WINNING_MATCH_COLOR);
                m_matchListCtrl->SetItemTextColour(row, *wxBLACK);
                continue;
            }
        }

        // Gray out disabled mods and untracked/vanilla sources
        bool shouldGray = (match.mod == nullptr); // untracked always grayed
        if (!shouldGray && match.mod != nullptr) {
            const shared_lock lock(match.mod->mutex);
            shouldGray = !match.mod->isEnabled;
        }
        if (shouldGray) {
            m_matchListCtrl->SetItemTextColour(row, wxColour(160, 160, 160));
        }
    }
}

auto DialogModConflictView::getSelectedMeshIndex() const -> long
{
    return m_meshListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
}

auto DialogModConflictView::getSelectedShapeIndex() const -> long
{
    return m_shapeListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
}

auto DialogModConflictView::getSelectedMatchRow() const -> long
{
    return m_matchListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
}

void DialogModConflictView::copyTextToClipboard(const wxString& text)
{
    if (!wxTheClipboard->Open()) {
        return;
    }

    wxTheClipboard->SetData(new wxTextDataObject(text));
    wxTheClipboard->Close();
}

void DialogModConflictView::openPathWithDefaultApp(const filesystem::path& path)
{
    wxLaunchDefaultApplication(wxString(path.wstring()));
}

void DialogModConflictView::openMatchFile(const wxString& modNameStr,
                                          const filesystem::path& relPath)
{
    if (!PGGlobals::isPGDSet()) {
        wxMessageBox("Cannot open file: data directory is not available.", "Error", wxOK | wxICON_ERROR, this);
        return;
    }

    // Try to open from the mod's actual folder first.
    if (!modNameStr.IsEmpty() && modNameStr != "[Untracked Mod/Vanilla]") {
        std::shared_ptr<PGModManager::Mod> mod = nullptr;
        try {
            mod = PGGlobals::getPGMM()->getMod(modNameStr.ToStdWstring());
        } catch (...) {
            // ignore lookup failure and fall back to extraction
        }

        if (mod && !mod->folder.empty()) {
            const filesystem::path absPath = mod->folder / relPath;
            if (filesystem::exists(absPath)) {
                openPathWithDefaultApp(absPath);
                return;
            }
        }
    }

    const int result = wxMessageBox(wxString::Format("This file is inside a BSA archive:\n%s\n\nWould you "
                                                     "like to extract it to a read-only temporary location and open "
                                                     "it? It will be deleted when you close this dialog.",
                                                     relPath.wstring()),
                                    "File Extraction",
                                    wxYES_NO | wxICON_QUESTION,
                                    this);

    if (result != wxYES) {
        return;
    }

    try {
        filesystem::path tempDir = filesystem::temp_directory_path() / L"PGPatcher_Temp";
        filesystem::create_directories(tempDir);

        const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        const filesystem::path tempFile = tempDir / (std::to_wstring(timestamp) + L"_" + relPath.filename().wstring());

        std::vector<std::byte> fileBytes = PGGlobals::getPGD()->getFile(relPath);
        if (fileBytes.empty()) {
            wxMessageBox("Error: Failed to read file.", "Extraction Error", wxOK | wxICON_ERROR, this);
            return;
        }

        std::ofstream outFile(tempFile, std::ios::binary);
        if (!outFile) {
            wxMessageBox(wxString::Format("Error: Failed to create temporary file at %s", tempFile.wstring()),
                         "Extraction Error",
                         wxOK | wxICON_ERROR,
                         this);
            return;
        }

        outFile.write(reinterpret_cast<const char*>(fileBytes.data()), fileBytes.size());
        outFile.close();

        filesystem::permissions(tempFile,
                                filesystem::perms::owner_read | filesystem::perms::group_read
                                    | filesystem::perms::others_read,
                                filesystem::perm_options::replace);

        m_tempFiles.push_back(tempFile);
        openPathWithDefaultApp(tempFile);
    } catch (const exception& ex) {
        wxMessageBox(
            wxString::Format("Error attempting to extract and open file: %s", StringUtil::utf8toUTF16(ex.what())),
            "Extraction Error",
            wxOK | wxICON_ERROR,
            this);
    }
}

void DialogModConflictView::onMeshContextMenu(wxContextMenuEvent& event)
{
    const auto meshIdx = getSelectedMeshIndex();
    if (meshIdx == wxNOT_FOUND || static_cast<size_t>(meshIdx) >= m_filteredMeshes.size()) {
        event.Skip();
        return;
    }

    const auto& meshPath = m_filteredMeshes[static_cast<size_t>(meshIdx)];
    wxMenu menu;
    auto* copyName = menu.Append(wxID_ANY, "Copy Name");
    auto* openItem = menu.Append(wxID_ANY, "Open");

    menu.Bind(
        wxEVT_MENU,
        [this, meshPath](wxCommandEvent&) { copyTextToClipboard(wxString(meshPath.wstring())); },
        copyName->GetId());
    menu.Bind(wxEVT_MENU, [this, meshPath](wxCommandEvent&) { openPathWithDefaultApp(meshPath); }, openItem->GetId());

    m_meshListCtrl->PopupMenu(&menu);
}

void DialogModConflictView::onShapeContextMenu(wxContextMenuEvent& event)
{
    const auto shapeRow = getSelectedShapeIndex();
    if (shapeRow == wxNOT_FOUND) {
        event.Skip();
        return;
    }

    const wxString shapeName = m_shapeListCtrl->GetItemText(shapeRow, 0);
    wxMenu menu;
    auto* copyName = menu.Append(wxID_ANY, "Copy Name");

    menu.Bind(wxEVT_MENU, [this, shapeName](wxCommandEvent&) { copyTextToClipboard(shapeName); }, copyName->GetId());

    m_shapeListCtrl->PopupMenu(&menu);
}

void DialogModConflictView::onMatchContextMenu(wxContextMenuEvent& event)
{
    const auto row = getSelectedMatchRow();
    if (row == wxNOT_FOUND) {
        event.Skip();
        return;
    }

    const wxString modNameStr = m_matchListCtrl->GetItemText(row, 0);
    const wxString relPathStr = m_matchListCtrl->GetItemText(row, 2);
    if (relPathStr.IsEmpty()) {
        event.Skip();
        return;
    }

    wxMenu menu;
    auto* copyModName = menu.Append(wxID_ANY, "Copy Mod Name");
    auto* openModFolder = menu.Append(wxID_ANY, "Open Mod Folder");
    auto* openMatchingFile = menu.Append(wxID_ANY, "Open Matching File");

    menu.Bind(
        wxEVT_MENU, [this, modNameStr](wxCommandEvent&) { copyTextToClipboard(modNameStr); }, copyModName->GetId());

    menu.Bind(
        wxEVT_MENU,
        [this, modNameStr](wxCommandEvent&) {
            if (modNameStr.IsEmpty() || modNameStr == "[Untracked Mod/Vanilla]") {
                return;
            }

            try {
                auto* pgmm = PGGlobals::getPGMM();
                if (pgmm == nullptr) {
                    return;
                }

                const auto mod = pgmm->getMod(modNameStr.ToStdWstring());
                if (mod != nullptr && !mod->folder.empty()) {
                    openPathWithDefaultApp(mod->folder);
                }
            } catch (...) {
                // Ignore lookup failures.
            }
        },
        openModFolder->GetId());

    menu.Bind(
        wxEVT_MENU,
        [this, modNameStr, relPathStr](wxCommandEvent&) {
            openMatchFile(modNameStr, filesystem::path(relPathStr.ToStdWstring()));
        },
        openMatchingFile->GetId());

    m_matchListCtrl->PopupMenu(&menu);
}

void DialogModConflictView::refreshDisplay()
{
    // Refresh metadata and rebuild while preserving current selection state.
    m_patchMeta = PGPatcher::getPatchMeta();

    const filesystem::path selectedMeshPath = [&] {
        const long meshIdx = getSelectedMeshIndex();
        if (meshIdx == wxNOT_FOUND || static_cast<size_t>(meshIdx) >= m_filteredMeshes.size()) {
            return filesystem::path {};
        }
        return m_filteredMeshes[static_cast<size_t>(meshIdx)];
    }();

    const int selectedIdx3D = [&] {
        const long shapeRow = getSelectedShapeIndex();
        if (shapeRow == wxNOT_FOUND) {
            return -1;
        }
        return static_cast<int>(m_shapeListCtrl->GetItemData(shapeRow));
    }();

    const int selectedPluginUseIdx = m_selectedPluginUseIdx;
    const wxString selectedMatchMod = [&] {
        const long row = getSelectedMatchRow();
        return row == wxNOT_FOUND ? wxString {} : m_matchListCtrl->GetItemText(row, 0);
    }();
    const wxString selectedMatchShader = [&] {
        const long row = getSelectedMatchRow();
        return row == wxNOT_FOUND ? wxString {} : m_matchListCtrl->GetItemText(row, 1);
    }();
    const wxString selectedMatchPath = [&] {
        const long row = getSelectedMatchRow();
        return row == wxNOT_FOUND ? wxString {} : m_matchListCtrl->GetItemText(row, 2);
    }();

    const long topMeshItem = m_meshListCtrl->GetTopItem();

    Freeze();
    rebuildMeshList();

    if (!selectedMeshPath.empty()) {
        const auto meshIt = find(m_filteredMeshes.begin(), m_filteredMeshes.end(), selectedMeshPath);
        if (meshIt != m_filteredMeshes.end()) {
            const long restoredMeshIdx = static_cast<long>(meshIt - m_filteredMeshes.begin());
            m_meshListCtrl->SetItemState(restoredMeshIdx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

            populateShapeList(restoredMeshIdx);
            populatePluginUseList(selectedMeshPath);

            if (selectedPluginUseIdx >= 0 && selectedPluginUseIdx < static_cast<int>(m_currentPluginUses.size())) {
                m_selectedPluginUseIdx = selectedPluginUseIdx;
                m_pluginUseCombo->SetSelection(selectedPluginUseIdx + 1);
            } else {
                m_selectedPluginUseIdx = -1;
                m_pluginUseCombo->SetSelection(0);
            }

            if (selectedIdx3D >= 0) {
                for (long i = 0; i < m_shapeListCtrl->GetItemCount(); ++i) {
                    if (static_cast<int>(m_shapeListCtrl->GetItemData(i)) != selectedIdx3D) {
                        continue;
                    }

                    m_shapeListCtrl->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                    populateMatchList(selectedMeshPath, static_cast<size_t>(selectedIdx3D));

                    for (long row = 0; row < m_matchListCtrl->GetItemCount(); ++row) {
                        if (m_matchListCtrl->GetItemText(row, 0) != selectedMatchMod) {
                            continue;
                        }
                        if (m_matchListCtrl->GetItemText(row, 1) != selectedMatchShader) {
                            continue;
                        }
                        if (m_matchListCtrl->GetItemText(row, 2) != selectedMatchPath) {
                            continue;
                        }

                        m_matchListCtrl->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                        break;
                    }
                    break;
                }
            }
        }
    }

    const long meshCount = m_meshListCtrl->GetItemCount();
    if (meshCount > 0 && topMeshItem >= 0) {
        const long clampedTop = min(topMeshItem, meshCount - 1);
        m_meshListCtrl->EnsureVisible(meshCount - 1);
        m_meshListCtrl->EnsureVisible(clampedTop);
    }

    Thaw();
}

// ============================================================================
// Event handlers
// ============================================================================

void DialogModConflictView::onMeshSelected(wxListEvent& event)
{
    const long meshIdx = event.GetIndex();

    // Populate the shape list for this mesh
    populateShapeList(meshIdx);

    // Populate plugin use dropdown based on the selected mesh
    if (static_cast<size_t>(meshIdx) < m_filteredMeshes.size()) {
        const auto& meshPath = m_filteredMeshes[static_cast<size_t>(meshIdx)];
        if (m_patchMeta.contains(meshPath)) {
            populatePluginUseList(meshPath);
        } else {
            m_currentPluginUses.clear();
            while (m_pluginUseCombo->GetCount() > 1) {
                m_pluginUseCombo->Delete(1);
            }
            m_pluginUseCombo->SetSelection(0);
            m_selectedPluginUseIdx = -1;
        }
    }
    event.Skip();
}

void DialogModConflictView::onShapeSelected(wxListEvent& event)
{
    const long meshIdx = m_meshListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (meshIdx == wxNOT_FOUND || static_cast<size_t>(meshIdx) >= m_filteredMeshes.size()) {
        event.Skip();
        return;
    }

    const long shapeRow = event.GetIndex();
    if (shapeRow == wxNOT_FOUND) {
        event.Skip();
        return;
    }

    const auto& meshPath = m_filteredMeshes[static_cast<size_t>(meshIdx)];
    const auto idx3D = static_cast<size_t>(m_shapeListCtrl->GetItemData(shapeRow));

    populateMatchList(meshPath, idx3D);
    event.Skip();
}

void DialogModConflictView::onSearchChanged(wxCommandEvent& event)
{
    const filesystem::path selectedMeshPath = [&] {
        const long meshIdx = m_meshListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (meshIdx == wxNOT_FOUND || static_cast<size_t>(meshIdx) >= m_filteredMeshes.size()) {
            return filesystem::path {};
        }
        return m_filteredMeshes[static_cast<size_t>(meshIdx)];
    }();

    const int selectedIdx3D = [&] {
        const long shapeRow = m_shapeListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (shapeRow == wxNOT_FOUND) {
            return -1;
        }
        return static_cast<int>(m_shapeListCtrl->GetItemData(shapeRow));
    }();

    const int selectedPluginUseIdx = m_selectedPluginUseIdx;
    const long topMeshItem = m_meshListCtrl->GetTopItem();

    Freeze();
    rebuildMeshList();

    if (!selectedMeshPath.empty()) {
        const auto it = find(m_filteredMeshes.begin(), m_filteredMeshes.end(), selectedMeshPath);
        if (it != m_filteredMeshes.end()) {
            const long newMeshIdx = static_cast<long>(it - m_filteredMeshes.begin());
            m_meshListCtrl->SetItemState(newMeshIdx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
            populateShapeList(newMeshIdx);

            if (selectedIdx3D >= 0) {
                for (long i = 0; i < m_shapeListCtrl->GetItemCount(); ++i) {
                    if (static_cast<int>(m_shapeListCtrl->GetItemData(i)) == selectedIdx3D) {
                        m_shapeListCtrl->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                        break;
                    }
                }
            }

            if (selectedIdx3D >= 0) {
                if (selectedPluginUseIdx >= 0 && selectedPluginUseIdx < static_cast<int>(m_currentPluginUses.size())) {
                    m_selectedPluginUseIdx = selectedPluginUseIdx;
                    m_pluginUseCombo->SetSelection(selectedPluginUseIdx + 1);
                } else {
                    m_selectedPluginUseIdx = -1;
                    m_pluginUseCombo->SetSelection(0);
                }
                populateMatchList(selectedMeshPath, static_cast<size_t>(selectedIdx3D));
            }
        }
    }

    const long itemCount = m_meshListCtrl->GetItemCount();
    if (itemCount > 0 && topMeshItem >= 0) {
        const long clampedTop = min(topMeshItem, itemCount - 1);
        m_meshListCtrl->EnsureVisible(itemCount - 1);
        m_meshListCtrl->EnsureVisible(clampedTop);
    }

    Thaw();
    event.Skip();
}

void DialogModConflictView::onShowDisabledChanged(wxCommandEvent& event)
{
    // Remember what is currently selected so we can restore it after rebuilding.
    filesystem::path selectedMeshPath;
    int selectedIdx3D = -1;
    int selectedPluginUseIdx = m_selectedPluginUseIdx; // Save dropdown selection

    const long meshIdx = m_meshListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (meshIdx != wxNOT_FOUND && static_cast<size_t>(meshIdx) < m_filteredMeshes.size()) {
        selectedMeshPath = m_filteredMeshes[static_cast<size_t>(meshIdx)];

        const long shapeRow = m_shapeListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (shapeRow != wxNOT_FOUND) {
            selectedIdx3D = static_cast<int>(m_shapeListCtrl->GetItemData(shapeRow));
        }
    }

    const long topMeshItem = m_meshListCtrl->GetTopItem();

    // Freeze the whole dialog so the entire rebuild + restore paints in one shot.
    Freeze();

    // Rebuild (clears all three lists internally).
    rebuildMeshList();

    long newMeshIdx = wxNOT_FOUND;
    if (!selectedMeshPath.empty()) {
        const auto it = find(m_filteredMeshes.begin(), m_filteredMeshes.end(), selectedMeshPath);
        if (it != m_filteredMeshes.end()) {
            newMeshIdx = static_cast<long>(it - m_filteredMeshes.begin());
            m_meshListCtrl->SetItemState(newMeshIdx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

            populateShapeList(newMeshIdx);

            if (selectedIdx3D >= 0) {
                for (long i = 0; i < m_shapeListCtrl->GetItemCount(); ++i) {
                    if (static_cast<int>(m_shapeListCtrl->GetItemData(i)) == selectedIdx3D) {
                        m_shapeListCtrl->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                        populateMatchList(selectedMeshPath, selectedIdx3D);
                        break;
                    }
                }
            }

            // Restore the dropdown selection
            if (selectedPluginUseIdx >= 0 && selectedPluginUseIdx < static_cast<int>(m_currentPluginUses.size())) {
                m_selectedPluginUseIdx = selectedPluginUseIdx;
                m_pluginUseCombo->SetSelection(selectedPluginUseIdx + 1); // +1 for "no filter" entry
                populateMatchList(selectedMeshPath, selectedIdx3D);
            } else {
                m_selectedPluginUseIdx = -1;
                m_pluginUseCombo->SetSelection(0);
                if (selectedIdx3D >= 0) {
                    populateMatchList(selectedMeshPath, selectedIdx3D);
                }
            }
        }
    }

    // Restore the mesh list scroll position to where it was before the rebuild.
    const long itemCount = m_meshListCtrl->GetItemCount();
    if (itemCount > 0 && topMeshItem >= 0) {
        const long clampedTop = min(topMeshItem, itemCount - 1);
        // Standard wxListCtrl scroll trick: scroll to bottom then back to target
        // so the target row ends up at the top of the visible area.
        m_meshListCtrl->EnsureVisible(itemCount - 1);
        m_meshListCtrl->EnsureVisible(clampedTop);
    }

    Thaw();
    event.Skip();
}

void DialogModConflictView::onPluginUseSelected(wxCommandEvent& event)
{
    // Get the currently selected shape
    const long meshIdx = m_meshListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (meshIdx == wxNOT_FOUND || static_cast<size_t>(meshIdx) >= m_filteredMeshes.size()) {
        return;
    }

    const long shapeRow = m_shapeListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (shapeRow == wxNOT_FOUND) {
        return;
    }

    const auto& meshPath = m_filteredMeshes[static_cast<size_t>(meshIdx)];
    const auto idx3D = static_cast<size_t>(m_shapeListCtrl->GetItemData(shapeRow));

    // Update the selected plugin use index
    m_selectedPluginUseIdx = m_pluginUseCombo->GetSelection() - 1; // -1 because first entry is "no filter"

    // Repopulate the match list with the new filter
    populateMatchList(meshPath, idx3D);
}

void DialogModConflictView::onMatchActivated(wxListEvent& event)
{
    const long row = event.GetIndex();
    if (row == wxNOT_FOUND) {
        event.Skip();
        return;
    }

    // Retrieve the matched file path stored in column 2.
    const wxString relPathStr = m_matchListCtrl->GetItemText(row, 2);
    if (relPathStr.IsEmpty()) {
        event.Skip();
        return;
    }

    if (!PGGlobals::isPGDSet()) {
        wxMessageBox("Cannot open file: data directory is not available.", "Error", wxOK | wxICON_ERROR, this);
        event.Skip();
        return;
    }

    const filesystem::path relPath(relPathStr.ToStdWstring());

    // Get the mod name from column 0 (first column displays the mod)
    const wxString modNameStr = m_matchListCtrl->GetItemText(row, 0);

    // Try to open from the mod's actual folder
    if (!modNameStr.IsEmpty() && modNameStr != "[Untracked Mod/Vanilla]") {
        std::shared_ptr<PGModManager::Mod> mod = nullptr;
        try {
            mod = PGGlobals::getPGMM()->getMod(modNameStr.ToStdWstring());
        } catch (...) {
            // Mod lookup failed
        }

        if (mod && !mod->folder.empty()) {
            const filesystem::path absPath = mod->folder / relPath;
            if (filesystem::exists(absPath)) {
                wxLaunchDefaultApplication(wxString(absPath.wstring()));
                event.Skip();
                return;
            }
        }
    }

    // File is in a BSA or inaccessible through the virtual data path.
    // Extract to temp location.
    const int result = wxMessageBox(wxString::Format("This file is inside a BSA archive:\n%s\n\nWould you "
                                                     "like to extract it to a read-only temporary location and open "
                                                     "it? It will be deleted when you close this dialog.",
                                                     relPathStr),
                                    "File Extraction",
                                    wxYES_NO | wxICON_QUESTION,
                                    this);

    if (result != wxYES) {
        event.Skip();
        return;
    }

    // Extract to temp location.
    try {
        filesystem::path tempDir = filesystem::temp_directory_path() / L"PGPatcher_Temp";
        filesystem::create_directories(tempDir);

        // Generate a unique filename to avoid collisions
        const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        const filesystem::path tempFile = tempDir / (std::to_wstring(timestamp) + L"_" + relPath.filename().wstring());

        // Read file (works for both loose files and BSA files)
        std::vector<std::byte> fileBytes = PGGlobals::getPGD()->getFile(relPath);
        if (fileBytes.empty()) {
            wxMessageBox("Error: Failed to read file.", "Extraction Error", wxOK | wxICON_ERROR, this);
            event.Skip();
            return;
        }

        // Write to temp file
        std::ofstream outFile(tempFile, std::ios::binary);
        if (!outFile) {
            wxMessageBox(wxString::Format("Error: Failed to create temporary file at %s", tempFile.wstring()),
                         "Extraction Error",
                         wxOK | wxICON_ERROR,
                         this);
            event.Skip();
            return;
        }
        outFile.write(reinterpret_cast<const char*>(fileBytes.data()), fileBytes.size());
        outFile.close();

        // Make the temp file read-only to prevent accidental editing
        filesystem::permissions(tempFile,
                                filesystem::perms::owner_read | filesystem::perms::group_read
                                    | filesystem::perms::others_read,
                                filesystem::perm_options::replace);

        // Track temp file for cleanup
        m_tempFiles.push_back(tempFile);

        // Open the temp file
        wxLaunchDefaultApplication(wxString(tempFile.wstring()));
    } catch (const exception& ex) {
        wxMessageBox(
            wxString::Format("Error attempting to extract and open file: %s", StringUtil::utf8toUTF16(ex.what())),
            "Extraction Error",
            wxOK | wxICON_ERROR,
            this);
    }

    event.Skip();
}

void DialogModConflictView::onMeshActivated(wxListEvent& event)
{
    const long row = event.GetIndex();
    if (row == wxNOT_FOUND || static_cast<size_t>(row) >= m_filteredMeshes.size()) {
        event.Skip();
        return;
    }

    if (!PGGlobals::isPGDSet()) {
        wxMessageBox("Cannot open file: data directory is not available.", "Error", wxOK | wxICON_ERROR, this);
        event.Skip();
        return;
    }

    const auto& meshPath = m_filteredMeshes[static_cast<size_t>(row)];

    // Try to open from each enabled mod's actual folder
    auto* pgmm = PGGlobals::getPGMM();
    if (pgmm != nullptr) {
        const auto& mods = pgmm->getModsByPriority();
        for (const auto& mod : mods) {
            if (!mod->isEnabled) {
                continue;
            }

            const filesystem::path absPath = mod->folder / meshPath;
            if (filesystem::exists(absPath)) {
                wxLaunchDefaultApplication(wxString(absPath.wstring()));
                event.Skip();
                return;
            }
        }
    }

    // File is in a BSA or inaccessible through the virtual data path.
    // Extract to temp location.
    const int result = wxMessageBox(wxString::Format("This file is inside a BSA archive:\n%s\n\nWould you "
                                                     "like to extract it to a read-only temporary location and open "
                                                     "it? It will be deleted when you close this dialog.",
                                                     meshPath.wstring()),
                                    "File Extraction",
                                    wxYES_NO | wxICON_QUESTION,
                                    this);

    if (result != wxYES) {
        event.Skip();
        return;
    }

    // Extract to temp location.
    try {
        filesystem::path tempDir = filesystem::temp_directory_path() / L"PGPatcher_Temp";
        filesystem::create_directories(tempDir);

        // Generate a unique filename to avoid collisions
        const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        const filesystem::path tempFile = tempDir / (std::to_wstring(timestamp) + L"_" + meshPath.filename().wstring());

        // Read file (works for both loose files and BSA files)
        std::vector<std::byte> fileBytes = PGGlobals::getPGD()->getFile(meshPath);
        if (fileBytes.empty()) {
            wxMessageBox("Error: Failed to read file.", "Extraction Error", wxOK | wxICON_ERROR, this);
            event.Skip();
            return;
        }

        // Write to temp file
        std::ofstream outFile(tempFile, std::ios::binary);
        if (!outFile) {
            wxMessageBox(wxString::Format("Error: Failed to create temporary file at %s", tempFile.wstring()),
                         "Extraction Error",
                         wxOK | wxICON_ERROR,
                         this);
            event.Skip();
            return;
        }
        outFile.write(reinterpret_cast<const char*>(fileBytes.data()), fileBytes.size());
        outFile.close();

        // Make the temp file read-only to prevent accidental editing
        filesystem::permissions(tempFile,
                                filesystem::perms::owner_read | filesystem::perms::group_read
                                    | filesystem::perms::others_read,
                                filesystem::perm_options::replace);

        // Track temp file for cleanup
        m_tempFiles.push_back(tempFile);

        // Open the temp file
        wxLaunchDefaultApplication(wxString(tempFile.wstring()));
    } catch (const exception& ex) {
        wxMessageBox(
            wxString::Format("Error attempting to extract and open file: %s", StringUtil::utf8toUTF16(ex.what())),
            "Extraction Error",
            wxOK | wxICON_ERROR,
            this);
    }

    event.Skip();
}

void DialogModConflictView::onMeshListResize(wxSizeEvent& event)
{
    m_meshListCtrl->SetColumnWidth(0, m_meshListCtrl->GetClientSize().GetWidth() - 2);
    event.Skip();
}

void DialogModConflictView::onShapeListResize(wxSizeEvent& event)
{
    m_shapeListCtrl->SetColumnWidth(0, m_shapeListCtrl->GetClientSize().GetWidth() - 2);
    event.Skip();
}

void DialogModConflictView::onMatchListResize(wxSizeEvent& event)
{
    const int totalWidth = m_matchListCtrl->GetClientSize().GetWidth();
    constexpr int col0Width = 200; // Mod
    constexpr int col1Width = 130; // Shader
    const int col2Width = totalWidth - col0Width - col1Width - 2; // Matched File (fills remaining)
    m_matchListCtrl->SetColumnWidth(0, col0Width);
    m_matchListCtrl->SetColumnWidth(1, col1Width);
    m_matchListCtrl->SetColumnWidth(2, col2Width > 40 ? col2Width : 40);
    event.Skip();
}

void DialogModConflictView::cleanupTempFiles()
{
    for (const auto& tempPath : m_tempFiles) {
        try {
            if (filesystem::exists(tempPath)) {
                filesystem::remove(tempPath);
            }
        } catch (const exception& ex) {
            // Log error but don't throw; just skip cleanup for this file
            // In production, this might log to a debug output or system log
        }
    }
    m_tempFiles.clear();
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
