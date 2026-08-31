#include "PGUI.hpp"

#include "GUI/LauncherWindow.hpp"
#include "GUI/ModSortDialog.hpp"
#include "PGConfig.hpp"
#include "PGPatcherGlobals.hpp"

#include <boost/algorithm/string/join.hpp>
#include <wx/settings.h>
#include <wx/wx.h>

#include <windows.h>

#include <cstdlib>
#include <stdexcept>
#include <string>

using namespace std;

// PGUI class

void PGUI::init()
{
    wxApp::SetInstance(new wxApp()); // NOLINT(cppcoreguidelines-owning-memory)
    if (!wxEntryStart(nullptr, nullptr)) {
        throw runtime_error("Failed to initialize wxWidgets");
    }

    applyTheme();
}

auto PGUI::applyTheme() -> bool
{
    string theme = "system";
    if (PGPatcherGlobals::getPGC() != nullptr) {
        theme = PGPatcherGlobals::getPGC()->getUITheme();
    }

    if (theme != "light" && theme != "dark") {
        theme = "system";
    }

    if (theme != s_appliedTheme) {
        wxApp::AppearanceResult result {};
        if (theme == "dark") {
            result = wxTheApp->SetAppearance(wxApp::Appearance::Dark);
        } else if (theme == "light") {
            result = wxTheApp->SetAppearance(wxApp::Appearance::Light);
        } else {
            result = wxTheApp->SetAppearance(wxApp::Appearance::System);
        }

        if (result != wxApp::AppearanceResult::Ok) {
            return false;
        }

        s_appliedTheme = theme;
    }

    PGPatcherGlobals::setIsDarkMode(theme == "dark"
                                    || (theme == "system" && wxSystemSettings::GetAppearance().IsSystemDark()));
    return true;
}

void PGUI::showLauncher(PGConfig& pgc,
                        PGConfig::PGParams& params)
{
    int result = wxID_CANCEL;
    do {
        auto* launcher = new LauncherWindow(pgc); // NOLINT(cppcoreguidelines-owning-memory)
        result = launcher->ShowModal();
        if (result == wxID_OK) {
            launcher->getParams(params);
        }
        launcher->Destroy();

        if (result == LauncherWindow::RESULT_RELAUNCH) {
            // The theme may have changed in settings; the appearance can only change while no
            // top-level windows exist, so flush the just-destroyed launcher first (ProcessIdle
            // deletes the objects pending destruction)
            wxTheApp->ProcessIdle();
            if (!applyTheme()) {
                // wxMSW cannot leave dark mode within the same process, so restart PGPatcher
                // with the same command line to apply the new theme (already saved to config)
                wxExecute(wxString(GetCommandLineW()), wxEXEC_ASYNC);
                exit(0);
            }
        }
    } while (result == LauncherWindow::RESULT_RELAUNCH); // rebuild the launcher after a language/theme change
}

void PGUI::selectModOrder()
{
    ModSortDialog dialog;
    dialog.ShowModal();
}
