#pragma once

#include "PGPlugin.hpp"

#include <wx/listctrl.h>
#include <wx/wx.h>

#include <unordered_set>

/**
 * @brief wxDialog that lets the user select which plugin record types are allowed for model patching.
 *
 * Presents a resizable dialog with an instruction label, a checkable list of all available
 * record types, and OK/Cancel buttons. The list supports right-click context menus to
 * enable/disable multiple selected items at once.
 */
class DialogRecTypeSelector : public wxDialog {
private:
    wxListCtrl* m_listCtrl;

public:
    /**
     * @brief Construct the DialogRecTypeSelector dialog.
     *
     * @param parent Parent wxWindow, or nullptr for a top-level dialog.
     * @param title  Title text shown in the dialog's title bar.
     */
    DialogRecTypeSelector(wxWindow* parent,
                          const wxString& title = "Allowed Record Types");

    /**
     * @brief Retrieve the set of record types that are currently checked.
     *
     * @return Unordered set of ModelRecordType values corresponding to checked list entries.
     */
    [[nodiscard]] auto getSelectedRecordTypes() const -> std::unordered_set<PGPlugin::ModelRecordType>;

    /**
     * @brief Populate the list with all available record types, checking those in the provided set.
     *
     * Inserts every entry from PGPlugin::getAvailableRecTypeStrs() into the list and marks
     * each one checked if its corresponding ModelRecordType is present in selectedRecTypes.
     * Also resizes the dialog height to display all items without scrolling.
     *
     * @param selectedRecTypes Set of record types that should appear checked initially.
     */
    void populateList(const std::unordered_set<PGPlugin::ModelRecordType>& selectedRecTypes);
};
