#include "ParallaxGenUI.hpp"

#include "GUI/LauncherWindow.hpp"
#include "GUI/ModSortDialog.hpp"
#include "ParallaxGenConfig.hpp"

#include <boost/algorithm/string/join.hpp>
#include <wx/wx.h>

#include <stdexcept>

using namespace std;

// ParallaxGenUI class

void ParallaxGenUI::init()
{
    wxApp::SetInstance(new wxApp()); // NOLINT(cppcoreguidelines-owning-memory)
    if (!wxEntryStart(nullptr, nullptr)) {
        throw runtime_error("Failed to initialize wxWidgets");
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
