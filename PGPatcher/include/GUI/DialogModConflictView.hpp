#pragma once

#include "PGModManager.hpp"
#include "PGPatcher.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"

#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/splitter.h>
#include <wx/textctrl.h>
#include <wx/wx.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
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

    /**
     * @brief Set a callback that returns the live mod priority order in the conflict manager
     *        Called by ModSortDialog after construction.
     */
    void setModOrderProvider(std::function<std::vector<std::shared_ptr<PGModManager::Mod>>()> provider);

private:
    using MatchView = PGPatcher::MatchMeta;

    struct PluginUseInfo {
        PGMeshPermutationTracker::FormKey formKey;

        [[nodiscard]] auto displayString() const -> wxString;
    };

    wxTextCtrl* m_meshSearchCtrl = nullptr;
    wxListCtrl* m_meshListCtrl = nullptr;
    wxComboBox* m_pluginUseCombo = nullptr; ///< Dropdown to filter matches by plugin use.
    wxListCtrl* m_shapeListCtrl = nullptr;
    wxListCtrl* m_matchListCtrl = nullptr;
    wxStaticText* m_filterLabel = nullptr; ///< Banner showing which mods are being filtered.
    wxCheckBox* m_showDisabledCheckbox = nullptr; ///< Toggle visibility of disabled/untracked matches.
    wxCheckBox* m_showOnlyConflictsCheckbox = nullptr; ///< When checked, show only conflicting shapes.
    wxCheckBox* m_showMismatchesCheckbox = nullptr; ///< Toggle visibility of the mismatch warning icons.

    /// Thread-safe snapshot of mesh patch metadata taken at construction time.
    PGPatcher::MeshPatchInfo m_patchMeta;
    /// Ordered list of mesh paths currently visible in the left panel.
    std::vector<std::filesystem::path> m_filteredMeshes;
    /// Cached display labels for the visible mesh paths, kept in the same order as m_filteredMeshes.
    std::vector<wxString> m_filteredMeshLabels;
    /// Mod-name filter; empty means "show all".
    std::unordered_set<std::wstring> m_filterMods;
    /// If true, show only conflicting meshes/shapes (based on filterMods intersection or actual conflict).
    bool m_showOnlyConflicts = false;
    /// Callback that returns the current live mod priority list (enabled first, disabled after).
    std::function<std::vector<std::shared_ptr<PGModManager::Mod>>()> m_modOrderProvider;
    /// Temp files extracted from BSAs; cleaned up on dialog close.
    std::vector<std::filesystem::path> m_tempFiles;
    /// Current plugin use selection (-1 = no filter, show all deduplicated).
    int m_selectedPluginUseIdx = -1;
    /// Plugin use info for the currently selected shape.
    std::vector<PluginUseInfo> m_currentPluginUses;
    /// Per-row warning tooltips for the mesh list; empty string means no warning icon on that row.
    std::vector<wxString> m_meshRowTooltips;
    /// Per-row warning tooltips for the match list; empty string means no warning icon on that row.
    std::vector<wxString> m_matchRowTooltips;
    /// Warning icon image list for the mesh list (owned here; the list control only borrows it).
    wxImageList m_meshWarningImages;
    /// Warning icon image list for the match list (owned here; the list control only borrows it).
    wxImageList m_matchWarningImages;
    /// True once the warning icon image lists were successfully created.
    bool m_warningIconAvailable = false;
    /// Whether mismatch warning icons are currently shown ("Show Potential Mismatches" checkbox state, off by
    /// default).
    bool m_showMismatches = false;

    constexpr static int DEFAULT_WIDTH = 1100;
    constexpr static int DEFAULT_HEIGHT = 650;
    constexpr static int DEFAULT_BORDER = 5;
    constexpr static int LEFT_PANE_WIDTH = 420;
    constexpr static int MID_PANE_WIDTH = 220;
    constexpr static int WARNING_ICON_SIZE = 16;
    /// Image list index of the warning icon (index 0 is a transparent placeholder shown by default).
    constexpr static int WARNING_ICON_IMAGE_INDEX = 1;

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
     * @brief Return true if the mesh contains at least one shape where any match belongs to any of the filtered mods.
     *        Used for union filtering in non-conflict-only mode.
     */
    [[nodiscard]] auto meshPassesAnyModFilter(const PGPatcher::MeshMeta& meshMeta) const -> bool;

    /**
     * @brief Return true if the shape has at least one match from any mod in m_filterMods.
     *        Used for union filtering in non-conflict-only mode.
     */
    [[nodiscard]] auto shapePassesAnyModFilter(const PGPatcher::MeshShapeMeta& shape) const -> bool;

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

    /**
     * @brief Load resources/warning.svg and build the warning icon image lists for the mesh and match lists.
     */
    void setupWarningIcons();

    /**
     * @brief Attach or detach the warning icon image lists based on the "Show Mismatches" checkbox state.
     */
    void applyWarningIconVisibility();

    /**
     * @brief Warning tooltip for a mesh row when the mesh file belongs to a tracked mod outside m_filterMods.
     *        Returns an empty string when no warning applies (no mod filter, mesh owned by a filtered mod, or
     *        mesh from vanilla/untracked sources which are assumed correct).
     */
    [[nodiscard]] auto getMeshWarningTooltip(const std::filesystem::path& meshPath) const -> wxString;

    /**
     * @brief Warning tooltip for a match row whose result textures come from more than one mod.
     *        Lists each result texture slot with its owning mod. Empty string when no warning applies.
     */
    [[nodiscard]] static auto buildResultTexturesTooltip(const MatchView& match) -> wxString;

    /// @brief Display name for a texture slot (e.g. "Diffuse", "Normal").
    [[nodiscard]] static auto getSlotDisplayName(PGEnums::TextureSlots slot) -> wxString;

    /**
     * @brief Show/hide the list's native tooltip based on whether the cursor is over a row's warning icon.
     *
     * @param list List control the motion event belongs to.
     * @param tooltips Per-row warning texts for that list (empty string = no warning on that row).
     * @param event The motion event (skipped before returning).
     */
    static void updateHoverTooltip(wxListCtrl* list,
                                   const std::vector<wxString>& tooltips,
                                   wxMouseEvent& event);

    // ---- Event handlers ----------------------------------------------------

    void onMeshSelected(wxListEvent& event);
    void onMeshDeselected(wxListEvent& event);
    void onMeshActivated(wxListEvent& event);
    void onMeshContextMenu(wxContextMenuEvent& event);
    void onShapeSelected(wxListEvent& event);
    void onShapeDeselected(wxListEvent& event);
    void onShapeContextMenu(wxContextMenuEvent& event);
    void onSearchChanged(wxCommandEvent& event);
    void onShowDisabledChanged(wxCommandEvent& event);
    void onShowOnlyConflictsChanged(wxCommandEvent& event);
    void onShowMismatchesChanged(wxCommandEvent& event);
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
