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
