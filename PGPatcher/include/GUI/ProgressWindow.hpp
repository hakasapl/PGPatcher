#pragma once

#include <wx/gauge.h>
#include <wx/stattext.h>
#include <wx/wx.h>

#include <string>

class ProgressWindow : public wxDialog {
private:
    wxString m_mainLabelBase;
    wxString m_stepLabelBase;

    wxStaticText* m_mainStatusText;
    wxGauge* m_progressBarMain;

    wxStaticText* m_stepStatusText;
    wxGauge* m_progressBarStep;

public:
    ProgressWindow();

    void setMainProgress(int done, int total, bool addToLabel = false);
    void setMainLabel(const std::string& label);

    void setStepProgress(int done, int total, bool addToLabel = false);
    void setStepLabel(const std::string& label);
};
