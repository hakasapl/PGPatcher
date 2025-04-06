#include "ParallaxGenUI.hpp"

#include <algorithm>
#include <boost/algorithm/string/join.hpp>
#include <wx/app.h>
#include <wx/arrstr.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/msw/statbox.h>
#include <wx/msw/stattext.h>
#include <wx/sizer.h>
#include <wx/toplevel.h>

#include "GUI/LauncherWindow.hpp"
#include "GUI/ModSortDialog.hpp"
#include "ParallaxGenConfig.hpp"
#include "ParallaxGenUtil.hpp"

using namespace std;

// ParallaxGenUI class

void ParallaxGenUI::init()
{
    wxApp::SetInstance(new wxApp()); // NOLINT(cppcoreguidelines-owning-memory)
    if (!wxEntryStart(nullptr, nullptr)) {
        throw runtime_error("Failed to initialize wxWidgets");
    }
}

auto ParallaxGenUI::showLauncher(ParallaxGenConfig& pgc, const std::filesystem::path& cacheDir)
    -> ParallaxGenConfig::PGParams
{
    auto* launcher = new LauncherWindow(pgc, cacheDir); // NOLINT(cppcoreguidelines-owning-memory)
    if (launcher->ShowModal() == wxID_OK) {
        return launcher->getParams();
    }

    return {};
}

auto ParallaxGenUI::selectModOrder(
    const std::unordered_map<std::wstring, tuple<std::set<NIFUtil::ShapeShader>, unordered_set<wstring>>>& conflicts,
    const std::vector<std::wstring>& existingMods) -> std::vector<std::wstring>
{

    std::vector<std::pair<NIFUtil::ShapeShader, std::wstring>> modOrder;
    for (const auto& [mod, value] : conflicts) {
        const auto& shaderSet = std::get<0>(value);
        modOrder.emplace_back(*prev(shaderSet.end()), mod); // Get the first shader
    }

    // Sort by ShapeShader first, and then by key name lexicographically
    std::ranges::sort(modOrder, [](const auto& lhs, const auto& rhs) {
        if (lhs.first == rhs.first) {
            return lhs.second < rhs.second; // Secondary sort by wstring key
        }
        return lhs.first < rhs.first; // Primary sort by ShapeShader
    });

    vector<wstring> finalModOrder;

    // Add new mods
    for (const auto& [shader, mod] : modOrder) {
        if (std::ranges::find(existingMods, mod) == existingMods.end()) {
            // add to new mods
            finalModOrder.push_back(mod);
        }
    }

    // Add existing mods
    for (const auto& mod : existingMods) {
        // check if existing mods contain the mod
        if (conflicts.find(mod) != conflicts.end()) {
            // add to final mod order
            finalModOrder.push_back(mod);
        }
    }

    // split into vectors
    vector<wstring> modStrs;
    vector<wstring> shaderCombinedStrs;
    vector<bool> isNew;
    unordered_map<wstring, unordered_set<wstring>> conflictTracker;

    // first loop through existing order to restore is

    for (const auto& mod : finalModOrder) {
        if (mod.empty()) {
            // skip unmanaged stuff
            continue;
        }

        modStrs.push_back(mod);

        vector<wstring> shaderStrs;
        for (const auto& shader : get<0>(conflicts.at(mod))) {
            if (shader == NIFUtil::ShapeShader::NONE) {
                // don't print none type
                continue;
            }

            shaderStrs.insert(shaderStrs.begin(), ParallaxGenUtil::utf8toUTF16(NIFUtil::getStrFromShader(shader)));
        }

        auto shaderStr = boost::join(shaderStrs, L",");
        shaderCombinedStrs.push_back(shaderStr);

        // check if mod is in existing order
        isNew.push_back(std::ranges::find(existingMods, mod) == existingMods.end());

        // add to conflict tracker
        conflictTracker.insert({ mod, get<1>(conflicts.at(mod)) });
    }

    ModSortDialog dialog(modStrs, shaderCombinedStrs, isNew, conflictTracker);
    if (dialog.ShowModal() == wxID_OK) {
        return dialog.getSortedItems();
    }

    return modStrs; // Return the original order if cancelled or closed
}
