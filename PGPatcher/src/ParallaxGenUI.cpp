#include "ParallaxGenUI.hpp"

#include "GUI/LauncherWindow.hpp"
#include "GUI/ModSortDialog.hpp"
#include "PGPatcherGlobals.hpp"
#include "ParallaxGenConfig.hpp"

#include <boost/algorithm/string/join.hpp>
#include <wx/settings.h>
#include <wx/wx.h>

#include <stdexcept>

using namespace std;

// ParallaxGenUI class

void ParallaxGenUI::init(bool forceDarkMode, bool forceLightMode)
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
    }
}

void ParallaxGenUI::showLauncher(ParallaxGenConfig& pgc, ParallaxGenConfig::PGParams& params)
{
    auto* launcher = new LauncherWindow(pgc); // NOLINT(cppcoreguidelines-owning-memory)
    if (launcher->ShowModal() == wxID_OK) {
        launcher->getParams(params);
    }
}

void ParallaxGenUI::selectModOrder()
{
    ModSortDialog dialog;
    dialog.ShowModal();
}
