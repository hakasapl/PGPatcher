#include "GUI/components/CheckedColorDragListCtrl.hpp"
#include <wx/gdicmn.h>

using namespace std;

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

auto ItemDraggedEvent::Clone() const -> wxEvent* { return new ItemDraggedEvent(*this); }
wxDEFINE_EVENT(wxEVT_LIST_ITEM_DRAGGED, ItemDraggedEvent);

CheckedColorDragListCtrl::CheckedColorDragListCtrl(
    wxWindow* parent, wxWindowID id, const wxPoint& pt, const wxSize& sz, long style)
    : wxListCtrl(parent, id, pt, sz, style)
    , m_scrollTimer(this)
    , m_listCtrlHeaderHeight(getHeaderHeight())
    , m_cutoffLine(-1)
{
    Bind(wxEVT_TIMER, &CheckedColorDragListCtrl::onTimer, this, m_scrollTimer.GetId());

    Bind(wxEVT_LEFT_DOWN, &CheckedColorDragListCtrl::onMouseLeftDown, this);
    Bind(wxEVT_MOTION, &CheckedColorDragListCtrl::onMouseMotion, this);
    Bind(wxEVT_LEFT_UP, &CheckedColorDragListCtrl::onMouseLeftUp, this);

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

// EVENT HANDLERS

void CheckedColorDragListCtrl::onMouseLeftDown(wxMouseEvent& event)
{
    int flags = 0;
    const long item = HitTest(event.GetPosition(), flags);

    if (item != wxNOT_FOUND) {
        // Clicked on the checkbox part
        if ((flags & wxLIST_HITTEST_ONITEMICON) != 0) {
            check(item, !isChecked(item));
        } else if (event.LeftDown()) {
            // Ignore items below cutoff
            if (m_cutoffLine >= 0 && item >= m_cutoffLine) {
                event.Skip();
                return;
            }

            // Clicked elsewhere on the row — handle selection/dragging
            // Select item if not already selected
            if (!(event.ControlDown() || event.ShiftDown())) {
                if ((GetItemState(item, wxLIST_STATE_SELECTED) & wxLIST_STATE_SELECTED) == 0) {
                    SetItemState(item, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                }
            }

            // Capture all selected indices for dragging
            m_draggedIndices.clear();
            long selectedItem = -1;
            while ((selectedItem = GetNextItem(selectedItem, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != wxNOT_FOUND) {
                m_draggedIndices.push_back(selectedItem);
            }
        }
    }

    event.Skip();
}

void CheckedColorDragListCtrl::onMouseLeftUp(wxMouseEvent& event)
{
    if (!m_draggedIndices.empty() && m_targetLineIndex != -1) {
        if (m_cutoffLine >= 0 && m_targetLineIndex > m_cutoffLine) {
            m_targetLineIndex = m_cutoffLine;
        }

        m_overlay.Reset(); // Clear the m_overlay when the drag operation is complete

        // Sort indices to maintain the order during removal
        std::ranges::sort(m_draggedIndices);

        // Determine target insertion point
        const long insertPos = m_targetLineIndex;

        // Move each item individually using moveItem
        long numRemovedAboveInsertPos = 0;
        long numRemovedBelowInsertPos = 0;
        for (const long oldIndex : m_draggedIndices) {
            // Move the item and update the insertion position for the next item
            moveItem(oldIndex - numRemovedAboveInsertPos, insertPos + numRemovedBelowInsertPos);
            if (oldIndex < insertPos) {
                numRemovedAboveInsertPos++;
            }

            if (oldIndex > insertPos) {
                numRemovedBelowInsertPos++;
            }

            // insertPos++; // Increment insert position so items are inserted in order
        }

        // Reset drag state
        m_draggedIndices.clear();
        m_targetLineIndex = -1;
    }

    // Stop the timer when the drag operation ends
    if (m_scrollTimer.IsRunning()) {
        m_scrollTimer.Stop();
    }

    event.Skip();
}

void CheckedColorDragListCtrl::onMouseMotion(wxMouseEvent& event)
{
    if (!m_draggedIndices.empty() && event.LeftIsDown()) {
        // Start the timer to handle scrolling
        if (!m_scrollTimer.IsRunning()) {
            m_scrollTimer.Start(TIMER_INTERVAL); // Start the timer with a 50ms interval
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
            drawDropIndicator(dropTargetIndex);
            m_targetLineIndex = dropTargetIndex;
        } else {
            // Clear m_overlay if not hovering over a valid item
            m_overlay.Reset();
            m_targetLineIndex = -1;
        }
    }

    event.Skip();
}

void CheckedColorDragListCtrl::drawDropIndicator(int targetIndex)
{
    wxClientDC dc(this);
    wxDCOverlay dcOverlay(m_overlay, &dc);
    dcOverlay.Clear(); // Clear the existing m_overlay to avoid double lines

    wxRect itemRect;
    int lineY = -1;

    // Validate TargetIndex before calling GetItemRect()
    if (targetIndex >= 0 && targetIndex < GetItemCount()) {
        if (GetItemRect(targetIndex, itemRect)) {
            lineY = itemRect.GetTop();
        }
    } else if (targetIndex >= GetItemCount()) {
        // Handle drawing at the end of the list (after the last item)
        if (GetItemRect(GetItemCount() - 1, itemRect)) {
            lineY = itemRect.GetBottom();
        }
    }

    // Ensure LineY is set correctly before drawing
    if (lineY != -1) {
        dc.SetPen(wxPen(*wxBLACK, 2)); // Draw the line with a width of 2 pixels
        dc.DrawLine(itemRect.GetLeft(), lineY, itemRect.GetRight(), lineY);
    }
}

void CheckedColorDragListCtrl::onTimer([[maybe_unused]] wxTimerEvent& event)
{
    // Get the current mouse position relative to the m_listCtrl
    const wxPoint mousePos = ScreenToClient(wxGetMousePosition());
    const wxRect listCtrlRect = GetRect();

    // Check if the mouse is within the m_listCtrl bounds
    if (listCtrlRect.Contains(mousePos)) {
        const int scrollMargin = 20; // Margin to trigger scrolling
        const int mouseY = mousePos.y;

        if (mouseY < listCtrlRect.GetTop() + scrollMargin + m_listCtrlHeaderHeight) {
            // Scroll up if the mouse is near the top edge
            ScrollLines(-1);
        } else if (mouseY > listCtrlRect.GetBottom() - scrollMargin) {
            // Scroll down if the mouse is near the bottom edge
            ScrollLines(1);
        }
    }
}

auto CheckedColorDragListCtrl::getHeaderHeight() -> int
{
    if (GetItemCount() > 0) {
        wxRect firstItemRect;
        if (GetItemRect(0, firstItemRect)) {
            // The top of the first item minus the top of the client area gives the header height
            return firstItemRect.GetTop() - GetClientRect().GetTop();
        }
    }
    return 0; // Fallback if the list is empty
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
