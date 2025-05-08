#include "ParallaxGenUI.hpp"

#include <boost/algorithm/string/join.hpp>
#include <memory>
#include <wx/app.h>
#include <wx/arrstr.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/msw/statbox.h>
#include <wx/msw/stattext.h>
#include <wx/sizer.h>
#include <wx/toplevel.h>

#include "GUI/LauncherWindow.hpp"
#include "GUI/ModConflictDialog.hpp"
#include "PGGlobals.hpp"
#include "ParallaxGenConfig.hpp"

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

    const auto conflicts = mmd->getConflicts();
    unordered_map<unordered_set<wstring>, wstring, ModConflictDialog::WStringSetHash> conflictsWstr;
    for (const auto& [modSet, winningMod] : conflicts) {
        unordered_set<wstring> modSetWstr;
        for (const auto& mod : modSet) {
            modSetWstr.insert(mod->name);
        }
        conflictsWstr[modSetWstr] = winningMod->name;
    }

    ModConflictDialog dialog(conflictsWstr);
    dialog.ShowModal();
    const auto resolvedConflictsWstr = dialog.getResolvedConflicts();

    std::unordered_map<std::unordered_set<std::shared_ptr<ModManagerDirectory::Mod>, ModManagerDirectory::Mod::ModHash>,
        std::shared_ptr<ModManagerDirectory::Mod>, ModManagerDirectory::ModSetHash, ModManagerDirectory::ModSetEqual>
        resolvedConflicts;

    for (const auto& [modSetWstr, winningModWstr] : resolvedConflictsWstr) {
        unordered_set<shared_ptr<ModManagerDirectory::Mod>, ModManagerDirectory::Mod::ModHash> modSet;
        for (const auto& modWstr : modSetWstr) {
            const auto mod = mmd->getMod(modWstr);
            if (mod == nullptr) {
                throw runtime_error("Unable to resolve mod from resolved conflicts");
            }

            modSet.insert(mod);
        }

        const auto winningMod = mmd->getMod(winningModWstr);
        if (winningMod == nullptr) {
            throw runtime_error("Unable to resolve winning mod from resolved conflicts");
        }

        resolvedConflicts[modSet] = winningMod;
    }

    // Set the winning mod for each conflict set
    mmd->setConflicts(resolvedConflicts);
}
