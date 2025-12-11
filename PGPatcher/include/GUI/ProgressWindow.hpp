#pragma once

#include <wx/gauge.h>
#include <wx/wx.h>

class ProgressWindow : public wxDialog {
private:
    wxGauge* m_progressBarMain;
    wxGauge* m_progressBarStep;

public:
    ProgressWindow();
};
