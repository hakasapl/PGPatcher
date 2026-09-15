#include "PGUI.hpp"

#include "GUI/LauncherWindow.hpp"
#include "GUI/ModSortDialog.hpp"
#include "PGConfig.hpp"
#include "PGPatcherGlobals.hpp"

#include <boost/algorithm/string/join.hpp>
#include <wx/iconbndl.h>
#include <wx/settings.h>
#include <wx/wx.h>

#include <windows.h>

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

// PGUI class.

void PGUI::init()
{
    wxApp::SetInstance(new wxApp()); // NOLINT(cppcoreguidelines-owning-memory)
    if (!wxEntryStart(nullptr, nullptr))
        throw std::runtime_error("Failed to initialize wxWidgets");

    applyTheme();
}

bool PGUI::applyTheme()
{
    std::string theme = "system";
    if (PGPatcherGlobals::pgc())
        theme = PGPatcherGlobals::pgc()->uiTheme();

    if (theme != "light" && theme != "dark")
        theme = "system";

    if (theme != s_appliedTheme) {
        wxApp::AppearanceResult result { };
        if (theme == "dark")
            result = wxTheApp->SetAppearance(wxApp::Appearance::Dark);
        else if (theme == "light")
            result = wxTheApp->SetAppearance(wxApp::Appearance::Light);
        else
            result = wxTheApp->SetAppearance(wxApp::Appearance::System);

        if (result != wxApp::AppearanceResult::Ok)
            return false;

        s_appliedTheme = theme;
    }

    PGPatcherGlobals::setIsDarkMode(theme == "dark"
                                    || (theme == "system" && wxSystemSettings::GetAppearance().IsSystemDark()));
    return true;
}

bool PGUI::showLauncher(PGConfig& pgc,
                        PGConfig::PGParams& params)
{
    bool updateRequested = false;
    int result = wxID_CANCEL;
    // Unsaved UI state carried over to the rebuilt launcher after a language or theme change.
    std::optional<PGConfig::PGParams> unsavedParams;
    do {
        auto* launcher = new LauncherWindow(pgc, unsavedParams); // NOLINT(cppcoreguidelines-owning-memory)
        result = launcher->ShowModal();
        if (result == wxID_OK) {
            launcher->getParams(params);
            updateRequested = launcher->isUpdateRequested();
        } else if (result == LauncherWindow::resultRelaunch) {
            PGConfig::PGParams curParams = pgc.params();
            launcher->getParams(curParams);
            unsavedParams = curParams;
        }
        launcher->Destroy();

        if (result == LauncherWindow::resultRelaunch) {
            // The theme may have changed in settings; the appearance can only change while no
            // top-level windows exist, so flush the just-destroyed launcher first (ProcessIdle
            // deletes the objects pending destruction).
            wxTheApp->ProcessIdle();
            if (!applyTheme()) {
                // Restart PGPatcher with the same command line to apply the new theme (already saved to
                // config), because wxMSW cannot leave dark mode within the same process.
                wxExecute(wxString(GetCommandLineW()), wxEXEC_ASYNC);
                exit(0);
            }
        }
    } while (result == LauncherWindow::resultRelaunch); // rebuild the launcher after a language/theme change

    return updateRequested;
}

void PGUI::selectModOrder()
{
    ModSortDialog dialog;
    dialog.ShowModal();
}

wxIconBundle PGUI::appIcons()
{
    // Loaded from the icon resource of the executable (see resources/icon.rc), which holds every icon size.
    return { "IDI_ICON1", nullptr };
}
