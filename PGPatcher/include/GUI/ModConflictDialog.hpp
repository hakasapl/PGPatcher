#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include <wx/overlay.h>
#include <wx/sizer.h>
#include <wx/wx.h>

/**
 * @brief wxDialog that allows the user to sort the mods in the order they want
 */
class ModConflictDialog : public wxDialog {
public:
    struct WStringSetHash {
        auto operator()(const std::unordered_set<std::wstring>& s) const -> std::size_t
        {
            std::size_t h = 0;
            for (const auto& str : s) {
                h ^= std::hash<std::wstring> {}(str) + 0x9e3779b9 // NOLINT(cppcoreguidelines-avoid-magic-numbers)
                    + (h << 6) + (h >> 2); // NOLINT(cppcoreguidelines-avoid-magic-numbers)
            }
            return h;
        }
    };

private:
    constexpr static int DEFAULT_BORDER = 4;
    constexpr static int DEFAULT_WIDTH = 300;
    constexpr static int DEFAULT_HEIGHT = 600;
    constexpr static int SCROLL_SIZE = 5;

    std::unordered_map<std::unordered_set<std::wstring>, std::vector<wxRadioButton*>, WStringSetHash> m_rbTracker;

public:
    ModConflictDialog(
        const std::unordered_map<std::unordered_set<std::wstring>, std::wstring, WStringSetHash>& conflicts,
        const std::unordered_map<std::wstring, std::string>& shaderTypes);

    [[nodiscard]] auto getResolvedConflicts() const
        -> std::unordered_map<std::unordered_set<std::wstring>, std::wstring, WStringSetHash>;

private:
    /**
     * @brief Resets indices for the list after drag or sort
     *
     * @param event wxWidgets event object
     */
    void onClose(wxCloseEvent& event);
};
