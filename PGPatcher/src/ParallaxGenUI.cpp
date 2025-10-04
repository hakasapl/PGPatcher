#include "ParallaxGenUI.hpp"

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
