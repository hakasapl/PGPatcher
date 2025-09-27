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
#include <vector>

#include "GUI/components/CheckedColorDragListCtrl.hpp"

/**
 * @brief wxDialog that allows the user to sort the mods in the order they want
 */
class ModSortDialog : public wxDialog {
private:
    CheckedColorDragListCtrl* m_listCtrl; /** Main list object that stores all the mods */

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

    static inline const wxColour s_NEW_MOD_COLOR { 243, 230, 255 };
    static inline const wxColour s_LOSING_MOD_COLOR { 255, 102, 102 };
    static inline const wxColour s_WINNING_MOD_COLOR { 204, 255, 102 };

public:
    /**
     * @brief Construct a new Mod Sort Dialog object
     */
    ModSortDialog();

    /**
     * @brief Get the list of sorted mods (meant to be called after the user presses okay)
     *
     * @return std::vector<std::wstring> list of sorted mods
     */
    [[nodiscard]] auto getSortedItems() const -> std::vector<std::wstring>;

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

    /**
     * @brief Event handler that triggers when the show disabled mods checkbox is toggled
     *
     * @param event wxWidgets event object
     */
    void onShowDisabledModsToggled(wxCommandEvent& event);

    void onListCtrlResize(wxSizeEvent& event);

    /**
     * @brief Resets indices for the list after drag or sort
     *
     * @param event wxWidgets event object
     */
    void onClose(wxCloseEvent& event);

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

    /**
     * @brief Reverses the order of the list
     */
    void reverseListOrder();
};
