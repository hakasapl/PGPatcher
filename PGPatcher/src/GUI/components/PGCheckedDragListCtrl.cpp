#include "GUI/components/PGCheckedDragListCtrl.hpp"

#include "GUI/components/PGCheckedDragListCtrlEvtItemChecked.hpp"
#include "GUI/components/PGCheckedDragListCtrlEvtItemDragged.hpp"
#include "GUI/components/PGCheckedDragListCtrlEvtMeshesIgnoredChanged.hpp"
#include "GUI/components/PGCheckedDragListCtrlGhostWindow.hpp"
#include "PGLocale.hpp"

#include <wx/renderer.h>
#include <wx/wx.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <vector>

// Disable owning memory checks because wxWidgets will take care of deleting the objects.
// Disable convert member functions to static because these functions need to be non-static for wxWidgets.
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

PGCheckedDragListCtrl::PGCheckedDragListCtrl(wxWindow* parent,
                                             wxWindowID id,
                                             const wxPoint& pt,
                                             const wxSize& sz,
                                             long style)
    : wxListCtrl(parent,
                 id,
                 pt,
                 sz,
                 style)
    , m_autoscrollTimer(this)

{
    // Bind Event Handlers.
    Bind(wxEVT_TIMER, &PGCheckedDragListCtrl::onAutoscrollTimer, this, m_autoscrollTimer.GetId());

    Bind(wxEVT_LEFT_DOWN, &PGCheckedDragListCtrl::onMouseLeftDown, this);
    Bind(wxEVT_MOTION, &PGCheckedDragListCtrl::onMouseMotion, this);
    Bind(wxEVT_LEFT_UP, &PGCheckedDragListCtrl::onMouseLeftUp, this);
    Bind(wxEVT_CONTEXT_MENU, &PGCheckedDragListCtrl::onContextMenu, this);

    // Setup checkboxes.
    const wxSize chkSize = wxRendererNative::Get().GetCheckBoxSize(this);
    m_imagelist = new wxImageList(chkSize.GetWidth(), chkSize.GetHeight(), true);
    AssignImageList(m_imagelist, wxIMAGE_LIST_SMALL);

    wxBitmap unchecked(chkSize);
    wxBitmap checked(chkSize);
    wxMemoryDC dc;

    // Draw unchecked.
    dc.SelectObject(unchecked);
    dc.SetBackground(wxBrush(GetBackgroundColour(), wxBRUSHSTYLE_SOLID));
    dc.Clear();
    wxRendererNative::Get().DrawCheckBox(this, dc, wxRect(0, 0, chkSize.GetWidth(), chkSize.GetHeight()), 0);

    // Draw checked.
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
    // We must verify the timer is stopped and the ghost is killed on destruction.
    if (m_autoscrollTimer.IsRunning())
        m_autoscrollTimer.Stop();

    if (m_ghost != nullptr) {
        m_ghost->Destroy();
        m_ghost = nullptr;
    }
}

void PGCheckedDragListCtrl::check(long item,
                                  bool checked)
{
    // This is what actually adds the checkmark.
    SetItemImage(item, checked ? 1 : 0);
}

auto PGCheckedDragListCtrl::isChecked(long item) const -> bool
{
    wxListItem info;
    info.m_mask = wxLIST_MASK_IMAGE;
    info.m_itemId = item;
    if (GetItem(info))
        return info.m_image == 1;

    return false;
}

void PGCheckedDragListCtrl::ignoreMeshes(long item,
                                         bool ignore)
{
    wxFont font = GetItemFont(item);
    font.SetStrikethrough(ignore);

    SetItemFont(item, font);
}

auto PGCheckedDragListCtrl::areMeshesIgnored(long item) const -> bool
{
    const wxFont font = GetItemFont(item);
    return font.IsOk() && font.GetStrikethrough();
}

void PGCheckedDragListCtrl::setCutoffLine(int index) { m_cutoffLine = index; }

auto PGCheckedDragListCtrl::getCutoffLine() const -> int { return m_cutoffLine; }

void PGCheckedDragListCtrl::setDraggingEnabled(bool enabled) { m_isDraggingEnabled = enabled; }

void PGCheckedDragListCtrl::setContextMoveEnabled(bool enabled) { m_isContextMoveEnabled = enabled; }

auto PGCheckedDragListCtrl::isDraggingEnabled() const -> bool { return m_isDraggingEnabled; }

auto PGCheckedDragListCtrl::isContextMoveEnabled() const -> bool { return m_isContextMoveEnabled; }

void PGCheckedDragListCtrl::setContextMenuExtension(std::function<void(wxMenu&,
                                                                       const std::vector<long>&)> extension)
{
    m_contextMenuExtension = std::move(extension);
}

// EVENT HANDLERS.

void PGCheckedDragListCtrl::onMouseLeftDown(wxMouseEvent& event)
{
    int flags = 0;
    const long item = HitTest(event.GetPosition(), flags);

    // Not clicked on any item.
    if (item == wxNOT_FOUND) {
        event.Skip();
        return;
    }

    // Clicked on the checkbox part.
    if ((flags & wxLIST_HITTEST_ONITEMICON) != 0) {
        check(item, !isChecked(item));
        processCheckItem(item, isChecked(item));

        event.Skip();
        return;
    }

    // Clicked on the item part.
    if (m_isDraggingEnabled && event.LeftDown()) {
        // Ignore items below cutoff.
        if (m_cutoffLine >= 0 && item >= m_cutoffLine) {
            event.Skip();
            return;
        }

        const bool alreadySelected = (GetItemState(item, wxLIST_STATE_SELECTED) & wxLIST_STATE_SELECTED) != 0;
        if (!event.ControlDown() && !event.ShiftDown()) {
            if (!alreadySelected) {
                // Clicked a new item > clear all and select just this one.
                clearAllSelections();
                SetItemState(item, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
            }
            // Else: clicked inside existing selection > keep it as-is.
        } else {
            // Ctrl/Shift modifiers > let default wxWidgets selection logic work.
            event.Skip();
        }

        // Capture all selected indices for dragging.
        m_draggedRows.clear();
        auto selectedItems = getSelectedItems();

        // Deselect any items below cutoff line.
        std::erase_if(selectedItems, [this](long idx) -> bool { return m_cutoffLine >= 0 && idx >= m_cutoffLine; });

        for (const long selectedItem : selectedItems)
            m_draggedRows.push_back({ .index = selectedItem, .text = GetItemText(selectedItem, 0) });

        // Create ghost at cursor position.
        // Reset to nullptr just in case.
        m_ghost = nullptr;
        // Loop through each dragged row and create a single string.
        std::vector<wxString> combinedText;
        combinedText.reserve(m_draggedRows.size());
        for (const auto& row : m_draggedRows)
            combinedText.push_back(row.text);

        m_ghost = new PGCheckedDragListCtrlGhostWindow(nullptr, combinedText);
        const wxPoint pos = ClientToScreen(event.GetPosition() + FromDIP(wxPoint(4, 4)));
        m_ghost->Move(pos);

        // We initially hide the ghost until we start moving.
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

    // Verify the timer is running.
    if (!m_autoscrollTimer.IsRunning())
        m_autoscrollTimer.Start(autoscrollTimerInterval); // Start the timer with a 50ms interval

    // Update ghost position.
    if (m_ghost != nullptr) {
        const wxPoint pos = ClientToScreen(event.GetPosition() + FromDIP(wxPoint(4, 4)));
        m_ghost->updatePosition(pos);
        m_ghost->Show();
    }

    int flags = 0;
    auto dropTargetIndex = HitTest(event.GetPosition(), flags);

    if (dropTargetIndex != wxNOT_FOUND) {
        wxRect itemRect;
        GetItemRect(dropTargetIndex, itemRect);

        // Check if the mouse is in the top or bottom half of the item.
        const int midPointY = itemRect.GetTop() + (itemRect.GetHeight() / 2);
        const auto curPosition = event.GetPosition().y;
        const bool targetingBottomHalf = curPosition > midPointY;
        if (targetingBottomHalf)
            dropTargetIndex++;

        // Clamp drop target above cutoff.
        if (m_cutoffLine >= 0 && dropTargetIndex > m_cutoffLine)
            dropTargetIndex = m_cutoffLine;

        m_targetLineIndex = dropTargetIndex;
    } else {
        m_targetLineIndex = -1;
    }

    event.Skip();
}

void PGCheckedDragListCtrl::onMouseLeftUp(wxMouseEvent& event)
{
    // Stop the timer when the drag operation ends.
    if (m_autoscrollTimer.IsRunning())
        m_autoscrollTimer.Stop();

    // Clear the ghost window.
    if (m_ghost != nullptr) {
        m_ghost->Destroy();
        m_ghost = nullptr;
    }

    if (m_draggedRows.empty() || m_targetLineIndex == -1) {
        event.Skip();
        return;
    }

    if (m_cutoffLine >= 0 && m_targetLineIndex > m_cutoffLine)
        m_targetLineIndex = m_cutoffLine;

    // m_overlay.Reset(); // Clear the m_overlay when the drag operation is complete
    std::vector<long> draggedIndices;
    draggedIndices.reserve(m_draggedRows.size());
    for (const auto& row : m_draggedRows)
        draggedIndices.push_back(row.index);

    const auto newIdx = moveItems(draggedIndices, m_targetLineIndex);

    // Re-select the moved items.
    for (const long idx : newIdx)
        SetItemState(idx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

    // Fire custom event for the *last dragged item* (or the first, your choice).
    PGCheckedDragListCtrlEvtItemDragged dragEvt(GetId(), draggedIndices.front(), m_targetLineIndex);
    dragEvt.SetEventObject(this);
    wxPostEvent(this, dragEvt);

    // Reset drag state.
    m_draggedRows.clear();
    m_targetLineIndex = -1;

    event.Skip();
}

void PGCheckedDragListCtrl::onAutoscrollTimer([[maybe_unused]] wxTimerEvent& event)
{
    static constexpr int autoscrollMarginDIP = 30; /** Margin in DIPs to trigger autoscroll */
    static constexpr int autoscrollHeaderSizeDIP = 30; /** Header size in DIPs to offset autoscroll */
    const int autoscrollMargin = FromDIP(autoscrollMarginDIP);
    const int autoscrollHeaderSize = FromDIP(autoscrollHeaderSizeDIP);

    // Get the current mouse position relative to the m_listCtrl.
    const wxPoint mousePos = ScreenToClient(wxGetMousePosition());
    const wxRect listCtrlRect = GetClientRect();

    // Check if the mouse is within the m_listCtrl bounds.
    if (listCtrlRect.Contains(mousePos)) {
        const int mouseY = mousePos.y;

        if (mouseY < listCtrlRect.GetTop() + autoscrollMargin + autoscrollHeaderSize) {
            // Scroll up if the mouse is near the top edge.
            ScrollLines(-1);
        } else if (mouseY > listCtrlRect.GetBottom() - autoscrollMargin) {
            // Scroll down if the mouse is near the bottom edge.
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
    if (clickedItem == wxNOT_FOUND)
        return;

    wxMenu menu;

    // Menu IDs.
    static constexpr int idMoveTop = 1001;
    static constexpr int idMoveBottom = 1002;
    static constexpr int idEnable = 1003;
    static constexpr int idDisable = 1004;
    static constexpr int idEnableMeshes = 1005;
    static constexpr int idDisableMeshes = 1006;

    menu.Append(idMoveTop, pgTr("components.checkedDragList.moveToTop"));
    menu.Append(idMoveBottom, pgTr("components.checkedDragList.moveToBottom"));
    menu.AppendSeparator();
    menu.Append(idEnable, pgTr("common.enable"));
    menu.Append(idDisable, pgTr("common.disable"));
    menu.AppendSeparator();
    menu.Append(idEnableMeshes, pgTr("components.checkedDragList.patchMeshes"));
    menu.Append(idDisableMeshes, pgTr("components.checkedDragList.ignoreMeshes"));

    // Gather all selected items.
    std::vector<long> selectedItems = getSelectedItems();

    // If nothing is selected, select the clicked item.
    if (selectedItems.empty()) {
        selectedItems.push_back(clickedItem);
        SetItemState(clickedItem, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    }

    // Disable move options if any selected item is below the cutoff line.
    const bool anyBelowCutoff = std::ranges::any_of(
        selectedItems, [this](long idx) -> bool { return m_cutoffLine >= 0 && idx >= m_cutoffLine; });
    menu.Enable(idMoveTop, !anyBelowCutoff && m_isContextMoveEnabled);
    menu.Enable(idMoveBottom, !anyBelowCutoff && m_isContextMoveEnabled);

    // Disable enable/disable options if all selected items are already in that state.
    const bool allEnabled = std::ranges::all_of(selectedItems, [this](long idx) -> bool { return isChecked(idx); });
    const bool allDisabled = std::ranges::all_of(selectedItems, [this](long idx) -> bool { return !isChecked(idx); });
    menu.Enable(idEnable, !allEnabled);
    menu.Enable(idDisable, !allDisabled);

    // Disable mesh patching options if all selected items are already in that state.
    const bool allIgnoringMeshes
        = std::ranges::all_of(selectedItems, [this](long idx) -> bool { return areMeshesIgnored(idx); });
    const bool allPatchingMeshes
        = std::ranges::all_of(selectedItems, [this](long idx) -> bool { return !areMeshesIgnored(idx); });
    menu.Enable(idEnableMeshes, !allPatchingMeshes);
    menu.Enable(idDisableMeshes, !allIgnoringMeshes);

    // Bind menu actions.
    menu.Bind(
        wxEVT_MENU,
        [this, selectedItems](wxCommandEvent&) -> void {
            // Move all items to top.
            const auto newIndices = moveItems(selectedItems, 0);
            clearAllSelections();
            for (const long idx : newIndices)
                SetItemState(idx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

            PGCheckedDragListCtrlEvtItemDragged dragEvt(GetId(), selectedItems.front(), 0);
            dragEvt.SetEventObject(this);
            wxPostEvent(this, dragEvt);
        },
        idMoveTop);

    menu.Bind(
        wxEVT_MENU,
        [this, selectedItems](wxCommandEvent&) -> void {
            // Move all items to bottom (just above cutoff line).
            const long insertPos = m_cutoffLine >= 0 ? m_cutoffLine : GetItemCount();
            const auto newIndices = moveItems(selectedItems, insertPos);
            clearAllSelections();
            for (const long idx : newIndices)
                SetItemState(idx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

            PGCheckedDragListCtrlEvtItemDragged dragEvt(GetId(), selectedItems.front(), insertPos);
            dragEvt.SetEventObject(this);
            wxPostEvent(this, dragEvt);
        },
        idMoveBottom);

    menu.Bind(
        wxEVT_MENU,
        [this, selectedItems](wxCommandEvent&) -> void {
            // Enable (check) all selected items.
            // Check all selected items.
            for (const long item : selectedItems)
                check(item, true);
            processCheckItems(selectedItems, true);
        },
        idEnable);

    menu.Bind(
        wxEVT_MENU,
        [this, selectedItems](wxCommandEvent&) -> void {
            // Disable (uncheck) all selected items.
            for (const long item : selectedItems)
                check(item, false);
            processCheckItems(selectedItems, false);
        },
        idDisable);

    menu.Bind(
        wxEVT_MENU,
        [this, selectedItems](wxCommandEvent&) -> void {
            // Enable patching meshes for all selected items.
            for (const long item : selectedItems)
                ignoreMeshes(item, false);

            PGCheckedDragListCtrlEvtMeshesIgnoredChanged evt(GetId());
            evt.SetEventObject(this);
            wxPostEvent(this, evt);
        },
        idEnableMeshes);

    menu.Bind(
        wxEVT_MENU,
        [this, selectedItems](wxCommandEvent&) -> void {
            // Disable patching meshes for all selected items.
            for (const long item : selectedItems)
                ignoreMeshes(item, true);

            PGCheckedDragListCtrlEvtMeshesIgnoredChanged evt(GetId());
            evt.SetEventObject(this);
            wxPostEvent(this, evt);
        },
        idDisableMeshes);

    // Allow the owner to append extra items (e.g. "Show Conflicts...").
    if (m_contextMenuExtension) {
        menu.AppendSeparator();
        m_contextMenuExtension(menu, selectedItems);
    }

    PopupMenu(&menu);
}

// HELPERS.

void PGCheckedDragListCtrl::processCheckItem(long item,
                                             bool checked)
{
    // When checked, move item to just above cutoff line.
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

    // Fire a custom event.
    PGCheckedDragListCtrlEvtItemChecked evt(GetId(), item, checked);
    evt.SetEventObject(this);
    wxPostEvent(this, evt);
}

void PGCheckedDragListCtrl::processCheckItems(const std::vector<long>& items,
                                              bool checked)
{
    if (items.empty() || m_cutoffLine < 0)
        return;

    const long topItem = GetTopItem();
    Freeze();

    std::vector<long> sortedItems = items;

    if (checked) {
        // Move checked items up: top > bottom.
        std::ranges::sort(sortedItems);
    } else {
        // Move unchecked items down: bottom > top.
        std::ranges::sort(sortedItems, std::greater<>());
    }

    for (const long item : sortedItems)
        processCheckItem(item, checked);

    Thaw();

    if (topItem > 0 && topItem < GetItemCount()) {
        EnsureVisible(GetItemCount() - 1);
        EnsureVisible(topItem);
    }
}

auto PGCheckedDragListCtrl::moveItem(long fromIndex,
                                     long toIndex) -> long
{
    if (fromIndex == toIndex || fromIndex < 0 || fromIndex >= GetItemCount())
        return fromIndex;

    const long topItem = GetTopItem(); // preserve scroll position across delete/insert

    Freeze();

    // Capture item data (all columns).
    const int colCount = GetColumnCount();
    std::vector<wxString> cols;
    cols.reserve(std::max(1, colCount));
    for (int c = 0; c < colCount; ++c)
        cols.push_back(GetItemText(fromIndex, c));
    const wxColour bgColor = GetItemBackgroundColour(fromIndex);
    const bool curChecked = isChecked(fromIndex);
    const bool curIgnoreMeshes = areMeshesIgnored(fromIndex);

    // Remove the item.
    DeleteItem(fromIndex);

    // Adjust toIndex if the deletion was above the target.
    if (fromIndex < toIndex)
        toIndex--;

    // Insert item at new position.
    const long newIndex = InsertItem(toIndex, cols.empty() ? wxString { } : cols.at(0));
    for (int c = 1; c < colCount; ++c)
        SetItem(newIndex, c, cols.at(static_cast<size_t>(c)));

    // Restore properties.
    SetItemBackgroundColour(newIndex, bgColor);
    check(newIndex, curChecked);
    ignoreMeshes(newIndex, curIgnoreMeshes);

    Thaw();

    // EnsureVisible while frozen is a no-op on the native Windows ListView (WM_SETREDRAW=FALSE).
    // Call it AFTER Thaw. Thaw only posts InvalidateRect (deferred paint), not UpdateWindow,
    // so EnsureVisible here runs before WM_PAINT fires - one clean repaint at the right position.
    // Double-EnsureVisible: scroll to bottom first so the next call must scroll up,
    // pinning topItem to the top of the viewport.
    if (topItem > 0 && topItem < GetItemCount()) {
        EnsureVisible(GetItemCount() - 1);
        EnsureVisible(topItem);
    }

    return newIndex;
}

auto PGCheckedDragListCtrl::moveItems(const std::vector<long>& fromIndices,
                                      long toIndex) -> std::vector<long>
{
    if (fromIndices.empty() || toIndex < 0 || toIndex > GetItemCount())
        return fromIndices;

    const bool movingDown = fromIndices.front() < toIndex;

    std::vector<long> sortedIndices = fromIndices;
    if (movingDown)
        std::ranges::sort(sortedIndices, std::greater<>()); // bottom > top
    else
        std::ranges::sort(sortedIndices); // top > bottom

    // Map original index -> new index
    std::unordered_map<long, long> indexMap;

    for (size_t i = 0; i < sortedIndices.size(); i++) {
        const long oldIndex = sortedIndices.at(i);
        long newIndex = toIndex;

        if (movingDown)
            newIndex -= static_cast<long>(i); // shift down each subsequent item
        else
            newIndex += static_cast<long>(i); // shift up each subsequent item

        const long finalIndex = moveItem(oldIndex, newIndex);
        indexMap[oldIndex] = finalIndex;
    }

    // Return new indices in the **same order as fromIndices**
    std::vector<long> newIndices;
    newIndices.reserve(fromIndices.size());
    for (const long oldIdx : fromIndices)
        newIndices.push_back(indexMap[oldIdx]);

    return newIndices;
}

auto PGCheckedDragListCtrl::getSelectedItems() const -> std::vector<long>
{
    std::vector<long> selectedItems;
    long sel = -1;
    while ((sel = GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != wxNOT_FOUND)
        selectedItems.push_back(sel);
    return selectedItems;
}

void PGCheckedDragListCtrl::clearAllSelections()
{
    long sel = -1;
    while ((sel = GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != wxNOT_FOUND)
        SetItemState(sel, 0, wxLIST_STATE_SELECTED);
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
