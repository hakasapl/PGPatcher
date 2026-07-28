#include "PGUI.hpp"

#include "GUI/LauncherWindow.hpp"
#include "GUI/ModSortDialog.hpp"
#include "PGConfig.hpp"
#include "PGPatcherGlobals.hpp"

#include <boost/algorithm/string/join.hpp>
#include <wx/settings.h>
#include <wx/wx.h>

#include <stdexcept>

using namespace std;

// PGUI class

void PGUI::init(bool forceDarkMode,
                bool forceLightMode)
{
    wxApp::SetInstance(new wxApp()); // NOLINT(cppcoreguidelines-owning-memory)
    if (!wxEntryStart(nullptr, nullptr)) {
        throw runtime_error("Failed to initialize wxWidgets");
    }

    if (forceDarkMode && !forceLightMode) {
        wxTheApp->SetAppearance(wxApp::Appearance::Dark);
        PGPatcherGlobals::setIsDarkMode(true);
    } else if (forceLightMode && !forceDarkMode) {
        wxTheApp->SetAppearance(wxApp::Appearance::Light);
        PGPatcherGlobals::setIsDarkMode(false);
    } else {
        wxTheApp->SetAppearance(wxApp::Appearance::System);
        PGPatcherGlobals::setIsDarkMode(wxSystemSettings::GetAppearance().IsSystemDark());
    }
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
    } while (result == LauncherWindow::RESULT_RELAUNCH); // rebuild the launcher after a language change
}

void PGUI::selectModOrder()
{
    ModSortDialog dialog;
    dialog.ShowModal();
}
