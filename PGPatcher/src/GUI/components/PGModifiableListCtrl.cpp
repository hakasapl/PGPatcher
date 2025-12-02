#include "GUI/components/PGModifiableListCtrl.hpp"

#include "GUI/components/PGCustomListctrlChangedEvent.hpp"

using namespace std;

PGModifiableListCtrl::PGModifiableListCtrl(
    wxWindow* parent, wxWindowID id, const wxPoint& pt, const wxSize& sz, long style)
    : wxListCtrl(parent, id, pt, sz, style)
{
    // Bind events
    Bind(wxEVT_LIST_END_LABEL_EDIT, &PGModifiableListCtrl::onListEdit, this);
    Bind(wxEVT_LIST_ITEM_ACTIVATED, &PGModifiableListCtrl::onListItemActivated, this);

    Bind(wxEVT_CONTEXT_MENU, &PGModifiableListCtrl::onContextMenu, this);
    Bind(wxEVT_MENU, &PGModifiableListCtrl::onAddItem, this, static_cast<int>(ContextMenu::ID_PG_ADD_ITEM));
    Bind(wxEVT_MENU, &PGModifiableListCtrl::onRemoveItem, this, static_cast<int>(ContextMenu::ID_PG_REMOVE_ITEM));
}

void PGModifiableListCtrl::onListEdit(wxListEvent& event)
{
    if (event.IsEditCancelled()) {
        return;
    }

    const auto editedIndex = event.GetIndex();
    const auto& editedText = event.GetLabel();

    if (editedText.IsEmpty() && editedIndex != GetItemCount() - 1) {
        // If the edited item is empty and it's not the last item
        this->CallAfter([this, editedIndex]() -> void { DeleteItem(editedIndex); });

        // Fire event for list change
        PGCustomListctrlChangedEvent changeEvt(GetId(), editedIndex);
        changeEvt.SetEventObject(this);
        wxPostEvent(this, changeEvt);
        return;
    }

    // If the edited item is not empty and it's the last item
    if (!editedText.IsEmpty() && editedIndex == GetItemCount() - 1) {
        // Add a new empty item
        InsertItem(GetItemCount(), "");
    }

    // Fire event for list change
    PGCustomListctrlChangedEvent changeEvt(GetId(), editedIndex);
    changeEvt.SetEventObject(this);
    wxPostEvent(this, changeEvt);
}

void PGModifiableListCtrl::onListItemActivated(wxListEvent& event)
{
    const auto itemIndex = event.GetIndex();

    // Start editing the label of the item
    EditLabel(itemIndex);
}

void PGModifiableListCtrl::onContextMenu([[maybe_unused]] wxContextMenuEvent& event)
{
    wxMenu menu;
    menu.Append(static_cast<int>(ContextMenu::ID_PG_ADD_ITEM), "Add");
    auto* removeItem = menu.Append(static_cast<int>(ContextMenu::ID_PG_REMOVE_ITEM), "Remove");

    const long lastIndex = GetItemCount() - 1; // trailing blank row
    const long selectedCount = GetSelectedItemCount();
    const bool onlyBlankSelected
        = (selectedCount == 1 && GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == lastIndex);

    // Disable Remove if nothing selected or only the trailing blank is selected
    if (selectedCount == 0 || onlyBlankSelected) {
        removeItem->Enable(false);
    }

    PopupMenu(&menu);
}

void PGModifiableListCtrl::onAddItem([[maybe_unused]] wxCommandEvent& event)
{
    const long lastIndex = GetItemCount() - 1;

    // Begin editing the appropriate blank row
    EditLabel(lastIndex);
}

void PGModifiableListCtrl::onRemoveItem([[maybe_unused]] wxCommandEvent& event)
{
    long item = -1;
    const long lastIndex = GetItemCount() - 1; // trailing blank row
    bool removed = false;

    // Remove all selected items, but skip the last blank row
    for (;;) {
        item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (item == wxNOT_FOUND) {
            break;
        }
        if (item == lastIndex) {
            continue; // skip the always-present blank row
        }

        DeleteItem(item);
        removed = true;
        item--; // continue from same index
    }

    if (!removed) {
        return;
    }

    // Fire change event
    PGCustomListctrlChangedEvent evt(GetId(), -1);
    evt.SetEventObject(this);
    wxPostEvent(this, evt);
}
