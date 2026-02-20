#pragma once

#include "GUI/components/PGTextureMapListCtrl.hpp"

#include "pgutil/PGNIFUtil.hpp"

#include <wx/listctrl.h>
#include <wx/wx.h>

#include <string>
#include <utility>
#include <vector>

/**
 * @brief wxDialog that wraps a PGTextureMapListCtrl to allow the user to configure texture type mappings.
 *
 * Presents a resizable dialog with an instruction label, a two-column editable list
 * (texture path and texture type), and OK/Cancel buttons. Mappings can be pre-populated
 * via populateList() and retrieved after the dialog is accepted via getList().
 */
class DialogTextureMapListCtrl : public wxDialog {
private:
    PGTextureMapListCtrl* m_listCtrl;
    wxStaticText* m_helpText;

public:
    /**
     * @brief Construct the DialogTextureMapListCtrl dialog.
     *
     * @param parent Parent wxWindow, or nullptr for a top-level dialog.
     * @param title  Title text shown in the dialog's title bar.
     * @param text   Instruction text displayed above the list control.
     */
    DialogTextureMapListCtrl(wxWindow* parent,
                             const wxString& title,
                             const wxString& text);

    /**
     * @brief Retrieve all non-empty texture-map entries currently in the list.
     *
     * Rows with an empty texture path are skipped.
     *
     * @return Vector of (wide-string texture path, TextureType) pairs.
     */
    [[nodiscard]] auto getList() const -> std::vector<std::pair<std::wstring,
                                                                PGEnums::TextureType>>;

    /**
     * @brief Populate the list control with the provided texture-map entries.
     *
     * Clears any existing entries, inserts each (path, type) pair, and appends an
     * empty row at the end to allow the user to add new entries.
     *
     * @param items Vector of (wide-string texture path, TextureType) pairs to insert.
     */
    void populateList(const std::vector<std::pair<std::wstring,
                                                  PGEnums::TextureType>>& items);

private:
    /**
     * @brief Resize the first (texture-path) column to fill the space not occupied by the type column.
     *
     * Called on dialog resize events and after list content changes. The second column
     * retains its fixed width; the first column takes the remaining client width (minimum 50 px).
     */
    void updateColumnWidths();
};
