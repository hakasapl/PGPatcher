#pragma once

#include "PGModManager.hpp"
#include "PGPatcher.hpp"
#include "patchers/base/PatcherUtil.hpp"

#include <wx/listctrl.h>
#include <wx/splitter.h>
#include <wx/textctrl.h>
#include <wx/wx.h>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/**
 * @brief Modeless dialog that visualizes per-mesh, per-shape shader conflicts between mods.
 *
 * Three-panel layout:
 *   Left  — searchable list of all meshes that have shape conflicts.
 *   Middle — shapes within the selected mesh that have multiple mod matches.
 *   Right  — all candidate shader matches for the selected shape; the winning
 *             match (highest-priority enabled mod) is highlighted in green.
 *
 * When @p filterMods is non-empty only meshes where at least one conflicting
 * match belongs to one of the specified mods are shown.
 *
 * When @p showAllMeshes is true, displays ALL meshes/shapes/matches (not just conflicts),
 * and the mod filter label is hidden.
 */
class DialogModConflictView : public wxDialog {
public:
    /**
     * @brief Construct the conflict view dialog.
     *
     * @param filterMods Optional set of mod names to restrict the view to.
     *                   Pass an empty set to show all conflicts.
     * @param showAllMeshes If true, show all meshes/shapes/matches instead of just conflicts.
     */
    explicit DialogModConflictView(const std::unordered_set<std::wstring>& filterMods = {},
                                   bool showAllMeshes = false);

    /**
     * @brief Refresh the currently displayed match list to reflect changes in mod order.
     *        Called when mod priorities change in the main window.
     */
    void refreshDisplay();

private:
    using MatchView = PGPatcher::MatchMeta;

    struct PluginUseInfo {
        PGMeshPermutationTracker::FormKey formKey;

        auto displayString() const -> wxString;
    };

    wxTextCtrl* m_meshSearchCtrl = nullptr;
    wxListCtrl* m_meshListCtrl = nullptr;
    wxComboBox* m_pluginUseCombo = nullptr; ///< Dropdown to filter matches by plugin use.
    wxListCtrl* m_shapeListCtrl = nullptr;
    wxListCtrl* m_matchListCtrl = nullptr;
    wxStaticText* m_filterLabel = nullptr; ///< Banner showing which mods are being filtered.
    wxCheckBox* m_showDisabledCheckbox = nullptr; ///< Toggle visibility of disabled/untracked matches.

    /// Thread-safe snapshot of mesh patch metadata taken at construction time.
    PGPatcher::MeshPatchInfo m_patchMeta;
    /// Ordered list of mesh paths currently visible in the left panel.
    std::vector<std::filesystem::path> m_filteredMeshes;
    /// Cached display labels for the visible mesh paths, kept in the same order as m_filteredMeshes.
    std::vector<wxString> m_filteredMeshLabels;
    /// Mod-name filter; empty means "show all".
    std::unordered_set<std::wstring> m_filterMods;
    /// If true, show ALL meshes/shapes/matches instead of just conflicts.
    bool m_showAllMeshes = false;
    /// Temp files extracted from BSAs; cleaned up on dialog close.
    std::vector<std::filesystem::path> m_tempFiles;
    /// Current plugin use selection (-1 = no filter, show all deduplicated).
    int m_selectedPluginUseIdx = -1;
    /// Plugin use info for the currently selected shape.
    std::vector<PluginUseInfo> m_currentPluginUses;

    constexpr static int DEFAULT_WIDTH = 1100;
    constexpr static int DEFAULT_HEIGHT = 650;
    constexpr static int DEFAULT_BORDER = 5;
    constexpr static int LEFT_PANE_WIDTH = 420;
    constexpr static int MID_PANE_WIDTH = 220;

    /// Background colour used to highlight the winning match row.
    static inline const wxColour s_WINNING_MATCH_COLOR {160, 215, 160};

    // ---- Helpers -----------------------------------------------------------

    /**
     * @brief Rebuild the left mesh list from m_conflictData, applying
     *        the search filter and the mod filter.
     */
    void rebuildMeshList();

    /**
     * @brief Populate the shape list for the mesh at @p meshIdx in
     *        m_filteredMeshes.
     */
    void populateShapeList(long meshIdx);

    /**
     * @brief Populate the match list for the given mesh path / shape index.
     */
    void populateMatchList(const std::filesystem::path& meshPath,
                           size_t idx3D);

    /// Rebuild the plugin-use dropdown for the currently selected mesh.
    void populatePluginUseList(const std::filesystem::path& meshPath);

    /**
     * @brief Return true if @p meshData contains at least one shape where
     *        a match belongs to one of the filtered mods (or if m_filterMods
     *        is empty).
     */
    [[nodiscard]] auto meshPassesModFilter(const PGPatcher::MeshMeta& meshMeta) const -> bool;

    /**
     * @brief Return true if the shape passes the intersection filter: every mod in
     *        m_filterMods must have at least one match in this shape.
     */
    [[nodiscard]] auto shapePassesIntersectionFilter(const PGPatcher::MeshShapeMeta& shape) const -> bool;

    /**
     * @brief Return true if the shape has at least two distinct visible match sources
     *        (taking the "show disabled" checkbox into account).
     */
    [[nodiscard]] auto shapeHasActualConflict(const std::vector<MatchView>& matches) const -> bool;

    /**
     * @brief Return true if the given match should be displayed given the current
     *        state of the "show disabled" checkbox.
     */
    [[nodiscard]] auto isMatchVisible(const MatchView& match) const -> bool;

    /**
     * @brief Build deduplicated matches for a shape the same way the right-hand list is built.
     *
     * @param shapeMeta Shape metadata source.
     * @param selectedFormKey Optional plugin-use filter. Nullopt means aggregate all plugin uses.
     */
    [[nodiscard]] auto buildDisplayMatches(const PGPatcher::MeshShapeMeta& shapeMeta,
                                           const std::optional<PGMeshPermutationTracker::FormKey>& selectedFormKey
                                           = std::nullopt) const -> std::vector<MatchView>;

    /**
     * @brief Return the index (into @p matches) of the winning match —
     *        the highest-priority enabled mod.  Returns -1 when nothing wins.
     */
    static auto computeWinningMatchIdx(const std::vector<MatchView>& matches) -> int;

    // ---- Event handlers ----------------------------------------------------

    void onMeshSelected(wxListEvent& event);
    void onMeshActivated(wxListEvent& event);
    void onMeshContextMenu(wxContextMenuEvent& event);
    void onShapeSelected(wxListEvent& event);
    void onShapeContextMenu(wxContextMenuEvent& event);
    void onSearchChanged(wxCommandEvent& event);
    void onShowDisabledChanged(wxCommandEvent& event);
    void onPluginUseSelected(wxCommandEvent& event);
    void onMatchActivated(wxListEvent& event);
    void onMatchContextMenu(wxContextMenuEvent& event);
    void onMeshListResize(wxSizeEvent& event);
    void onShapeListResize(wxSizeEvent& event);
    void onMatchListResize(wxSizeEvent& event);

    /// Clean up temporary files extracted from BSAs.
    void cleanupTempFiles();

    [[nodiscard]] auto getSelectedMeshIndex() const -> long;
    [[nodiscard]] auto getSelectedShapeIndex() const -> long;
    [[nodiscard]] auto getSelectedMatchRow() const -> long;
    void copyTextToClipboard(const wxString& text);
    void openMeshFile(const std::filesystem::path& relPath);
    void openMatchFile(const wxString& modNameStr,
                       const std::filesystem::path& relPath);
    void extractAndOpenVirtualFile(const std::filesystem::path& relPath);
    void openPathWithDefaultApp(const std::filesystem::path& path);
};
