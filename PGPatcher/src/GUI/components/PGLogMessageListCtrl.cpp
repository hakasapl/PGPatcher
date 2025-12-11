#include "GUI/components/PGLogMessageListCtrl.hpp"

#include <cstddef>
#include <unordered_map>
#include <vector>

wxDEFINE_EVENT(s_EVT_PG_LOG_IGNORE_CHANGED, wxCommandEvent);

PGLogMessageListCtrl::PGLogMessageListCtrl(wxWindow* parent, wxWindowID id, bool allowIgnore)
    : wxListCtrl(parent, id, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_HRULES | wxLC_VRULES | wxLC_NO_HEADER)
    , m_allowIgnore(allowIgnore)
    , m_showIgnored(false)
{
    InsertColumn(0, "Message", wxLIST_FORMAT_LEFT);

    Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) -> void {
        const int width = GetClientSize().GetWidth();
        const int vsWidth = GetScrollThumb(wxVERTICAL) > 0 ? wxSystemSettings::GetMetric(wxSYS_VSCROLL_X) : 0;

        SetColumnWidth(0, width - vsWidth);
        evt.Skip();
    });

    Bind(wxEVT_CONTEXT_MENU, &PGLogMessageListCtrl::onContextMenu, this);
}

void PGLogMessageListCtrl::setShowIgnored(bool showIgnored)
{
    m_showIgnored = showIgnored;
    repopulateList();
}

void PGLogMessageListCtrl::setLogMessages(const std::vector<wxString>& messages)
{
    m_allMessages = messages;
    repopulateList();
}

void PGLogMessageListCtrl::setIgnoreMap(const std::unordered_map<wxString, bool>& ignoredItems)
{
    m_ignoredItems = ignoredItems;
    repopulateList();
}

auto PGLogMessageListCtrl::getIgnoreMap() const -> const std::unordered_map<wxString, bool>& { return m_ignoredItems; }

auto PGLogMessageListCtrl::getNumUnignoredMessages() const -> size_t
{
    size_t count = 0;
    for (const auto& msg : m_allMessages) {
        if (!m_ignoredItems.contains(msg) || !m_ignoredItems.at(msg)) {
            ++count;
        }
    }
    return count;
}

void PGLogMessageListCtrl::repopulateList()
{
    Freeze();
    DeleteAllItems();

    long displayIndex = 0;
    for (const auto& msg : m_allMessages) {
        // add to ignore map if not present
        if (!m_ignoredItems.contains(msg)) {
            m_ignoredItems[msg] = false;
        }

        const bool messageIgnored = m_ignoredItems.at(msg);

        if (!m_showIgnored && messageIgnored) {
            // message is ignored and we're not showing that - skip
            continue;
        }

        if (messageIgnored) {
            // ignored messages should be a faded black
            const long index = InsertItem(displayIndex, msg);
            SetItemTextColour(index, s_IGNORED_MESSAGE_COLOR);
        } else {
            InsertItem(displayIndex, msg);
        }
        ++displayIndex;
    }

    // Fire event after ignore/unignore action
    wxCommandEvent updateEvt(s_EVT_PG_LOG_IGNORE_CHANGED, GetId());
    updateEvt.SetEventObject(this);
    ProcessWindowEvent(updateEvt);

    Thaw();
}

void PGLogMessageListCtrl::onContextMenu([[maybe_unused]] wxContextMenuEvent& event)
{
    if (!m_allowIgnore) {
        return; // ignoring not allowed
    }

    // Get all selected items
    wxArrayInt selections;
    long item = -1;
    while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
        selections.Add(item);
    }

    if (selections.IsEmpty()) {
        return; // nothing selected
    }

    // Determine current state of selection
    bool allIgnored = true;
    bool allNotIgnored = true;

    for (auto idx : selections) {
        const wxString& msg = GetItemText(idx);
        if (m_ignoredItems.at(msg)) {
            allNotIgnored = false;
        } else {
            allIgnored = false;
        }
    }

    // Build context menu
    wxMenu menu;

    auto* ignoreItem = menu.Append(static_cast<int>(ContextMenu::ID_PG_IGNORE_ITEM), "Ignore");
    ignoreItem->Enable(!allIgnored);

    if (m_showIgnored) {
        auto* unignoreItem = menu.Append(static_cast<int>(ContextMenu::ID_PG_UNIGNORE_ITEM), "Un-Ignore");
        unignoreItem->Enable(!allNotIgnored);
    }

    // Bind actions (lambda captures this and selections)
    menu.Bind(wxEVT_MENU, [this, selections](wxCommandEvent& evt) -> void {
        const int id = evt.GetId();
        switch (id) {
        case static_cast<int>(ContextMenu::ID_PG_IGNORE_ITEM):
            for (auto idx : selections) {
                const wxString& msg = GetItemText(idx);
                m_ignoredItems[msg] = true;
            }
            break;

        case static_cast<int>(ContextMenu::ID_PG_UNIGNORE_ITEM):
            for (auto idx : selections) {
                const wxString& msg = GetItemText(idx);
                m_ignoredItems[msg] = false;
            }
            break;
        }

        repopulateList();
    });

    // Show menu
    PopupMenu(&menu);
}
