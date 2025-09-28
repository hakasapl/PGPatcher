#pragma once

#include <wx/arrstr.h>
#include <wx/dnd.h>
#include <wx/dragimag.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/msw/textctrl.h>
#include <wx/overlay.h>
#include <wx/renderer.h>
#include <wx/sizer.h>
#include <wx/wx.h>

#include <string>

#include "GUI/components/CheckedColorDragListCtrl.hpp"
#include "ModManagerDirectory.hpp"

/**
 * @brief wxDialog that allows the user to sort the mods in the order they want
 */
class ModSortDialog : public wxDialog {
private:
    CheckedColorDragListCtrl* m_listCtrl; /** Main list object that stores all the mods */

    wxButton* m_applyButton; /** Apply button to save changes without closing the dialog */

    //
    // Item Highlighting
    //
    std::unordered_map<std::wstring, wxColour>
        m_originalBackgroundColors; /** Stores the original highlight of elements to be able to restore it later */

    constexpr static int DEFAULT_WIDTH = 300;
    constexpr static int DEFAULT_HEIGHT = 600;
    constexpr static int MIN_HEIGHT = 400;
    constexpr static int DEFAULT_PADDING = 20;
    constexpr static int DEFAULT_BORDER = 10;
    constexpr static int MIN_COL_WIDTH = 50;
    constexpr static int RECT_LABEL_FONT_SIZE = 20;

    static inline const wxColour s_NEW_MOD_COLOR { 243, 230, 255 };
    static inline const wxColour s_LOSING_MOD_COLOR { 255, 102, 102 };
    static inline const wxColour s_WINNING_MOD_COLOR { 204, 255, 102 };

public:
    /**
     * @brief Construct a new Mod Sort Dialog object
     */
    ModSortDialog();

private:
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

    void onItemDragged(ItemDraggedEvent& event);

    void onItemChecked(ItemCheckedEvent& event);

    /**
     * @brief Event handler that triggers when the list control is resized
     *
     * @param event wxWidgets event object
     */
    void onListCtrlResize(wxSizeEvent& event);

    /**
     * @brief Resets indices for the list after drag or sort
     *
     * @param event wxWidgets event object
     */
    void onClose(wxCloseEvent& event);

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

    void onRestoreDefault(wxCommandEvent& event);

    void onDiscardChanges(wxCommandEvent& event);

    /**
     * @brief Calculates the width of a column in the list
     *
     * @param colIndex Index of column to calculate
     * @return int Width of column
     */
    auto calculateColumnWidth(int colIndex) -> int;

    /**
     * @brief Highlights the conflicting items for a selected mod
     *
     * @param selectedMod Mod that is selected
     */
    void highlightConflictingItems(const std::wstring& selectedMod);

    /**
     * @brief Clear all yellow highlights from the list
     */
    void clearAllHighlights();

    void updateMods();

    void updateApplyButtonState();

    void fillListCtrl(
        const std::vector<std::shared_ptr<ModManagerDirectory::Mod>>& modList, const bool& autoEnable = false);

    static auto constructShaderString(const std::set<NIFUtil::ShapeShader>& shaders) -> std::string;
};
