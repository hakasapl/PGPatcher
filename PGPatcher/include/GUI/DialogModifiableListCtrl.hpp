#pragma once

#include "GUI/components/PGModifiableListCtrl.hpp"

#include <wx/listctrl.h>
#include <wx/wx.h>

#include <string>
#include <vector>

/**
 * @brief wxDialog that wraps a PGModifiableListCtrl to allow the user to add/remove items from a list.
 *
 * Presents a resizable dialog with an instruction label, an editable single-column
 * list control, and OK/Cancel buttons. Items can be pre-populated via populateList()
 * and retrieved after the dialog is accepted via getList().
 */
class DialogModifiableListCtrl : public wxDialog {
private:
    PGModifiableListCtrl* m_listCtrl;
    wxStaticText* m_helpText;

public:
    /**
     * @brief Construct the DialogModifiableListCtrl dialog.
     *
     * @param parent Parent wxWindow, or nullptr for a top-level dialog.
     * @param title  Title text shown in the dialog's title bar.
     * @param text   Instruction text displayed above the list control.
     */
    DialogModifiableListCtrl(wxWindow* parent,
                             const wxString& title,
                             const wxString& text);

    /**
     * @brief Retrieve all non-empty items currently in the list.
     *
     * @return Vector of wide strings representing each list entry.
     */
    [[nodiscard]] auto getList() const -> std::vector<std::wstring>;

    /**
     * @brief Populate the list control with the provided items.
     *
     * Clears any existing entries, inserts each item, and appends an empty row at the
     * end to allow the user to add new entries.
     *
     * @param items Wide strings to insert into the list.
     */
    void populateList(const std::vector<std::wstring>& items);

private:
    /**
     * @brief Resize the single list column to fill the available client width.
     *
     * Called on dialog resize events and after list content changes to keep the
     * column width in sync with the list control's client area.
     */
    void updateColumnWidth();
};
