#include "GUI/DialogModConflictView.hpp"

#include "GUI/PGMessageBox.hpp"
#include "PGGlobals.hpp"
#include "PGLocale.hpp"
#include "PGModManager.hpp"
#include "PGPatcher.hpp"
#include "PGPatcherGlobals.hpp"
#include "PGUI.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "util/StringUtil.hpp"

#include <wx/artprov.h>
#include <wx/bmpbndl.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/splitter.h>
#include <wx/wx.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <limits>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
// Sizes in DIPs (pixels at 100% scaling), scaled to the monitor's DPI with FromDIP() where they are used
constexpr int filterLabelTopSpacer = 10;
constexpr int outerSplitterMinPaneSize = 100;
constexpr int innerSplitterMinPaneSize = 80;
constexpr int disabledTextColorChannel = 160;
constexpr int matchListModColWidth = 200;
constexpr int matchListShaderColWidth = 130;
constexpr int matchListMinPathColWidth = 40;
}

// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

DialogModConflictView::DialogModConflictView(const std::unordered_set<std::wstring>& filterMods,
                                             bool showAllMeshes)
    : wxDialog(nullptr,
               wxID_ANY,
               pgTr("matchViewer.title"),
               wxDefaultPosition,
               wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMINIMIZE_BOX)
    , m_filterMods(filterMods)
    , m_showOnlyConflicts(!showAllMeshes)
{
    SetIcons(PGUI::appIcons());

    // Pixel sizes are defined for 100% scaling, so scale them to the DPI of the monitor showing the dialog.
    const wxSize defaultSize = FromDIP(wxSize(defaultWidth, defaultHeight));
    SetSize(defaultSize);
    const int defaultBorder = FromDIP(defaultBorderDIP);

    // Take a fresh snapshot of the mesh patch metadata based on current mod state.
    // This ensures we reflect any mod priority changes made without saving.
    m_patchMeta = PGPatcher::patchMeta();

    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // ---- Filter / mod indicator -------------------------------------------
    m_filterLabel = new wxStaticText(this, wxID_ANY, wxEmptyString);
    {
        wxString label;
        wxString names;
        for (const auto& mod : m_filterMods) {
            if (!names.IsEmpty())
                names += ", ";
            names += wxString(mod);
        }
        if (m_showOnlyConflicts) {
            if (names.IsEmpty())
                label = pgTr("matchViewer.filterLabel.allConflicts");
            else if (m_filterMods.size() == 1)
                label = wxString::Format(pgTr("matchViewer.filterLabel.conflictsForMod"), names);
            else
                label = wxString::Format(pgTr("matchViewer.filterLabel.conflictsBetweenMods"), names);
        } else {
            if (names.IsEmpty())
                label = pgTr("matchViewer.filterLabel.allMatches");
            else if (m_filterMods.size() == 1)
                label = wxString::Format(pgTr("matchViewer.filterLabel.matchesForMod"), names);
            else
                label = wxString::Format(pgTr("matchViewer.filterLabel.matchesForMods"), names);
        }
        m_filterLabel->SetLabel(label);
    }
    mainSizer->AddSpacer(FromDIP(filterLabelTopSpacer));
    mainSizer->Add(m_filterLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, defaultBorder);

    // ---- Search bar --------------------------------------------------------
    auto* searchSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* searchLabel = new wxStaticText(this, wxID_ANY, pgTr("matchViewer.search.label"));
    m_meshSearchCtrl = new wxTextCtrl(this, wxID_ANY);
    m_meshSearchCtrl->SetHint(pgTr("matchViewer.search.hint"));
    m_meshSearchCtrl->Bind(wxEVT_TEXT, &DialogModConflictView::onSearchChanged, this);
    searchSizer->Add(searchLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, defaultBorder);
    searchSizer->Add(m_meshSearchCtrl, 1, wxEXPAND);

    m_showDisabledCheckbox = new wxCheckBox(this, wxID_ANY, pgTr("matchViewer.showDisabledMods"));
    m_showDisabledCheckbox->SetValue(false); // default: hide disabled-mod matches
    m_showDisabledCheckbox->Bind(wxEVT_CHECKBOX, &DialogModConflictView::onShowDisabledChanged, this);
    searchSizer->Add(m_showDisabledCheckbox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2 * defaultBorder);

    m_showOnlyConflictsCheckbox = new wxCheckBox(this, wxID_ANY, pgTr("matchViewer.onlyShowConflicts"));
    m_showOnlyConflictsCheckbox->SetValue(m_showOnlyConflicts);
    m_showOnlyConflictsCheckbox->Bind(wxEVT_CHECKBOX, &DialogModConflictView::onShowOnlyConflictsChanged, this);
    searchSizer->Add(m_showOnlyConflictsCheckbox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2 * defaultBorder);

    m_showMismatchesCheckbox = new wxCheckBox(this, wxID_ANY, pgTr("matchViewer.showMismatches"));
    m_showMismatchesCheckbox->SetValue(m_showMismatches);
    m_showMismatchesCheckbox->SetToolTip(pgTr("matchViewer.showMismatchesTooltip"));
    m_showMismatchesCheckbox->Bind(wxEVT_CHECKBOX, &DialogModConflictView::onShowMismatchesChanged, this);
    searchSizer->Add(m_showMismatchesCheckbox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2 * defaultBorder);
    mainSizer->Add(searchSizer, 0, wxEXPAND | wxALL, defaultBorder);

    // ---- Three-panel split area --------------------------------------------
    // The outer splitter holds meshPanel (left) | innerSplitter (right).
    // The inner splitter holds shapePanel (left) | matchPanel (right).
    auto* outerSplitter
        = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
    auto* innerSplitter
        = new wxSplitterWindow(outerSplitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);

    // -- Mesh panel ----------------------------------------------------------
    auto* meshPanel = new wxPanel(outerSplitter);
    auto* meshSizer = new wxBoxSizer(wxVERTICAL);

    auto* meshLabel = new wxStaticText(meshPanel, wxID_ANY, pgTr("matchViewer.panels.meshes"));
    wxFont boldFont = meshLabel->GetFont();
    boldFont.SetWeight(wxFONTWEIGHT_BOLD);
    meshLabel->SetFont(boldFont);
    meshSizer->Add(meshLabel, 0, wxALL, FromDIP(2));

    m_meshListCtrl
        = new wxListCtrl(meshPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_meshListCtrl->InsertColumn(0, pgTr("matchViewer.columns.meshPath"));
    m_meshListCtrl->Bind(wxEVT_LIST_ITEM_SELECTED, &DialogModConflictView::onMeshSelected, this);
    m_meshListCtrl->Bind(wxEVT_LIST_ITEM_DESELECTED, &DialogModConflictView::onMeshDeselected, this);
    m_meshListCtrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, &DialogModConflictView::onMeshActivated, this);
    m_meshListCtrl->Bind(wxEVT_CONTEXT_MENU, &DialogModConflictView::onMeshContextMenu, this);
    m_meshListCtrl->Bind(wxEVT_SIZE, &DialogModConflictView::onMeshListResize, this);
    m_meshListCtrl->Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        if (!m_showMismatches) {
            event.Skip();
            return;
        }
        updateHoverTooltip(m_meshListCtrl, m_meshRowTooltips, event);
    });
    meshSizer->Add(m_meshListCtrl, 1, wxEXPAND);
    meshPanel->SetSizer(meshSizer);

    // -- Shape panel ---------------------------------------------------------
    auto* shapePanel = new wxPanel(innerSplitter);
    auto* shapeSizer = new wxBoxSizer(wxVERTICAL);

    auto* shapeLabel = new wxStaticText(shapePanel, wxID_ANY, pgTr("matchViewer.panels.shapes"));
    shapeLabel->SetFont(boldFont);
    shapeSizer->Add(shapeLabel, 0, wxALL, FromDIP(2));

    m_shapeListCtrl
        = new wxListCtrl(shapePanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_shapeListCtrl->InsertColumn(0, pgTr("matchViewer.columns.shape"));
    m_shapeListCtrl->Bind(wxEVT_LIST_ITEM_SELECTED, &DialogModConflictView::onShapeSelected, this);
    m_shapeListCtrl->Bind(wxEVT_LIST_ITEM_DESELECTED, &DialogModConflictView::onShapeDeselected, this);
    m_shapeListCtrl->Bind(wxEVT_CONTEXT_MENU, &DialogModConflictView::onShapeContextMenu, this);
    m_shapeListCtrl->Bind(wxEVT_SIZE, &DialogModConflictView::onShapeListResize, this);
    shapeSizer->Add(m_shapeListCtrl, 1, wxEXPAND);
    shapePanel->SetSizer(shapeSizer);

    // -- Match panel ---------------------------------------------------------
    auto* matchPanel = new wxPanel(innerSplitter);
    auto* matchSizer = new wxBoxSizer(wxVERTICAL);

    auto* matchLabel = new wxStaticText(matchPanel, wxID_ANY, pgTr("matchViewer.panels.matches"));
    matchLabel->SetFont(boldFont);
    matchSizer->Add(matchLabel, 0, wxALL, FromDIP(2));

    // Plugin use filter dropdown (above match list).
    auto* pluginUseSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* pluginUseLabel = new wxStaticText(matchPanel, wxID_ANY, pgTr("matchViewer.pluginUse.label"));
    m_pluginUseCombo = new wxComboBox(
        matchPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY | wxCB_DROPDOWN);
    m_pluginUseCombo->Append(pgTr("matchViewer.pluginUse.noneSelected"));
    m_pluginUseCombo->SetSelection(0);
    m_pluginUseCombo->Bind(wxEVT_COMBOBOX, &DialogModConflictView::onPluginUseSelected, this);
    pluginUseSizer->Add(pluginUseLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, defaultBorder);
    pluginUseSizer->Add(m_pluginUseCombo, 1, wxEXPAND);
    matchSizer->Add(pluginUseSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, defaultBorder);

    m_matchListCtrl
        = new wxListCtrl(matchPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_matchListCtrl->InsertColumn(0, pgTr("matchViewer.columns.mod"));
    m_matchListCtrl->InsertColumn(1, pgTr("matchViewer.columns.shader"));
    m_matchListCtrl->InsertColumn(2, pgTr("matchViewer.columns.matchedFile"));
    m_matchListCtrl->Bind(wxEVT_SIZE, &DialogModConflictView::onMatchListResize, this);
    m_matchListCtrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, &DialogModConflictView::onMatchActivated, this);
    m_matchListCtrl->Bind(wxEVT_CONTEXT_MENU, &DialogModConflictView::onMatchContextMenu, this);
    m_matchListCtrl->Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        if (!m_showMismatches) {
            event.Skip();
            return;
        }
        updateHoverTooltip(m_matchListCtrl, m_matchRowTooltips, event);
    });
    matchSizer->Add(m_matchListCtrl, 1, wxEXPAND);
    matchPanel->SetSizer(matchSizer);

    setupWarningIcons();

    // -- Wire up splitters ---------------------------------------------------
    innerSplitter->SplitVertically(shapePanel, matchPanel, FromDIP(midPaneWidth));
    outerSplitter->SplitVertically(meshPanel, innerSplitter, FromDIP(leftPaneWidth));
    outerSplitter->SetMinimumPaneSize(FromDIP(outerSplitterMinPaneSize));
    innerSplitter->SetMinimumPaneSize(FromDIP(innerSplitterMinPaneSize));

    mainSizer->Add(outerSplitter, 1, wxEXPAND | wxALL, defaultBorder);

    // ---- Close button ------------------------------------------------------
    auto* closeButton = new wxButton(this, wxID_CLOSE, pgTr("common.close"));
    closeButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent& /*event*/) {
        if (IsModal()) {
            cleanupTempFiles();
            EndModal(wxID_CLOSE);
            return;
        }
        Close();
    });
    mainSizer->Add(closeButton, 0, wxALIGN_LEFT | wxALL, defaultBorder);

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
    SetMinSize(defaultSize);

    rebuildMeshList();
}

// ============================================================================
// Helpers.
// ============================================================================

wxString DialogModConflictView::PluginUseInfo::displayString() const
{
    wxString label = wxString(formKey.modKey);
    label += wxString::Format(L":%06X:", formKey.formID);
    label += wxString::FromUTF8(formKey.subMODL);
    return label;
}

bool DialogModConflictView::isMatchVisible(const MatchView& match) const
{
    if (m_showDisabledCheckbox->IsChecked())
        return true; // show everything
    if (!match.mod)
        return false; // hide untracked/vanilla when checkbox is off
    const std::shared_lock lock(match.mod->mutex);
    return match.mod->isEnabled;
}

auto DialogModConflictView::buildDisplayMatches(
    const PGPatcher::MeshShapeMeta& shapeMeta,
    const std::optional<PGMeshPermutationTracker::FormKey>& selectedFormKey) const -> std::vector<MatchView>
{
    std::vector<MatchView> matches;

    if (selectedFormKey.has_value()) {
        const auto matchIt = shapeMeta.matches.find(selectedFormKey.value());
        if (matchIt != shapeMeta.matches.end()) {
            matches.reserve(matchIt->second.size());
            for (const auto& match : matchIt->second)
                matches.push_back(match);
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

            if (!duplicate)
                matches.push_back(match);
        }
    }

    return matches;
}

bool DialogModConflictView::shapeHasActualConflict(const std::vector<MatchView>& matches) const
{
    // Count distinct visible sources (mods + untracked treated as one source).
    std::unordered_set<std::shared_ptr<PGModManager::Mod>, PGModManager::Mod::ModHash> visibleMods;
    bool hasUntracked = false;
    for (const auto& match : matches) {
        if (!isMatchVisible(match))
            continue;
        if (!match.mod)
            hasUntracked = true;
        else
            visibleMods.insert(match.mod);
    }
    return (visibleMods.size() + (hasUntracked ? 1 : 0)) >= 2;
}

bool DialogModConflictView::meshPassesModFilter(const PGPatcher::MeshMeta& meshMeta) const
{
    return std::ranges::any_of(meshMeta.shapeMeta, [this](const auto& shapeEntry) {
        return shapePassesIntersectionFilter(shapeEntry.second);
    });
}

bool DialogModConflictView::meshPassesAnyModFilter(const PGPatcher::MeshMeta& meshMeta) const
{
    return std::ranges::any_of(meshMeta.shapeMeta,
                               [this](const auto& shapeEntry) { return shapePassesAnyModFilter(shapeEntry.second); });
}

bool DialogModConflictView::shapePassesAnyModFilter(const PGPatcher::MeshShapeMeta& shape) const
{
    if (m_filterMods.empty())
        return true;
    for (const auto& [formKey, shapeMatches] : shape.matches) {
        (void)formKey;
        for (const auto& match : shapeMatches)
            if (match.mod && m_filterMods.contains(match.mod->name))
                return true;
    }
    return false;
}

bool DialogModConflictView::shapePassesIntersectionFilter(const PGPatcher::MeshShapeMeta& shape) const
{
    std::vector<MatchView> matches;
    for (const auto& [formKey, shapeMatches] : shape.matches) {
        (void)formKey;
        for (const auto& match : shapeMatches) {
            matches.push_back({
                .mod = match.mod,
                .shader = match.shader,
                .shaderTransformTo = match.shaderTransformTo,
                .matchedPath = match.matchedPath,
            });
        }
    }

    if (!shapeHasActualConflict(matches))
        return false;
    if (m_filterMods.empty())
        return true;
    // Every mod in the filter set must have a visible match on this shape.
    for (const auto& modName : m_filterMods) {
        bool found = false;
        for (const auto& match : matches) {
            if (isMatchVisible(match) && match.mod && match.mod->name == modName) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

int DialogModConflictView::computeWinningMatchIdx(const std::vector<MatchView>& matches)
{
    int maxPriority = -1;
    int winnerIdx = -1;

    for (size_t i = 0; i < matches.size(); ++i) {
        const auto& match = matches.at(i);
        if (!match.mod)
            continue;

        bool isEnabled = false;
        int curPriority = -1;
        {
            const std::shared_lock lock(match.mod->mutex);
            isEnabled = match.mod->isEnabled;
            curPriority = match.mod->priority;
        }

        if (!isEnabled)
            continue;

        // Match the patching logic: if priority is not less than current max, update winner.
        // This means last match with max priority wins (same as getWinningMatch in PGPatcher).
        if (curPriority < maxPriority)
            continue; // skip if lower priority

        maxPriority = curPriority;
        winnerIdx = static_cast<int>(i);
    }

    return winnerIdx;
}

void DialogModConflictView::setupWarningIcons()
{
    const wxSize iconSize = FromDIP(wxSize(warningIconSize, warningIconSize));

    wxBitmapBundle warningBundle;
    const std::filesystem::path svgPath = PGPatcherGlobals::exePath() / "resources" / "warning.svg";
    if (std::filesystem::exists(svgPath))
        warningBundle = wxBitmapBundle::FromSVGFile(wxString(svgPath.wstring()), iconSize);
    if (!warningBundle.IsOk())
        warningBundle = wxArtProvider::GetBitmapBundle(wxART_WARNING, wxART_LIST, iconSize);
    if (!warningBundle.IsOk())
        return; // no icon available; warning indicators are simply not shown

    const wxBitmap warningBitmap = warningBundle.GetBitmap(iconSize);

    // Rows inserted without an explicit image index render image 0 on Windows, so index 0 must
    // be a fully transparent placeholder; the actual warning icon lives at warningIconImageIndex.
    wxImage blankImage(iconSize.GetWidth(), iconSize.GetHeight());
    blankImage.InitAlpha();
    std::fill_n(blankImage.GetAlpha(), static_cast<size_t>(iconSize.GetWidth()) * iconSize.GetHeight(), 0);
    const wxBitmap blankBitmap(blankImage);

    m_meshWarningImages.Create(iconSize.GetWidth(), iconSize.GetHeight(), true, 2);
    m_meshWarningImages.Add(blankBitmap);
    m_meshWarningImages.Add(warningBitmap);

    m_matchWarningImages.Create(iconSize.GetWidth(), iconSize.GetHeight(), true, 2);
    m_matchWarningImages.Add(blankBitmap);
    m_matchWarningImages.Add(warningBitmap);

    m_isWarningIconAvailable = true;
    applyWarningIconVisibility();
}

void DialogModConflictView::applyWarningIconVisibility()
{
    if (!m_isWarningIconAvailable)
        return;

    // The lists only borrow the image lists (LVS_SHAREIMAGELISTS), so detaching is safe and
    // removes the reserved icon space entirely.
    m_meshListCtrl->SetImageList(m_showMismatches ? &m_meshWarningImages : nullptr, wxIMAGE_LIST_SMALL);
    m_matchListCtrl->SetImageList(m_showMismatches ? &m_matchWarningImages : nullptr, wxIMAGE_LIST_SMALL);

    if (!m_showMismatches) {
        // Motion events no longer update tooltips while hidden, so clear any tooltip that was
        // set during a hover to avoid stale warning text.
        m_meshListCtrl->UnsetToolTip();
        m_matchListCtrl->UnsetToolTip();
    }

    m_meshListCtrl->Refresh();
    m_matchListCtrl->Refresh();
}

wxString DialogModConflictView::meshWarningTooltip(const std::filesystem::path& meshPath) const
{
    if (m_filterMods.empty() || !PGGlobals::isPGMMSet())
        return { };

    // Vanilla/untracked meshes are assumed correct, so only warn for meshes from other tracked mods.
    const auto meshMod = PGGlobals::pgmm()->modByFileSmart(meshPath);
    if (!meshMod || m_filterMods.contains(meshMod->name))
        return { };

    return wxString::Format(pgTr("matchViewer.warnings.meshFromOtherMod"), wxString(meshMod->name));
}

wxString DialogModConflictView::buildResultTexturesTooltip(const MatchView& match)
{
    std::unordered_set<std::shared_ptr<PGModManager::Mod>, PGModManager::Mod::ModHash> distinctMods;
    for (const auto& [slot, slotMod] : match.resultTextureMods) {
        (void)slot;
        distinctMods.insert(slotMod);
    }

    if (distinctMods.size() < 2)
        return { };

    wxString tooltip = pgTr("matchViewer.warnings.resultTexturesFromDifferentMods");
    for (const auto& [slot, slotMod] : match.resultTextureMods)
        tooltip += "\n" + slotDisplayName(slot) + " - " + wxString(slotMod->name);

    return tooltip;
}

wxString DialogModConflictView::slotDisplayName(PGEnums::TextureSlots slot)
{
    switch (slot) {
    case PGEnums::TextureSlots::Diffuse:
        return pgTr("matchViewer.slots.diffuse");
    case PGEnums::TextureSlots::Normal:
        return pgTr("matchViewer.slots.normal");
    case PGEnums::TextureSlots::Glow:
        return pgTr("matchViewer.slots.glow");
    case PGEnums::TextureSlots::Parallax:
        return pgTr("matchViewer.slots.parallax");
    case PGEnums::TextureSlots::Cubemap:
        return pgTr("matchViewer.slots.cubemap");
    case PGEnums::TextureSlots::EnvMask:
        return pgTr("matchViewer.slots.envMask");
    case PGEnums::TextureSlots::MultiLayer:
        return pgTr("matchViewer.slots.multilayer");
    case PGEnums::TextureSlots::Backlight:
        return pgTr("matchViewer.slots.backlight");
    case PGEnums::TextureSlots::Unused:
        return pgTr("matchViewer.slots.unused");
    case PGEnums::TextureSlots::Unknown:
    default:
        return pgTr("matchViewer.slots.unknown");
    }
}

void DialogModConflictView::updateHoverTooltip(wxListCtrl* list,
                                               const std::vector<wxString>& tooltips,
                                               wxMouseEvent& event)
{
    int hitFlags = 0;
    const long item = list->HitTest(event.GetPosition(), hitFlags);

    wxString tooltip;
    if (item != wxNOT_FOUND && (hitFlags & wxLIST_HITTEST_ONITEMICON) && static_cast<size_t>(item) < tooltips.size())
        tooltip = tooltips.at(static_cast<size_t>(item));

    if (list->GetToolTipText() != tooltip) {
        if (tooltip.IsEmpty())
            list->UnsetToolTip();
        else
            list->SetToolTip(tooltip);
    }

    event.Skip();
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
    m_meshRowTooltips.clear();
    m_matchRowTooltips.clear();

    const wxString searchTerm = m_meshSearchCtrl->GetValue().Lower();

    for (const auto& [meshPath, meshData] : m_patchMeta) {
        // When "show only conflicts" is on, apply mod/conflict filter. Otherwise show all meshes.
        if (m_showOnlyConflicts && !meshPassesModFilter(meshData))
            continue;
        // When filter mods are set but NOT in conflicts-only mode, still restrict to meshes containing those mods.
        if (!m_showOnlyConflicts && !m_filterMods.empty() && !meshPassesAnyModFilter(meshData))
            continue;

        const wxString meshStr = wxString(meshPath.wstring());
        if (!searchTerm.IsEmpty() && !meshStr.Lower().Contains(searchTerm))
            continue;

        m_filteredMeshes.push_back(meshPath);
        m_filteredMeshLabels.push_back(meshStr);
    }

    // Stable sort so the list order is deterministic.
    std::vector<size_t> sortedIndices;
    sortedIndices.reserve(m_filteredMeshes.size());
    for (size_t i = 0; i < m_filteredMeshes.size(); ++i)
        sortedIndices.push_back(i);

    std::ranges::sort(sortedIndices,
                      [&](size_t a, size_t b) { return m_filteredMeshes.at(a) < m_filteredMeshes.at(b); });

    std::vector<std::filesystem::path> sortedMeshes;
    std::vector<wxString> sortedLabels;
    sortedMeshes.reserve(m_filteredMeshes.size());
    sortedLabels.reserve(m_filteredMeshLabels.size());
    for (const size_t idx : sortedIndices) {
        sortedMeshes.push_back(std::move(m_filteredMeshes.at(idx)));
        sortedLabels.push_back(std::move(m_filteredMeshLabels.at(idx)));
    }

    m_filteredMeshes = std::move(sortedMeshes);
    m_filteredMeshLabels = std::move(sortedLabels);

    m_meshRowTooltips.reserve(m_filteredMeshLabels.size());
    for (size_t i = 0; i < m_filteredMeshLabels.size(); ++i) {
        const long row = m_meshListCtrl->InsertItem(m_meshListCtrl->GetItemCount(), m_filteredMeshLabels.at(i));

        // Flag meshes that are owned by a mod outside the filtered mods (potential UV mismatch).
        wxString warningTooltip = meshWarningTooltip(m_filteredMeshes.at(i));
        if (!warningTooltip.IsEmpty() && m_isWarningIconAvailable)
            m_meshListCtrl->SetItemImage(row, warningIconImageIndex);
        m_meshRowTooltips.push_back(std::move(warningTooltip));
    }

    Thaw();
}

void DialogModConflictView::populateShapeList(long meshIdx)
{
    m_shapeListCtrl->DeleteAllItems();
    m_matchListCtrl->DeleteAllItems();

    if (meshIdx < 0 || static_cast<size_t>(meshIdx) >= m_filteredMeshes.size())
        return;

    const auto& meshPath = m_filteredMeshes.at(static_cast<size_t>(meshIdx));
    const auto meshIt = m_patchMeta.find(meshPath);
    if (meshIt == m_patchMeta.end())
        return;

    const auto& meshData = meshIt->second;

    // Sort shapes by map key so the list is stable.
    std::vector<std::pair<int, const PGPatcher::MeshShapeMeta*>> sortedShapes;
    sortedShapes.reserve(meshData.shapeMeta.size());
    for (const auto& [shapeKey, shapeInfo] : meshData.shapeMeta) {
        // When "show only conflicts" is on:
        // - if filterMods set: shape must pass intersection filter.
        // - if filterMods empty: shape must have an actual conflict.
        // When "show only conflicts" is off: show all shapes (but still filter to filterMods union if set).
        if (m_showOnlyConflicts) {
            if (!m_filterMods.empty() && !shapePassesIntersectionFilter(shapeInfo))
                continue;
            if (m_filterMods.empty()) {
                const auto allMatches = buildDisplayMatches(shapeInfo);
                if (!shapeHasActualConflict(allMatches))
                    continue;
            }
        } else if ((!m_filterMods.empty()) && (!shapePassesAnyModFilter(shapeInfo))) {
            // Union filter: at least one match from any of the filter mods must be on this shape.
            continue;
        }

        sortedShapes.emplace_back(static_cast<int>(shapeKey), &shapeInfo);
    }
    std::ranges::sort(sortedShapes, [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [idx3D, shapeInfo] : sortedShapes) {
        (void)idx3D;
        const wxString baseShapeName = shapeInfo->shapeName.empty() ? pgTr("matchViewer.unnamedShape")
                                                                    : wxString::FromUTF8(shapeInfo->shapeName);
        const wxString shapeLabelText = wxString::Format("%s (%u)", baseShapeName, shapeInfo->blockID);

        const long row = m_shapeListCtrl->InsertItem(m_shapeListCtrl->GetItemCount(), shapeLabelText);
        m_shapeListCtrl->SetItemData(row, static_cast<long>(idx3D));
    }
}

void DialogModConflictView::populatePluginUseList(const std::filesystem::path& meshPath)
{
    m_currentPluginUses.clear();
    while (m_pluginUseCombo->GetCount() > 1)
        m_pluginUseCombo->Delete(1);

    m_pluginUseCombo->SetSelection(0);
    m_selectedPluginUseIdx = -1;

    const auto meshIt = m_patchMeta.find(meshPath);
    if (meshIt == m_patchMeta.end())
        return;

    const auto& meshMeta = meshIt->second;
    if (meshMeta.formKeys.empty())
        return;

    for (const auto& formKey : meshMeta.formKeys) {
        PluginUseInfo info;
        info.formKey = formKey;
        m_currentPluginUses.push_back(std::move(info));
        m_pluginUseCombo->Append(m_currentPluginUses.back().displayString());
    }
}

void DialogModConflictView::populateMatchList(const std::filesystem::path& meshPath,
                                              size_t idx3D)
{
    m_matchListCtrl->DeleteAllItems();
    m_matchRowTooltips.clear();

    const auto meshIt = m_patchMeta.find(meshPath);
    if (meshIt == m_patchMeta.end())
        return;

    const auto& meshMeta = meshIt->second;
    const auto shapeIt = meshMeta.shapeMeta.find(idx3D);
    if (shapeIt == meshMeta.shapeMeta.end())
        return;

    const auto& shapeMeta = shapeIt->second;

    std::optional<PGMeshPermutationTracker::FormKey> selectedFormKey;
    if (m_selectedPluginUseIdx >= 0 && static_cast<size_t>(m_selectedPluginUseIdx) < m_currentPluginUses.size())
        selectedFormKey = m_currentPluginUses.at(static_cast<size_t>(m_selectedPluginUseIdx)).formKey;

    // Determine which matches to display based on plugin use filter.
    // The actual ordering is delegated to PGPatcher::sortMatches() using the live mod order.
    std::vector<MatchView> matches = buildDisplayMatches(shapeMeta, selectedFormKey);

    std::vector<std::shared_ptr<PGModManager::Mod>> modPriorityList;
    if (m_modOrderProvider)
        modPriorityList = m_modOrderProvider();
    else if (PGGlobals::isPGMMSet())
        modPriorityList = PGGlobals::pgmm()->modsByPriority();
    PGPatcher::sortMatches(matches, modPriorityList);
    int topVisibleMatchIdx = -1;
    if (m_selectedPluginUseIdx >= 0) {
        for (size_t i = 0; i < matches.size(); ++i) {
            if (isMatchVisible(matches.at(i))) {
                topVisibleMatchIdx = static_cast<int>(i);
                break;
            }
        }
    }

    // Handle case where shape has no matches (common when not in conflicts-only mode).
    if (matches.empty()) {
        if (!m_showOnlyConflicts) {
            // In "show all" mode, show explanatory text.
            const long row
                = m_matchListCtrl->InsertItem(m_matchListCtrl->GetItemCount(), pgTr("matchViewer.noMatches"));
            m_matchListCtrl->SetItemTextColour(
                row, wxColour(disabledTextColorChannel, disabledTextColorChannel, disabledTextColorChannel));
            m_matchRowTooltips.emplace_back();
        }
        return;
    }

    // Populate the list in helper-defined order.
    for (size_t i = 0; i < matches.size(); ++i) {
        const auto& match = matches.at(i);
        if (!isMatchVisible(match))
            continue;

        const wxString modName = match.mod ? wxString(match.mod->name) : pgTr("matchViewer.untrackedMod");
        const wxString shaderStr = wxString::FromUTF8(PGEnums::strFromShader(match.shader));
        const wxString matchedFile = wxString(match.matchedPath.wstring());

        const long row = m_matchListCtrl->InsertItem(m_matchListCtrl->GetItemCount(), modName);
        m_matchListCtrl->SetItem(row, 1, shaderStr);
        m_matchListCtrl->SetItem(row, 2, matchedFile);

        // Flag matches whose result textures come from more than one mod.
        wxString warningTooltip = buildResultTexturesTooltip(match);
        if (!warningTooltip.IsEmpty() && m_isWarningIconAvailable)
            m_matchListCtrl->SetItemImage(row, warningIconImageIndex);
        m_matchRowTooltips.push_back(std::move(warningTooltip));

        // Highlight the top displayed row (winner after filtering/visibility rules).
        if (std::cmp_equal(i, topVisibleMatchIdx)) {
            // Only highlight if the mod is enabled.
            bool shouldHighlight = (match.mod != nullptr);
            if (shouldHighlight) {
                const std::shared_lock lock(match.mod->mutex);
                shouldHighlight = match.mod->isEnabled;
            }

            if (shouldHighlight) {
                m_matchListCtrl->SetItemBackgroundColour(row, s_winningMatchColor);
                m_matchListCtrl->SetItemTextColour(row, *wxBLACK);
                continue; // Only continue if we actually highlighted
            }
            // If disabled, fall through to apply gray color.
        }

        // Gray out disabled mods and untracked/vanilla sources.
        bool shouldGray = (match.mod == nullptr); // untracked always grayed
        if (!shouldGray && match.mod) {
            const std::shared_lock lock(match.mod->mutex);
            shouldGray = !match.mod->isEnabled;
        }
        if (shouldGray) {
            m_matchListCtrl->SetItemTextColour(
                row, wxColour(disabledTextColorChannel, disabledTextColorChannel, disabledTextColorChannel));
        }
    }
}

long DialogModConflictView::selectedMeshIndex() const
{
    return m_meshListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
}

long DialogModConflictView::selectedShapeIndex() const
{
    return m_shapeListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
}

long DialogModConflictView::selectedMatchRow() const
{
    return m_matchListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
}

void DialogModConflictView::copyTextToClipboard(const wxString& text)
{
    if (!wxTheClipboard->Open())
        return;

    wxTheClipboard->SetData(new wxTextDataObject(text));
    wxTheClipboard->Close();
}

void DialogModConflictView::openPathWithDefaultApp(const std::filesystem::path& path)
{
    wxLaunchDefaultApplication(wxString(path.wstring()));
}

void DialogModConflictView::extractAndOpenVirtualFile(const std::filesystem::path& relPath)
{
    if (!PGGlobals::isPGDSet()) {
        pgMessageBox(pgTr("matchViewer.errors.dataDirUnavailable"), pgTr("common.error"), wxOK | wxICON_ERROR, this);
        return;
    }

    const int result = pgMessageBox(wxString::Format(pgTr("matchViewer.extraction.message"), relPath.wstring().c_str()),
                                    pgTr("matchViewer.extraction.title"),
                                    wxYES_NO | wxICON_QUESTION,
                                    this);

    if (result != wxYES)
        return;

    try {
        const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / L"PGPatcher_Temp";
        std::filesystem::create_directories(tempDir);

        const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        const std::filesystem::path tempFile
            = tempDir / (std::to_wstring(timestamp) + L"_" + relPath.filename().wstring());

        std::vector<std::byte> fileBytes = PGGlobals::pgd()->file(relPath);
        if (fileBytes.empty()) {
            pgMessageBox(pgTr("matchViewer.extraction.readFailed"),
                         pgTr("matchViewer.extraction.errorTitle"),
                         wxOK | wxICON_ERROR,
                         this);
            return;
        }

        std::ofstream outFile(tempFile, std::ios::binary);
        if (!outFile) {
            pgMessageBox(wxString::Format(pgTr("matchViewer.extraction.createTempFailed"), tempFile.wstring().c_str()),
                         pgTr("matchViewer.extraction.errorTitle"),
                         wxOK | wxICON_ERROR,
                         this);
            return;
        }
        if (fileBytes.size() > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
            pgMessageBox(pgTr("matchViewer.extraction.fileTooLarge"),
                         pgTr("matchViewer.extraction.errorTitle"),
                         wxOK | wxICON_ERROR,
                         this);
            return;
        }
        const auto bytesToWrite = static_cast<std::streamsize>(fileBytes.size());
        const auto* fileData = static_cast<const char*>(static_cast<const void*>(fileBytes.data()));
        outFile.write(fileData, bytesToWrite);
        outFile.close();

        std::filesystem::permissions(tempFile,
                                     std::filesystem::perms::owner_read | std::filesystem::perms::group_read
                                         | std::filesystem::perms::others_read,
                                     std::filesystem::perm_options::replace);

        m_tempFiles.push_back(tempFile);
        openPathWithDefaultApp(tempFile);
    } catch (const std::exception& ex) {
        pgMessageBox(
            wxString::Format(pgTr("matchViewer.extraction.extractOpenFailed"), StringUtil::utf8toUTF16(ex.what())),
            pgTr("matchViewer.extraction.errorTitle"),
            wxOK | wxICON_ERROR,
            this);
    }
}

void DialogModConflictView::openMeshFile(const std::filesystem::path& relPath)
{
    if (!PGGlobals::isPGDSet()) {
        pgMessageBox(pgTr("matchViewer.errors.dataDirUnavailable"), pgTr("common.error"), wxOK | wxICON_ERROR, this);
        return;
    }

    const auto* pgmm = PGGlobals::pgmm();
    if (pgmm) {
        const auto& mods = pgmm->modsByPriority();
        for (const auto& mod : mods) {
            if (!mod->isEnabled)
                continue;

            const std::filesystem::path absPath = mod->folder / relPath;
            if (std::filesystem::exists(absPath)) {
                openPathWithDefaultApp(absPath);
                return;
            }
        }
    }

    extractAndOpenVirtualFile(relPath);
}

void DialogModConflictView::openMatchFile(const wxString& modNameStr,
                                          const std::filesystem::path& relPath)
{
    if (!PGGlobals::isPGDSet()) {
        pgMessageBox(pgTr("matchViewer.errors.dataDirUnavailable"), pgTr("common.error"), wxOK | wxICON_ERROR, this);
        return;
    }

    // Try to open from the mod's actual folder first.
    if (!modNameStr.IsEmpty()) {
        std::shared_ptr<PGModManager::Mod> mod = nullptr;
        try {
            mod = PGGlobals::pgmm()->mod(modNameStr.ToStdWstring());
        } catch (...) {
            // Ignore lookup failure and fall back to extraction.
        }

        if (mod && !mod->folder.empty()) {
            const std::filesystem::path absPath = mod->folder / relPath;
            if (std::filesystem::exists(absPath)) {
                openPathWithDefaultApp(absPath);
                return;
            }
        }
    }

    extractAndOpenVirtualFile(relPath);
}

void DialogModConflictView::onMeshContextMenu(wxContextMenuEvent& event)
{
    const auto meshIdx = selectedMeshIndex();
    if (meshIdx == wxNOT_FOUND || static_cast<size_t>(meshIdx) >= m_filteredMeshes.size()) {
        event.Skip();
        return;
    }

    const auto& meshPath = m_filteredMeshes.at(static_cast<size_t>(meshIdx));
    wxMenu menu;
    const auto* copyName = menu.Append(wxID_ANY, pgTr("matchViewer.contextMenu.copyName"));
    const auto* openItem = menu.Append(wxID_ANY, pgTr("matchViewer.contextMenu.open"));

    menu.Bind(
        wxEVT_MENU,
        [this, meshPath](wxCommandEvent&) { copyTextToClipboard(wxString(meshPath.wstring())); },
        copyName->GetId());

    menu.Bind(wxEVT_MENU, [this, meshPath](wxCommandEvent&) { openMeshFile(meshPath); }, openItem->GetId());

    m_meshListCtrl->PopupMenu(&menu);
}

void DialogModConflictView::onShapeContextMenu(wxContextMenuEvent& event)
{
    const auto shapeRow = selectedShapeIndex();
    if (shapeRow == wxNOT_FOUND) {
        event.Skip();
        return;
    }

    const wxString shapeName = m_shapeListCtrl->GetItemText(shapeRow, 0);
    wxMenu menu;
    const auto* copyName = menu.Append(wxID_ANY, pgTr("matchViewer.contextMenu.copyName"));

    menu.Bind(wxEVT_MENU, [this, shapeName](wxCommandEvent&) { copyTextToClipboard(shapeName); }, copyName->GetId());

    m_shapeListCtrl->PopupMenu(&menu);
}

void DialogModConflictView::onMatchContextMenu(wxContextMenuEvent& event)
{
    const auto row = selectedMatchRow();
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
    const auto* copyModName = menu.Append(wxID_ANY, pgTr("matchViewer.contextMenu.copyModName"));
    const auto* openModFolder = menu.Append(wxID_ANY, pgTr("matchViewer.contextMenu.openModFolder"));
    const auto* openMatchingFile = menu.Append(wxID_ANY, pgTr("matchViewer.contextMenu.openMatchingFile"));

    menu.Bind(
        wxEVT_MENU, [this, modNameStr](wxCommandEvent&) { copyTextToClipboard(modNameStr); }, copyModName->GetId());

    menu.Bind(
        wxEVT_MENU,
        [this, modNameStr](wxCommandEvent&) {
            if (modNameStr.IsEmpty() || modNameStr == pgTr("matchViewer.untrackedMod"))
                return;

            try {
                const auto* pgmm = PGGlobals::pgmm();
                if (!pgmm)
                    return;

                const auto mod = pgmm->mod(modNameStr.ToStdWstring());
                if (mod && !mod->folder.empty())
                    openPathWithDefaultApp(mod->folder);
            } catch (...) {
                // Ignore lookup failures.
            }
        },
        openModFolder->GetId());

    menu.Bind(
        wxEVT_MENU,
        [this, modNameStr, relPathStr](wxCommandEvent&) {
            openMatchFile(modNameStr, std::filesystem::path(relPathStr.ToStdWstring()));
        },
        openMatchingFile->GetId());

    m_matchListCtrl->PopupMenu(&menu);
}

void DialogModConflictView::setModOrderProvider(
    std::function<std::vector<std::shared_ptr<PGModManager::Mod>>()> provider)
{
    m_modOrderProvider = std::move(provider);
}

void DialogModConflictView::refreshDisplay()
{
    // Refresh metadata and rebuild while preserving current selection state.
    m_patchMeta = PGPatcher::patchMeta();

    const std::filesystem::path selectedMeshPath = [&] {
        const long meshIdx = selectedMeshIndex();
        if (meshIdx == wxNOT_FOUND || static_cast<size_t>(meshIdx) >= m_filteredMeshes.size())
            return std::filesystem::path { };
        return m_filteredMeshes.at(static_cast<size_t>(meshIdx));
    }();

    const int selectedIdx3D = [&] {
        const long shapeRow = selectedShapeIndex();
        if (shapeRow == wxNOT_FOUND)
            return -1;
        return static_cast<int>(m_shapeListCtrl->GetItemData(shapeRow));
    }();

    const int selectedPluginUseIdx = m_selectedPluginUseIdx;
    const wxString selectedMatchMod = [&] {
        const long row = selectedMatchRow();
        return row == wxNOT_FOUND ? wxString { } : m_matchListCtrl->GetItemText(row, 0);
    }();
    const wxString selectedMatchShader = [&] {
        const long row = selectedMatchRow();
        return row == wxNOT_FOUND ? wxString { } : m_matchListCtrl->GetItemText(row, 1);
    }();
    const wxString selectedMatchPath = [&] {
        const long row = selectedMatchRow();
        return row == wxNOT_FOUND ? wxString { } : m_matchListCtrl->GetItemText(row, 2);
    }();

    const long topMeshItem = m_meshListCtrl->GetTopItem();

    Freeze();
    rebuildMeshList();

    if (!selectedMeshPath.empty()) {
        const auto meshIt = std::ranges::find(m_filteredMeshes, selectedMeshPath);
        if (meshIt != m_filteredMeshes.end()) {
            const long restoredMeshIdx = static_cast<long>(meshIt - m_filteredMeshes.begin());
            m_meshListCtrl->SetItemState(restoredMeshIdx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

            populateShapeList(restoredMeshIdx);
            populatePluginUseList(selectedMeshPath);

            if (selectedPluginUseIdx >= 0 && std::cmp_less(selectedPluginUseIdx, m_currentPluginUses.size())) {
                m_selectedPluginUseIdx = selectedPluginUseIdx;
                m_pluginUseCombo->SetSelection(selectedPluginUseIdx + 1);
            } else {
                m_selectedPluginUseIdx = -1;
                m_pluginUseCombo->SetSelection(0);
            }

            if (selectedIdx3D >= 0) {
                for (long i = 0; i < m_shapeListCtrl->GetItemCount(); ++i) {
                    if (std::cmp_not_equal(m_shapeListCtrl->GetItemData(i), selectedIdx3D))
                        continue;

                    m_shapeListCtrl->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                    populateMatchList(selectedMeshPath, static_cast<size_t>(selectedIdx3D));

                    for (long row = 0; row < m_matchListCtrl->GetItemCount(); ++row) {
                        if (m_matchListCtrl->GetItemText(row, 0) != selectedMatchMod)
                            continue;
                        if (m_matchListCtrl->GetItemText(row, 1) != selectedMatchShader)
                            continue;
                        if (m_matchListCtrl->GetItemText(row, 2) != selectedMatchPath)
                            continue;

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
        const long clampedTop = std::min(topMeshItem, meshCount - 1);
        m_meshListCtrl->EnsureVisible(meshCount - 1);
        m_meshListCtrl->EnsureVisible(clampedTop);
    }

    Thaw();
}

// ============================================================================
// Event handlers.
// ============================================================================

void DialogModConflictView::onMeshDeselected(wxListEvent& event)
{
    m_shapeListCtrl->DeleteAllItems();
    m_matchListCtrl->DeleteAllItems();
    m_pluginUseCombo->SetSelection(0);
    m_selectedPluginUseIdx = -1;
    m_currentPluginUses.clear();
    while (m_pluginUseCombo->GetCount() > 1)
        m_pluginUseCombo->Delete(1);
    event.Skip();
}

void DialogModConflictView::onShapeDeselected(wxListEvent& event)
{
    m_matchListCtrl->DeleteAllItems();
    event.Skip();
}

void DialogModConflictView::onMeshSelected(wxListEvent& event)
{
    const long meshIdx = event.GetIndex();

    // Populate the shape list for this mesh.
    populateShapeList(meshIdx);

    // Populate plugin use dropdown based on the selected mesh.
    if (static_cast<size_t>(meshIdx) < m_filteredMeshes.size()) {
        const auto& meshPath = m_filteredMeshes.at(static_cast<size_t>(meshIdx));
        if (m_patchMeta.contains(meshPath)) {
            populatePluginUseList(meshPath);
        } else {
            m_currentPluginUses.clear();
            while (m_pluginUseCombo->GetCount() > 1)
                m_pluginUseCombo->Delete(1);
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

    const auto& meshPath = m_filteredMeshes.at(static_cast<size_t>(meshIdx));
    const auto idx3D = static_cast<size_t>(m_shapeListCtrl->GetItemData(shapeRow));

    populateMatchList(meshPath, idx3D);
    event.Skip();
}

void DialogModConflictView::onSearchChanged(wxCommandEvent& event)
{
    const std::filesystem::path selectedMeshPath = [&] {
        const long meshIdx = m_meshListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (meshIdx == wxNOT_FOUND || static_cast<size_t>(meshIdx) >= m_filteredMeshes.size())
            return std::filesystem::path { };
        return m_filteredMeshes.at(static_cast<size_t>(meshIdx));
    }();

    const int selectedIdx3D = [&] {
        const long shapeRow = m_shapeListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (shapeRow == wxNOT_FOUND)
            return -1;
        return static_cast<int>(m_shapeListCtrl->GetItemData(shapeRow));
    }();

    const int selectedPluginUseIdx = m_selectedPluginUseIdx;
    const long topMeshItem = m_meshListCtrl->GetTopItem();

    Freeze();
    rebuildMeshList();

    if (!selectedMeshPath.empty()) {
        const auto it = std::ranges::find(m_filteredMeshes, selectedMeshPath);
        if (it != m_filteredMeshes.end()) {
            const long newMeshIdx = static_cast<long>(it - m_filteredMeshes.begin());
            m_meshListCtrl->SetItemState(newMeshIdx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
            populateShapeList(newMeshIdx);

            if (selectedIdx3D >= 0) {
                for (long i = 0; i < m_shapeListCtrl->GetItemCount(); ++i) {
                    if (std::cmp_equal(m_shapeListCtrl->GetItemData(i), selectedIdx3D)) {
                        m_shapeListCtrl->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                        break;
                    }
                }
            }

            if (selectedIdx3D >= 0) {
                if (selectedPluginUseIdx >= 0 && std::cmp_less(selectedPluginUseIdx, m_currentPluginUses.size())) {
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
        const long clampedTop = std::min(topMeshItem, itemCount - 1);
        m_meshListCtrl->EnsureVisible(itemCount - 1);
        m_meshListCtrl->EnsureVisible(clampedTop);
    }

    Thaw();
    event.Skip();
}

void DialogModConflictView::onShowDisabledChanged(wxCommandEvent& event)
{
    // Remember what is currently selected so we can restore it after rebuilding.
    std::filesystem::path selectedMeshPath;
    int selectedIdx3D = -1;
    const int selectedPluginUseIdx = m_selectedPluginUseIdx; // Save dropdown selection

    const long meshIdx = m_meshListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (meshIdx != wxNOT_FOUND && static_cast<size_t>(meshIdx) < m_filteredMeshes.size()) {
        selectedMeshPath = m_filteredMeshes.at(static_cast<size_t>(meshIdx));

        const long shapeRow = m_shapeListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (shapeRow != wxNOT_FOUND)
            selectedIdx3D = static_cast<int>(m_shapeListCtrl->GetItemData(shapeRow));
    }

    const long topMeshItem = m_meshListCtrl->GetTopItem();

    // Freeze the whole dialog so the entire rebuild + restore paints in one shot.
    Freeze();

    // Rebuild (clears all three lists internally).
    rebuildMeshList();

    long newMeshIdx = wxNOT_FOUND;
    if (!selectedMeshPath.empty()) {
        const auto it = std::ranges::find(m_filteredMeshes, selectedMeshPath);
        if (it != m_filteredMeshes.end()) {
            newMeshIdx = static_cast<long>(it - m_filteredMeshes.begin());
            m_meshListCtrl->SetItemState(newMeshIdx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

            populateShapeList(newMeshIdx);

            if (selectedIdx3D >= 0) {
                for (long i = 0; i < m_shapeListCtrl->GetItemCount(); ++i) {
                    if (std::cmp_equal(m_shapeListCtrl->GetItemData(i), selectedIdx3D)) {
                        m_shapeListCtrl->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                        populateMatchList(selectedMeshPath, selectedIdx3D);
                        break;
                    }
                }
            }

            // Restore the dropdown selection.
            if (selectedPluginUseIdx >= 0 && std::cmp_less(selectedPluginUseIdx, m_currentPluginUses.size())) {
                m_selectedPluginUseIdx = selectedPluginUseIdx;
                m_pluginUseCombo->SetSelection(selectedPluginUseIdx + 1); // +1 for "no filter" entry
                populateMatchList(selectedMeshPath, selectedIdx3D);
            } else {
                m_selectedPluginUseIdx = -1;
                m_pluginUseCombo->SetSelection(0);
                if (selectedIdx3D >= 0)
                    populateMatchList(selectedMeshPath, selectedIdx3D);
            }
        }
    }

    // Restore the mesh list scroll position to where it was before the rebuild.
    const long itemCount = m_meshListCtrl->GetItemCount();
    if (itemCount > 0 && topMeshItem >= 0) {
        const long clampedTop = std::min(topMeshItem, itemCount - 1);
        // Standard wxListCtrl scroll trick: scroll to bottom then back to target
        // so the target row ends up at the top of the visible area.
        m_meshListCtrl->EnsureVisible(itemCount - 1);
        m_meshListCtrl->EnsureVisible(clampedTop);
    }

    Thaw();
    event.Skip();
}

void DialogModConflictView::onShowOnlyConflictsChanged(wxCommandEvent& event)
{
    m_showOnlyConflicts = m_showOnlyConflictsCheckbox->IsChecked();

    // Update filter label text to reflect new mode.
    {
        wxString names;
        for (const auto& mod : m_filterMods) {
            if (!names.IsEmpty())
                names += ", ";
            names += wxString(mod);
        }
        wxString label;
        if (m_showOnlyConflicts) {
            if (names.IsEmpty())
                label = pgTr("matchViewer.filterLabel.allConflicts");
            else if (m_filterMods.size() == 1)
                label = wxString::Format(pgTr("matchViewer.filterLabel.conflictsForMod"), names);
            else
                label = wxString::Format(pgTr("matchViewer.filterLabel.conflictsBetweenMods"), names);
        } else {
            if (names.IsEmpty())
                label = pgTr("matchViewer.filterLabel.allMatches");
            else if (m_filterMods.size() == 1)
                label = wxString::Format(pgTr("matchViewer.filterLabel.matchesForMod"), names);
            else
                label = wxString::Format(pgTr("matchViewer.filterLabel.matchesForMods"), names);
        }
        m_filterLabel->SetLabel(label);
    }

    // Remember selection and rebuild.
    std::filesystem::path selectedMeshPath;
    int selectedIdx3D = -1;
    const int selectedPluginUseIdx = m_selectedPluginUseIdx;

    const long meshIdx = m_meshListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (meshIdx != wxNOT_FOUND && static_cast<size_t>(meshIdx) < m_filteredMeshes.size()) {
        selectedMeshPath = m_filteredMeshes.at(static_cast<size_t>(meshIdx));
        const long shapeRow = m_shapeListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (shapeRow != wxNOT_FOUND)
            selectedIdx3D = static_cast<int>(m_shapeListCtrl->GetItemData(shapeRow));
    }

    const long topMeshItem = m_meshListCtrl->GetTopItem();
    Freeze();

    rebuildMeshList();

    if (!selectedMeshPath.empty()) {
        const auto it = std::ranges::find(m_filteredMeshes, selectedMeshPath);
        if (it != m_filteredMeshes.end()) {
            const long newMeshIdx = static_cast<long>(it - m_filteredMeshes.begin());
            m_meshListCtrl->SetItemState(newMeshIdx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
            populateShapeList(newMeshIdx);

            if (selectedIdx3D >= 0) {
                for (long i = 0; i < m_shapeListCtrl->GetItemCount(); ++i) {
                    if (std::cmp_equal(m_shapeListCtrl->GetItemData(i), selectedIdx3D)) {
                        m_shapeListCtrl->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                        break;
                    }
                }
            }

            if (selectedPluginUseIdx >= 0 && std::cmp_less(selectedPluginUseIdx, m_currentPluginUses.size())) {
                m_selectedPluginUseIdx = selectedPluginUseIdx;
                m_pluginUseCombo->SetSelection(selectedPluginUseIdx + 1);
                populateMatchList(selectedMeshPath, selectedIdx3D);
            } else {
                m_selectedPluginUseIdx = -1;
                m_pluginUseCombo->SetSelection(0);
                if (selectedIdx3D >= 0)
                    populateMatchList(selectedMeshPath, selectedIdx3D);
            }
        }
    }

    const long itemCount = m_meshListCtrl->GetItemCount();
    if (itemCount > 0 && topMeshItem >= 0) {
        const long clampedTop = std::min(topMeshItem, itemCount - 1);
        m_meshListCtrl->EnsureVisible(itemCount - 1);
        m_meshListCtrl->EnsureVisible(clampedTop);
    }

    Thaw();
    event.Skip();
}

void DialogModConflictView::onShowMismatchesChanged(wxCommandEvent& event)
{
    m_showMismatches = m_showMismatchesCheckbox->IsChecked();
    applyWarningIconVisibility();
    event.Skip();
}

void DialogModConflictView::onPluginUseSelected(wxCommandEvent& /*event*/)
{
    // Get the currently selected shape.
    const long meshIdx = m_meshListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (meshIdx == wxNOT_FOUND || static_cast<size_t>(meshIdx) >= m_filteredMeshes.size())
        return;

    const long shapeRow = m_shapeListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (shapeRow == wxNOT_FOUND)
        return;

    const auto& meshPath = m_filteredMeshes.at(static_cast<size_t>(meshIdx));
    const auto idx3D = static_cast<size_t>(m_shapeListCtrl->GetItemData(shapeRow));

    // Update the selected plugin use index.
    m_selectedPluginUseIdx = m_pluginUseCombo->GetSelection() - 1; // -1 because first entry is "no filter"

    // Repopulate the match list with the new filter.
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

    const std::filesystem::path relPath(relPathStr.ToStdWstring());

    // Get the mod name from column 0 (first column displays the mod).
    const wxString modNameStr = m_matchListCtrl->GetItemText(row, 0);

    openMatchFile(modNameStr, relPath);

    event.Skip();
}

void DialogModConflictView::onMeshActivated(wxListEvent& event)
{
    const long row = event.GetIndex();
    if (row == wxNOT_FOUND || static_cast<size_t>(row) >= m_filteredMeshes.size()) {
        event.Skip();
        return;
    }

    const auto& meshPath = m_filteredMeshes.at(static_cast<size_t>(row));

    openMeshFile(meshPath);

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
    const int modColWidth = FromDIP(matchListModColWidth);
    const int shaderColWidth = FromDIP(matchListShaderColWidth);
    const int minPathColWidth = FromDIP(matchListMinPathColWidth);
    const int col2Width = totalWidth - modColWidth - shaderColWidth - 2;
    m_matchListCtrl->SetColumnWidth(0, modColWidth);
    m_matchListCtrl->SetColumnWidth(1, shaderColWidth);
    m_matchListCtrl->SetColumnWidth(2, col2Width > minPathColWidth ? col2Width : minPathColWidth);
    event.Skip();
}

void DialogModConflictView::cleanupTempFiles()
{
    for (const auto& tempPath : m_tempFiles) {
        try {
            if (std::filesystem::exists(tempPath))
                std::filesystem::remove(tempPath);
        } catch (const std::exception& ex) {
            wxLogError(wxString::Format(pgTr("matchViewer.errors.deleteTempFailed"),
                                        tempPath.wstring().c_str(),
                                        StringUtil::utf8toUTF16(ex.what())));
        }
    }
    m_tempFiles.clear();
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
