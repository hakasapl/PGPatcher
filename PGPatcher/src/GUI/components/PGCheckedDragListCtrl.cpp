#include <wx/gdicmn.h>
#include <wx/renderer.h>

#include <algorithm>
#include <unordered_map>

#include "GUI/components/PGCheckedDragListCtrl.hpp"
#include "GUI/components/PGCheckedDragListCtrlEvtItemChecked.hpp"
#include "GUI/components/PGCheckedDragListCtrlEvtItemDragged.hpp"
#include "GUI/components/PGCheckedDragListCtrlGhostWindow.hpp"

using namespace std;

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

PGCheckedDragListCtrl::PGCheckedDragListCtrl(
    wxWindow* parent, wxWindowID id, const wxPoint& pt, const wxSize& sz, long style)
    : wxListCtrl(parent, id, pt, sz, style)
    , m_autoscrollTimer(this)
    , m_ghost(nullptr)
{
    // Bind Event Handlers
    Bind(wxEVT_TIMER, &PGCheckedDragListCtrl::onAutoscrollTimer, this, m_autoscrollTimer.GetId());

    Bind(wxEVT_LEFT_DOWN, &PGCheckedDragListCtrl::onMouseLeftDown, this);
    Bind(wxEVT_MOTION, &PGCheckedDragListCtrl::onMouseMotion, this);
    Bind(wxEVT_LEFT_UP, &PGCheckedDragListCtrl::onMouseLeftUp, this);
    Bind(wxEVT_CONTEXT_MENU, &PGCheckedDragListCtrl::onContextMenu, this);

    // Setup checkboxes
    const wxSize chkSize = wxRendererNative::Get().GetCheckBoxSize(this);
    m_imagelist = new wxImageList(chkSize.GetWidth(), chkSize.GetHeight(), true);
    AssignImageList(m_imagelist, wxIMAGE_LIST_SMALL);

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

PGCheckedDragListCtrl::~PGCheckedDragListCtrl()
{
    // We must verify the timer is stopped and the ghost is killed on destruction
    if (m_autoscrollTimer.IsRunning()) {
        m_autoscrollTimer.Stop();
    }

    if (m_ghost != nullptr) {
        m_ghost->Destroy();
        m_ghost = nullptr;
    }
}

void PGCheckedDragListCtrl::check(long item, bool checked)
{
    // This is what actually adds the checkmark
    SetItemImage(item, checked ? 1 : 0);
    // Grays out the text if unchecked
    SetItemTextColour(item, checked ? *wxBLACK : *wxLIGHT_GREY);
}

auto PGCheckedDragListCtrl::isChecked(long item) const -> bool
{
    wxListItem info;
    info.m_mask = wxLIST_MASK_IMAGE;
    info.m_itemId = item;
    if (GetItem(info)) {
        return info.m_image == 1;
    }

    return false;
}

void PGCheckedDragListCtrl::setCutoffLine(int index) { m_cutoffLine = index; }

auto PGCheckedDragListCtrl::getCutoffLine() const -> int { return m_cutoffLine; }

void PGCheckedDragListCtrl::setDraggingEnabled(bool enabled) { m_draggingEnabled = enabled; }

auto PGCheckedDragListCtrl::isDraggingEnabled() const -> bool { return m_draggingEnabled; }

// EVENT HANDLERS

void PGCheckedDragListCtrl::onMouseLeftDown(wxMouseEvent& event)
{
    int flags = 0;
    const long item = HitTest(event.GetPosition(), flags);

    // Not clicked on any item
    if (item == wxNOT_FOUND) {
        event.Skip();
        return;
    }

    // Clicked on the checkbox part
    if ((flags & wxLIST_HITTEST_ONITEMICON) != 0) {
        check(item, !isChecked(item));
        processCheckItem(item, isChecked(item));

        event.Skip();
        return;
    }

    // Clicked on the item part
    if (m_draggingEnabled && event.LeftDown()) {
        // Ignore items below cutoff
        if (m_cutoffLine >= 0 && item >= m_cutoffLine) {
            event.Skip();
            return;
        }

        const bool alreadySelected = (GetItemState(item, wxLIST_STATE_SELECTED) & wxLIST_STATE_SELECTED) != 0;
        if (!event.ControlDown() && !event.ShiftDown()) {
            if (!alreadySelected) {
                // Clicked a new item > clear all and select just this one
                clearAllSelections();
                SetItemState(item, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
            }
            // else: clicked inside existing selection > keep it as-is
        } else {
            // Ctrl/Shift modifiers > let default wxWidgets selection logic work
            event.Skip();
        }

        // Capture all selected indices for dragging
        m_draggedRows.clear();
        auto selectedItems = getSelectedItems();

        // Deselect any items below cutoff line
        std::erase_if(selectedItems, [this](long idx) -> bool { return m_cutoffLine >= 0 && idx >= m_cutoffLine; });

        for (const long selectedItem : selectedItems) {
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

        m_ghost = new PGCheckedDragListCtrlGhostWindow(nullptr, combinedText);
        const wxPoint pos = ClientToScreen(event.GetPosition() + wxPoint(4, 4));
        m_ghost->Move(pos);

        // We initially hide the ghost until we start moving
        m_ghost->Hide();
    }

    event.Skip();
}

void PGCheckedDragListCtrl::onMouseMotion(wxMouseEvent& event)
{
    if (m_draggedRows.empty() || !event.LeftIsDown()) {
        event.Skip();
        return;
    }

    // Verify the timer is running
    if (!m_autoscrollTimer.IsRunning()) {
        m_autoscrollTimer.Start(AUTOSCROLL_TIMER_INTERVAL); // Start the timer with a 50ms interval
    }

    // Update ghost position
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
        const bool targetingBottomHalf = curPosition > midPointY;
        if (targetingBottomHalf) {
            dropTargetIndex++;
        }

        // Clamp drop target above cutoff
        if (m_cutoffLine >= 0 && dropTargetIndex > m_cutoffLine) {
            dropTargetIndex = m_cutoffLine;
        }

        m_targetLineIndex = dropTargetIndex;
    } else {
        m_targetLineIndex = -1;
    }

    event.Skip();
}

void PGCheckedDragListCtrl::onMouseLeftUp(wxMouseEvent& event)
{
    // Stop the timer when the drag operation ends
    if (m_autoscrollTimer.IsRunning()) {
        m_autoscrollTimer.Stop();
    }

    // Clear the ghost window
    if (m_ghost != nullptr) {
        m_ghost->Destroy();
        m_ghost = nullptr;
    }

    if (m_draggedRows.empty() || m_targetLineIndex == -1) {
        event.Skip();
        return;
    }

    if (m_cutoffLine >= 0 && m_targetLineIndex > m_cutoffLine) {
        m_targetLineIndex = m_cutoffLine;
    }

    // m_overlay.Reset(); // Clear the m_overlay when the drag operation is complete
    vector<long> draggedIndices;
    draggedIndices.reserve(m_draggedRows.size());
    for (const auto& row : m_draggedRows) {
        draggedIndices.push_back(row.index);
    }

    const auto newIdx = moveItems(draggedIndices, m_targetLineIndex);

    // Re-select the moved items
    for (const long idx : newIdx) {
        SetItemState(idx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    }

    // Fire custom event for the *last dragged item* (or the first, your choice)
    PGCheckedDragListCtrlEvtItemDragged dragEvt(GetId(), draggedIndices.front(), m_targetLineIndex);
    dragEvt.SetEventObject(this);
    wxPostEvent(this, dragEvt);

    // Reset drag state
    m_draggedRows.clear();
    m_targetLineIndex = -1;

    event.Skip();
}

void PGCheckedDragListCtrl::onAutoscrollTimer([[maybe_unused]] wxTimerEvent& event)
{
    static constexpr int AUTOSCROLL_MARGIN = 30; /** Margin in pixels to trigger autoscroll */
    static constexpr int AUTOSCROLL_HEADER_SIZE = 30; /** Header size in pixels to offset autoscroll */

    // Get the current mouse position relative to the m_listCtrl
    const wxPoint mousePos = ScreenToClient(wxGetMousePosition());
    const wxRect listCtrlRect = GetClientRect();

    // Check if the mouse is within the m_listCtrl bounds
    if (listCtrlRect.Contains(mousePos)) {
        const int mouseY = mousePos.y;

        if (mouseY < listCtrlRect.GetTop() + AUTOSCROLL_MARGIN + AUTOSCROLL_HEADER_SIZE) {
            // Scroll up if the mouse is near the top edge
            ScrollLines(-1);
        } else if (mouseY > listCtrlRect.GetBottom() - AUTOSCROLL_MARGIN) {
            // Scroll down if the mouse is near the bottom edge
            ScrollLines(1);
        }
    }
}

void PGCheckedDragListCtrl::onContextMenu(wxContextMenuEvent& event)
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
    static constexpr int ID_MOVE_TOP = 1001;
    static constexpr int ID_MOVE_BOTTOM = 1002;
    static constexpr int ID_ENABLE = 1003;
    static constexpr int ID_DISABLE = 1004;

    menu.Append(ID_MOVE_TOP, "Move to Top");
    menu.Append(ID_MOVE_BOTTOM, "Move to Bottom");
    menu.AppendSeparator();
    menu.Append(ID_ENABLE, "Enable");
    menu.Append(ID_DISABLE, "Disable");

    // Gather all selected items
    std::vector<long> selectedItems = getSelectedItems();

    // If nothing is selected, select the clicked item
    if (selectedItems.empty()) {
        selectedItems.push_back(clickedItem);
        SetItemState(clickedItem, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    }

    // Disable move options if any selected item is below the cutoff line
    const bool anyBelowCutoff = std::ranges::any_of(
        selectedItems, [this](long idx) -> bool { return m_cutoffLine >= 0 && idx >= m_cutoffLine; });
    menu.Enable(ID_MOVE_TOP, !anyBelowCutoff && m_draggingEnabled);
    menu.Enable(ID_MOVE_BOTTOM, !anyBelowCutoff && m_draggingEnabled);

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

// HELPERS

void PGCheckedDragListCtrl::processCheckItem(long item, bool checked)
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

    // Fire a custom event
    PGCheckedDragListCtrlEvtItemChecked evt(GetId(), item, checked);
    evt.SetEventObject(this);
    wxPostEvent(this, evt);
}

void PGCheckedDragListCtrl::processCheckItems(const std::vector<long>& items, bool checked)
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

auto PGCheckedDragListCtrl::moveItem(long fromIndex, long toIndex) -> long
{
    if (fromIndex == toIndex || fromIndex < 0 || fromIndex >= GetItemCount()) {
        return fromIndex;
    }

    // Capture item data (all columns)
    const int colCount = GetColumnCount();
    std::vector<wxString> cols;
    cols.reserve(std::max(1, colCount));
    for (int c = 0; c < colCount; ++c) {
        cols.push_back(GetItemText(fromIndex, c));
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
    const long newIndex = InsertItem(toIndex, cols.empty() ? wxString {} : cols[0]);
    for (int c = 1; c < colCount; ++c) {
        SetItem(newIndex, c, cols[c]);
    }

    // Restore properties
    SetItemBackgroundColour(newIndex, bgColor);
    check(newIndex, checked);

    return newIndex;
}

auto PGCheckedDragListCtrl::moveItems(const std::vector<long>& fromIndices, long toIndex) -> vector<long>
{
    if (fromIndices.empty() || toIndex < 0 || toIndex > GetItemCount()) {
        return fromIndices;
    }

    const bool movingDown = fromIndices.front() < toIndex;

    std::vector<long> sortedIndices = fromIndices;
    if (movingDown) {
        std::ranges::sort(sortedIndices, std::greater<>()); // bottom > top
    } else {
        std::ranges::sort(sortedIndices); // top > bottom
    }

    // Map original index -> new index
    std::unordered_map<long, long> indexMap;

    for (size_t i = 0; i < sortedIndices.size(); i++) {
        const long oldIndex = sortedIndices[i];
        long newIndex = toIndex;

        if (movingDown) {
            newIndex -= static_cast<long>(i); // shift down each subsequent item
        } else {
            newIndex += static_cast<long>(i); // shift up each subsequent item
        }

        const long finalIndex = moveItem(oldIndex, newIndex);
        indexMap[oldIndex] = finalIndex;
    }

    // Return new indices in the **same order as fromIndices**
    std::vector<long> newIndices;
    newIndices.reserve(fromIndices.size());
    for (const long oldIdx : fromIndices) {
        newIndices.push_back(indexMap[oldIdx]);
    }

    return newIndices;
}

auto PGCheckedDragListCtrl::getSelectedItems() const -> std::vector<long>
{
    std::vector<long> selectedItems;
    long sel = -1;
    while ((sel = GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != wxNOT_FOUND) {
        selectedItems.push_back(sel);
    }
    return selectedItems;
}

void PGCheckedDragListCtrl::clearAllSelections()
{
    long sel = -1;
    while ((sel = GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != wxNOT_FOUND) {
        SetItemState(sel, 0, wxLIST_STATE_SELECTED);
    }
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
