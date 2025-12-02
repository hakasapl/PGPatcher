#pragma once

#include <cstdint>
#include <wx/listctrl.h>
#include <wx/wx.h>

class PGModifiableListCtrl : public wxListCtrl {
public:
    /**
     * @brief Construct a new PGModifiableListCtrl object
     *
     * @param parent parent window
     * @param id window ID
     * @param pt position
     * @param sz size
     * @param style window style (default wxLC_REPORT)
     */
    PGModifiableListCtrl(wxWindow* parent, wxWindowID id, const wxPoint& pt = wxDefaultPosition,
        const wxSize& sz = wxDefaultSize, long style = wxLC_REPORT);

private:
    enum class ContextMenu : uint16_t { ID_PG_ADD_ITEM = wxID_HIGHEST + 1, ID_PG_REMOVE_ITEM };

    /**
     * @brief Event handler responsible for deleting/adding items based on list edits
     *
     * @param event wxWidgets event object
     */
    void onListEdit(wxListEvent& event);

    /**
     * @brief Event handler responsible for activating list items on double click or enter
     *
     * @param event wxWidgets event object
     */
    void onListItemActivated(wxListEvent& event);

    /**
     * @brief Event handler for context menu
     *
     * @param event wxWidgets event object
     */
    void onContextMenu(wxContextMenuEvent& event);

    /**
     * @brief Event handler for adding an item
     *
     * @param event wxWidgets event object
     */
    void onAddItem(wxCommandEvent& event);

    /**
     * @brief Event handler for removing selected items
     *
     * @param event wxWidgets event object
     */
    void onRemoveItem(wxCommandEvent& event);
};
