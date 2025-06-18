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
#include "PGGlobals.hpp"
#include "ParallaxGenConfig.hpp"
#include "util/ParallaxGenUtil.hpp"

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

void ParallaxGenUI::selectModOrder()
{
    auto* const mmd = PGGlobals::getMMD();

    // get allmods and sort by priority
    auto allMods = mmd->getMods();
    std::ranges::sort(allMods, [](const auto& lhs, const auto& rhs) {
        // If `isNew` differs, put the one with `isNew == true` first.
        if (lhs->isNew != rhs->isNew) {
            return lhs->isNew > rhs->isNew;
        }

        // Otherwise, sort by `priority`.
        return lhs->priority < rhs->priority;
    });

    // data we need for dialog
    vector<wstring> modStrs;
    vector<wstring> shaderCombinedStrs;
    vector<bool> isNew;
    unordered_map<wstring, unordered_set<wstring>> conflictTracker;

    // loop through each mod
    for (const auto& mod : allMods) {
        if (mod->conflicts.empty()) {
            // skip mods with no conflicts
            continue;
        }

        modStrs.push_back(mod->name);

        wstring shaderStr;
        for (const auto& shader : mod->shaders) {
            if (shader == NIFUtil::ShapeShader::NONE) {
                // don't print none type
                continue;
            }

            shaderStr += ParallaxGenUtil::utf8toUTF16(NIFUtil::getStrFromShader(shader)) + L",";
        }
        if (!shaderStr.empty()) {
            shaderStr.pop_back(); // remove last comma
        }
        shaderCombinedStrs.push_back(shaderStr);

        isNew.push_back(mod->isNew);

        for (const auto& conflict : mod->conflicts) {
            if (conflict->name.empty()) {
                continue; // skip empty mod
            }

            conflictTracker[mod->name].insert(conflict->name);
        }
    }

    if (modStrs.empty()) {
        // no mods with conflicts, so no need to show dialog
        return;
    }

    vector<wstring> sortedOrder;
    ModSortDialog dialog(modStrs, shaderCombinedStrs, isNew, conflictTracker);
    if (dialog.ShowModal() == wxID_OK) {
        sortedOrder = dialog.getSortedItems();
    }

    // set priorities
    int priority = 0;
    for (const auto& modName : sortedOrder) {
        auto mod = mmd->getMod(modName);
        if (mod) {
            mod->priority = priority++;
        }
    }
}
