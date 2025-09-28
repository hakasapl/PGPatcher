#include "GUI/components/CheckedColorDragListCtrl.hpp"
#include <wx/gdicmn.h>

#include <algorithm>

using namespace std;

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

wxDEFINE_EVENT(pgEVT_CCDLC_ITEM_DRAGGED, ItemDraggedEvent);
auto ItemDraggedEvent::Clone() const -> wxEvent* { return new ItemDraggedEvent(*this); }

wxDEFINE_EVENT(pgEVT_CCDLC_ITEM_CHECKED, ItemCheckedEvent);
auto ItemCheckedEvent::Clone() const -> wxEvent* { return new ItemCheckedEvent(*this); }

CheckedColorDragListCtrl::CheckedColorDragListCtrl(
    wxWindow* parent, wxWindowID id, const wxPoint& pt, const wxSize& sz, long style)
    : wxListCtrl(parent, id, pt, sz, style)
    , m_scrollTimer(this)
    , m_ghost(nullptr)
    , m_cutoffLine(-1)
{
    Bind(wxEVT_TIMER, &CheckedColorDragListCtrl::onTimer, this, m_scrollTimer.GetId());

    Bind(wxEVT_LEFT_DOWN, &CheckedColorDragListCtrl::onMouseLeftDown, this);
    Bind(wxEVT_MOTION, &CheckedColorDragListCtrl::onMouseMotion, this);
    Bind(wxEVT_LEFT_UP, &CheckedColorDragListCtrl::onMouseLeftUp, this);
    Bind(wxEVT_CONTEXT_MENU, &CheckedColorDragListCtrl::onContextMenu, this);

    // Create a native-size checkbox image list
    const wxSize chkSize = wxRendererNative::Get().GetCheckBoxSize(this);
    m_imagelist = new wxImageList(chkSize.GetWidth(), chkSize.GetHeight(), true);
    SetImageList(m_imagelist, wxIMAGE_LIST_SMALL);

    wxBitmap unchecked(chkSize);
    wxBitmap checked(chkSize);
    wxMemoryDC dc;

    // Draw unchecked
    dc.SelectObject(unchecked);
    dc.SetBackground(wxBrush(GetBackgroundColour(), wxBRUSHSTYLE_SOLID));
    dc.Clear();
    wxRendererNative::Get().DrawCheckBox(this, dc, wxRect(0, 0, chkSize.GetWidth(), chkSize.GetHeight()), 0);

    // Draw checked
    dc.SelectObject(checked);
    dc.SetBackground(wxBrush(GetBackgroundColour(), wxBRUSHSTYLE_SOLID));
    dc.Clear();
    wxRendererNative::Get().DrawCheckBox(
        this, dc, wxRect(0, 0, chkSize.GetWidth(), chkSize.GetHeight()), wxCONTROL_CHECKED);

    dc.SelectObject(wxNullBitmap);

    m_imagelist->Add(unchecked);
    m_imagelist->Add(checked);
}

auto CheckedColorDragListCtrl::isChecked(long item) const -> bool
{
    wxListItem info;
    info.m_mask = wxLIST_MASK_IMAGE;
    info.m_itemId = item;
    if (GetItem(info)) {
        return info.m_image == 1;
    }

    return false;
}

void CheckedColorDragListCtrl::check(long item, bool checked)
{
    SetItemImage(item, checked ? 1 : 0);
    SetItemTextColour(item, checked ? *wxBLACK : *wxLIGHT_GREY);

    // Fire a custom event
    ItemCheckedEvent evt(GetId(), item, checked);
    evt.SetEventObject(this);
    wxPostEvent(GetParent(), evt);
}

void CheckedColorDragListCtrl::setCutoffLine(int index) { m_cutoffLine = index; }

void CheckedColorDragListCtrl::moveItem(long fromIndex, long toIndex)
{
    if (fromIndex == toIndex || fromIndex < 0 || fromIndex >= GetItemCount()) {
        return;
    }

    // Capture item data
    const wxString col0 = GetItemText(fromIndex, 0);
    wxString col1;
    if (GetColumnCount() > 1) {
        col1 = GetItemText(fromIndex, 1);
    } else {
        col1 = wxString {};
    }
    const wxColour bgColor = GetItemBackgroundColour(fromIndex);
    const bool checked = isChecked(fromIndex);

    // Remove the item
    DeleteItem(fromIndex);

    // Adjust toIndex if the deletion was above the target
    if (fromIndex < toIndex) {
        toIndex--;
    }

    // Insert item at new position
    const long newIndex = InsertItem(toIndex, col0);
    if (GetColumnCount() > 1) {
        SetItem(newIndex, 1, col1);
    }

    // Restore properties
    SetItemBackgroundColour(newIndex, bgColor);
    check(newIndex, checked);

    // Fire a custom event
    const ItemDraggedEvent evt(GetId(), fromIndex, newIndex);
    wxPostEvent(this, evt);
}

void CheckedColorDragListCtrl::moveItems(const std::vector<long>& fromIndices, long toIndex)
{
    if (fromIndices.empty() || toIndex < 0 || toIndex > GetItemCount()) {
        return;
    }

    const bool movingDown = fromIndices.front() < toIndex;

    std::vector<long> sortedIndices = fromIndices;
    if (movingDown) {
        std::ranges::sort(sortedIndices, std::greater<>()); // bottom > top
    } else {
        std::ranges::sort(sortedIndices); // top > bottom
    }

    for (long i = 0; i < sortedIndices.size(); i++) {
        const long oldIndex = sortedIndices[i];
        long newIndex = toIndex;

        if (movingDown) {
            newIndex -= i; // shift down each subsequent item
        } else {
            newIndex += i; // shift up each subsequent item
        }

        moveItem(oldIndex, newIndex);
    }
}

void CheckedColorDragListCtrl::processCheckItem(long item, bool checked)
{
    // when checked, move item to just above cutoff line
    if (m_cutoffLine >= 0) {
        if (checked && item >= m_cutoffLine) {
            const long targetIndex = m_cutoffLine;
            m_cutoffLine++;
            moveItem(item, targetIndex);
        } else if (!checked && item < m_cutoffLine) {
            moveItem(item, m_cutoffLine);
            m_cutoffLine--;
        }
    }
}

void CheckedColorDragListCtrl::processCheckItems(const std::vector<long>& items, bool checked)
{
    if (items.empty() || m_cutoffLine < 0) {
        return;
    }

    std::vector<long> sortedItems = items;

    if (checked) {
        // Move checked items up: top > bottom
        std::ranges::sort(sortedItems);
    } else {
        // Move unchecked items down: bottom > top
        std::ranges::sort(sortedItems, std::greater<>());
    }

    for (const long item : sortedItems) {
        processCheckItem(item, checked);
    }
}

// EVENT HANDLERS

void CheckedColorDragListCtrl::onContextMenu(wxContextMenuEvent& event)
{
    int flags = 0;
    wxPoint point = event.GetPosition();
    point = ScreenToClient(point);

    const long clickedItem = HitTest(point, flags);
    if (clickedItem == wxNOT_FOUND) {
        return;
    }

    wxMenu menu;

    // Menu IDs
    constexpr int ID_MOVE_TOP = 1001;
    constexpr int ID_MOVE_BOTTOM = 1002;
    constexpr int ID_ENABLE = 1003;
    constexpr int ID_DISABLE = 1004;

    menu.Append(ID_MOVE_TOP, "Move to Top");
    menu.Append(ID_MOVE_BOTTOM, "Move to Bottom");
    menu.AppendSeparator();
    menu.Append(ID_ENABLE, "Enable");
    menu.Append(ID_DISABLE, "Disable");

    // Gather all selected items
    std::vector<long> selectedItems;
    long sel = -1;
    while ((sel = GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != wxNOT_FOUND) {
        selectedItems.push_back(sel);
    }

    // If nothing is selected, select the clicked item
    if (selectedItems.empty()) {
        selectedItems.push_back(clickedItem);
        SetItemState(clickedItem, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    }

    // Disable move options if any selected item is below the cutoff line
    const bool anyBelowCutoff
        = std::ranges::any_of(selectedItems, [this](long idx) { return m_cutoffLine >= 0 && idx >= m_cutoffLine; });
    menu.Enable(ID_MOVE_TOP, !anyBelowCutoff);
    menu.Enable(ID_MOVE_BOTTOM, !anyBelowCutoff);

    // Bind menu actions
    menu.Bind(
        wxEVT_MENU,
        [this, selectedItems](wxCommandEvent&) -> void {
            // Move all items to top
            moveItems(selectedItems, 0);
        },
        ID_MOVE_TOP);

    menu.Bind(
        wxEVT_MENU,
        [this, selectedItems](wxCommandEvent&) -> void {
            // Move all items to bottom (just above cutoff line)
            const long insertPos = m_cutoffLine >= 0 ? m_cutoffLine : GetItemCount();
            moveItems(selectedItems, insertPos);
        },
        ID_MOVE_BOTTOM);

    menu.Bind(
        wxEVT_MENU,
        [this, selectedItems](wxCommandEvent&) -> void {
            // Enable (check) all selected items
            // check all selected items
            for (const long item : selectedItems) {
                check(item, true);
            }
            processCheckItems(selectedItems, true);
        },
        ID_ENABLE);

    menu.Bind(
        wxEVT_MENU,
        [this, selectedItems](wxCommandEvent&) -> void {
            // Disable (uncheck) all selected items
            for (const long item : selectedItems) {
                check(item, false);
            }
            processCheckItems(selectedItems, false);
        },
        ID_DISABLE);

    PopupMenu(&menu);
}

void CheckedColorDragListCtrl::onMouseLeftDown(wxMouseEvent& event)
{
    int flags = 0;
    const long item = HitTest(event.GetPosition(), flags);

    if (item != wxNOT_FOUND) {
        // Clicked on the checkbox part
        if ((flags & wxLIST_HITTEST_ONITEMICON) != 0) {
            check(item, !isChecked(item));

            processCheckItem(item, isChecked(item));

            event.Skip();
            return;
        }

        if (event.LeftDown()) {
            // Ignore items below cutoff
            if (m_cutoffLine >= 0 && item >= m_cutoffLine) {
                event.Skip();
                return;
            }

            const bool ctrl = event.ControlDown();
            const bool shift = event.ShiftDown();
            const bool alreadySelected = (GetItemState(item, wxLIST_STATE_SELECTED) & wxLIST_STATE_SELECTED) != 0;

            if (!ctrl && !shift) {
                if (!alreadySelected) {
                    // Clicked a new item > clear all and select just this one
                    long sel = -1;
                    while ((sel = GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != wxNOT_FOUND) {
                        SetItemState(sel, 0, wxLIST_STATE_SELECTED);
                    }
                    SetItemState(item, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                }
                // else: clicked inside existing selection > keep it as-is
            } else {
                // Ctrl/Shift modifiers > let default wxWidgets selection logic work
                event.Skip();
            }

            // Capture all selected indices for dragging
            m_draggedRows.clear();
            long selectedItem = -1;
            while ((selectedItem = GetNextItem(selectedItem, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != wxNOT_FOUND) {
                m_draggedRows.push_back({ .index = selectedItem, .text = GetItemText(selectedItem, 0) });
            }

            // Create ghost at cursor position
            // Reset to nullptr just in case
            m_ghost = nullptr;
            // loop through each dragged row and create a single string
            vector<wxString> combinedText;
            combinedText.reserve(m_draggedRows.size());
            for (const auto& row : m_draggedRows) {
                combinedText.push_back(row.text);
            }

            m_ghost = new DragGhostWindow(nullptr, combinedText);
            const wxPoint pos = ClientToScreen(event.GetPosition() + wxPoint(4, 4));
            m_ghost->Move(pos);

            // We initially hide the ghost until we start moving
            m_ghost->Hide();
        }
    }

    event.Skip();
}

void CheckedColorDragListCtrl::onMouseLeftUp(wxMouseEvent& event)
{
    if (!m_draggedRows.empty() && m_targetLineIndex != -1) {
        if (m_cutoffLine >= 0 && m_targetLineIndex > m_cutoffLine) {
            m_targetLineIndex = m_cutoffLine;
        }

        // m_overlay.Reset(); // Clear the m_overlay when the drag operation is complete
        vector<long> draggedIndices;
        draggedIndices.reserve(m_draggedRows.size());
        for (const auto& row : m_draggedRows) {
            draggedIndices.push_back(row.index);
        }

        moveItems(draggedIndices, m_targetLineIndex);

        // Fire custom event for the *last dragged item* (or the first, your choice)
        ItemDraggedEvent dragEvt(GetId(), draggedIndices.front(), m_targetLineIndex);
        dragEvt.SetEventObject(this);
        wxPostEvent(GetParent(), dragEvt);

        // Reset drag state
        m_draggedRows.clear();
        m_targetLineIndex = -1;
    }

    // Stop the timer when the drag operation ends
    if (m_scrollTimer.IsRunning()) {
        m_scrollTimer.Stop();
    }

    if (m_ghost != nullptr) {
        m_ghost->Destroy();
        m_ghost = nullptr;
    }

    event.Skip();
}

void CheckedColorDragListCtrl::onMouseMotion(wxMouseEvent& event)
{
    if (!m_draggedRows.empty() && event.LeftIsDown()) {
        // Start the timer to handle scrolling
        if (!m_scrollTimer.IsRunning()) {
            m_scrollTimer.Start(TIMER_INTERVAL); // Start the timer with a 50ms interval
        }

        // Move ghost window
        if (m_ghost != nullptr) {
            const wxPoint pos = ClientToScreen(event.GetPosition() + wxPoint(4, 4));
            m_ghost->updatePosition(pos);
            m_ghost->Show();
        }

        int flags = 0;
        auto dropTargetIndex = HitTest(event.GetPosition(), flags);

        if (dropTargetIndex != wxNOT_FOUND) {
            wxRect itemRect;
            GetItemRect(dropTargetIndex, itemRect);

            // Check if the mouse is in the top or bottom half of the item
            const int midPointY = itemRect.GetTop() + (itemRect.GetHeight() / 2);
            const auto curPosition = event.GetPosition().y;
            const bool targetingTopHalf = curPosition > midPointY;

            if (targetingTopHalf) {
                dropTargetIndex++;
            }

            // Clamp drop target above cutoff
            if (m_cutoffLine >= 0 && dropTargetIndex > m_cutoffLine) {
                dropTargetIndex = m_cutoffLine;
            }

            // Draw the line only if the target index has changed
            // drawDropIndicator(dropTargetIndex);
            m_targetLineIndex = dropTargetIndex;
        } else {
            // Clear m_overlay if not hovering over a valid item
            // m_overlay.Reset();
            m_targetLineIndex = -1;
        }
    }

    if (m_ghost != nullptr) {
        const wxPoint pos = ClientToScreen(event.GetPosition() + wxPoint(4, 4));
        m_ghost->updatePosition(pos);
    }

    event.Skip();
}

void CheckedColorDragListCtrl::onTimer([[maybe_unused]] wxTimerEvent& event)
{
    // Get the current mouse position relative to the m_listCtrl
    const wxPoint mousePos = ScreenToClient(wxGetMousePosition());
    const wxRect listCtrlRect = GetClientRect();

    // Check if the mouse is within the m_listCtrl bounds
    if (listCtrlRect.Contains(mousePos)) {
        const int scrollMargin = 20; // Margin to trigger scrolling
        const int mouseY = mousePos.y;

        if (mouseY < listCtrlRect.GetTop() + scrollMargin + AUTOSCROLL_MARGIN) {
            // Scroll up if the mouse is near the top edge
            ScrollLines(-1);
        } else if (mouseY > listCtrlRect.GetBottom() - scrollMargin) {
            // Scroll down if the mouse is near the bottom edge
            ScrollLines(1);
        }
    }
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
