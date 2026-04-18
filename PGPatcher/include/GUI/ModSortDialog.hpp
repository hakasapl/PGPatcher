#pragma once

#include "GUI/components/PGCheckedDragListCtrl.hpp"
#include "GUI/components/PGCheckedDragListCtrlEvtItemChecked.hpp"
#include "GUI/components/PGCheckedDragListCtrlEvtItemDragged.hpp"
#include "GUI/components/PGCheckedDragListCtrlEvtMeshesIgnoredChanged.hpp"
#include "PGModManager.hpp"
#include "pgutil/PGEnums.hpp"

#include <wx/string.h>
#include <wx/wx.h>

#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

/**
 * @brief wxDialog that allows the user to sort the mods in the order they want
 */
class ModSortDialog : public wxDialog {
private:
    PGCheckedDragListCtrl* m_listCtrl = nullptr; /** Main list object that stores all the mods */

    wxButton* m_applyButton = nullptr; /** Apply button to save changes without closing the dialog */
    wxButton* m_discardButton = nullptr; /** Discard changes button to revert to last saved state */
    wxButton* m_restoreButton = nullptr; /** Restore default order button */
    wxCheckBox* m_checkBoxMO2 = nullptr; /** Checkbox to use MO2 loose file order */
    wxTextCtrl* m_searchCtrl = nullptr; /** Search box used to quickly find mods by name */

    struct CachedModRow {
        std::wstring modName;
        wxString shaderString;
        bool isChecked = false;
        bool areMeshesIgnored = false;
        bool isNew = false;
    };

    std::vector<CachedModRow> m_cachedRows; /** Cached full mod list state used for filtering */

    std::unordered_set<std::wstring>
        m_newMods; /** Stores the original highlight of elements to be able to restore it later */

    constexpr static int DEFAULT_WIDTH = 600;
    constexpr static int DEFAULT_HEIGHT = 600;
    constexpr static int MIN_WIDTH = 600;
    constexpr static int MIN_HEIGHT = 400;
    constexpr static int DEFAULT_PADDING = 20;
    constexpr static int DEFAULT_BORDER = 10;
    constexpr static int HELPBTN_FONT_SIZE = 12;
    constexpr static int HELPBTN_SIZE = 30;

    static inline const wxColour s_NEW_MOD_COLOR {213, 128, 255};
    static inline const wxColour s_LOSING_MOD_COLOR {255, 102, 102};
    static inline const wxColour s_WINNING_MOD_COLOR {204, 255, 102};

    static inline wxColour s_BASE_ITEM_BG_COLOR = *wxWHITE;
    static inline wxColour s_BASE_ITEM_FG_COLOR = *wxBLACK;

public:
    /**
     * @brief Construct a new Mod Sort Dialog object
     */
    ModSortDialog();

private:
    // Event Handlers

    /**
     * @brief Event handler that triggers when an item is selected in the list (highlighting)
     *
     * @param event wxWidgets event object
     */
    void onItemSelected(wxListEvent& event);

    /**
     * @brief Event handler that triggers when an item is deselected in the list
     *
     * @param event wxWidgets event object
     */
    void onItemDeselected(wxListEvent& event);

    /**
     * @brief Event handler that triggers when an item is dragged in the list
     *
     * @param event wxWidgets event object
     */
    void onItemDragged(PGCheckedDragListCtrlEvtItemDragged& event);

    /**
     * @brief Event handler that triggers when an item is checked/unchecked in the list
     *
     * @param event wxWidgets event object
     */
    void onItemChecked(PGCheckedDragListCtrlEvtItemChecked& event);

    /**
     * @brief Event handler that triggers when one or more items are toggled for mesh ignore state
     *
     * @param event wxWidgets event object
     */
    void onMeshesIgnoredChanged(PGCheckedDragListCtrlEvtMeshesIgnoredChanged& event);

    /**
     * @brief Event handler that triggers when the list control is resized
     *
     * @param event wxWidgets event object
     */
    void onListCtrlResize(wxSizeEvent& event);

    /**
     * @brief Close event handler
     *
     * @param event wxWidgets event object
     */
    void onClose(wxCloseEvent& event);

    /**
     * @brief Event handler that triggers when the Close button is pressed
     *
     * @param event wxWidgets event object
     */
    void onBtnClose(wxCommandEvent& event);

    /**
     * @brief Event handler that triggers when the OK button is pressed
     *
     * @param event wxWidgets event object
     */
    void onOkay(wxCommandEvent& event);

    /**
     * @brief Event handler that triggers when the Apply button is pressed
     *
     * @param event wxWidgets event object
     */
    void onApply(wxCommandEvent& event);

    /**
     * @brief Event handler that triggers when the Restore Default button is pressed
     *
     * @param event wxWidgets event object
     */
    void onRestoreDefault(wxCommandEvent& event);

    /**
     * @brief Event handler that triggers when the Discard Changes button is pressed
     *
     * @param event wxWidgets event object
     */
    void onDiscardChanges(wxCommandEvent& event);

    /**
     * @brief Event handler that triggers when the "Use MO2 Loose File Order" checkbox is changed
     *
     * @param event wxWidgets event object
     */
    void onUseMO2LooseFileOrderChange(wxCommandEvent& event);

    /**
     * @brief Event handler that triggers when the search text is changed
     *
     * @param event wxWidgets event object
     */
    void onSearchTextChanged(wxCommandEvent& event);

    // Helpers

    /**
     * @brief Sets the state of the "Use MO2 Loose File Order" checkbox based on whether MO2 is being used
     */
    void setMO2LooseFileOrderCheckboxState();

    /**
     * @brief Calculates the width of a column in the list
     *
     * @param colIndex Index of column to calculate
     * @return int Width of column
     */
    auto calculateColumnWidth(int colIndex) -> int;

    /**
     * @brief Highlights the conflicting items for a selected mod
     */
    void highlightConflictingItems();

    /**
     * @brief Clear all yellow highlights from the list
     */
    void clearAllHighlights();

    /**
     * @brief Updates the mods in the PGModManager based on the current state of the list control
     */
    void updateMods();

    /**
     * @brief Fills the list control with the given mod list
     *
     * @param modList List of mods to fill the list control with
     * @param autoEnable If true, will autoenable any disabled mods that have shaders other than NONE
     */
    void fillListCtrl(const std::vector<std::shared_ptr<PGModManager::Mod>>& modList,
                      bool autoEnable = false,
                      bool preserveChecks = false);

    /**
     * @brief Rebuilds the cached rows from the currently visible list control state.
     */
    void rebuildCacheFromListCtrl();

    /**
     * @brief Merges currently visible list state into cache and updates visible-subset ordering.
     */
    void syncCacheFromListCtrl();

    /**
     * @brief Rebuilds the visible list control from cached rows and current search term.
     */
    void rebuildListCtrlFromCache();

    /**
     * @brief Returns trimmed lowercase search text.
     */
    [[nodiscard]] auto getActiveSearchTerm() const -> wxString;

    /**
     * @brief Returns ordered view of cached rows: enabled rows first, disabled rows second.
     *
     * @return Vector of pointers to cached rows in display order.
     */
    [[nodiscard]] auto getOrderedCachedRows() const -> std::vector<const CachedModRow*>;

    /**
     * @brief Reorders the full cached enabled list for a move-top or move-bottom action while search is active.
     *
     * @param selectedModNames Selected visible mod names in display order
     * @param moveToTop true to move to top of enabled rows, false to move to bottom of enabled rows
     */
    void reorderCachedRowsFromFilteredMove(const std::vector<std::wstring>& selectedModNames,
                                           bool moveToTop);

    /**
     * @brief Enables or disables the apply button based on whether there are unsaved changes
     */
    void updateApplyButtonState();

    /**
     * @brief Constructs a comma-separated string of shader names from a set of ShapeShader enums
     *
     * @param shaders Set of ShapeShader enums
     * @return wxString Comma-separated string of shader names
     */
    static auto constructShaderString(const std::set<PGEnums::ShapeShader>& shaders) -> wxString;
};
