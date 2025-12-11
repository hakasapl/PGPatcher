#pragma once

#include <wx/listbase.h>
#include <wx/listctrl.h>
#include <wx/msw/colour.h>
#include <wx/wx.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

wxDECLARE_EVENT(s_EVT_PG_LOG_IGNORE_CHANGED, wxCommandEvent);

class PGLogMessageListCtrl : public wxListCtrl {
private:
    bool m_allowIgnore;
    bool m_showIgnored;
    std::vector<wxString> m_allMessages;
    std::unordered_map<wxString, bool> m_ignoredItems;

    static inline const wxColour s_IGNORED_MESSAGE_COLOR = wxColour(50, 50, 50);

    enum class ContextMenu : uint16_t { ID_PG_IGNORE_ITEM = wxID_HIGHEST + 3, ID_PG_UNIGNORE_ITEM };

public:
    /**
     * @brief Construct a new PGLogMessageListCtrl object
     *
     * @param parent parent window
     * @param id window ID
     * @param pt position
     * @param sz size
     * @param style window style (default wxLC_REPORT)
     */
    PGLogMessageListCtrl(wxWindow* parent, wxWindowID id, bool allowIgnore = true);

    void setShowIgnored(bool showIgnored);
    void setLogMessages(const std::vector<wxString>& messages);
    void setIgnoreMap(const std::unordered_map<wxString, bool>& ignoredItems);
    [[nodiscard]] auto getIgnoreMap() const -> const std::unordered_map<wxString, bool>&;
    [[nodiscard]] auto getNumUnignoredMessages() const -> size_t;

private:
    void repopulateList();

    /**
     * @brief Event handler for context menu
     *
     * @param event wxWidgets event object
     */
    void onContextMenu(wxContextMenuEvent& event);
};
