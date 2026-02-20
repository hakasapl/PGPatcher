#pragma once

#include <wx/gauge.h>
#include <wx/stattext.h>
#include <wx/wx.h>

#include <string>

/**
 * @brief wxDialog that shows main and step progress bars during the patching operation.
 *
 * Displays an animated GIF alongside two labelled progress gauges (overall and per-step)
 * and a button that allows the user to abort the generation process.
 */
class ProgressWindow : public wxDialog {
private:
    wxString m_mainLabelBase;
    wxString m_stepLabelBase;

    wxStaticText* m_mainStatusText;
    wxGauge* m_progressBarMain;

    wxStaticText* m_stepStatusText;
    wxGauge* m_progressBarStep;

public:
    /**
     * @brief Construct the ProgressWindow dialog.
     *
     * Creates the dialog with an animated GIF, two progress gauges, and a stop button.
     */
    ProgressWindow();

    /**
     * @brief Update the main (overall) progress bar.
     *
     * @param done      Number of completed items.
     * @param total     Total number of items.
     * @param addToLabel If true, appends "done / total [ pct% ]" to the current main label.
     */
    void setMainProgress(int done,
                         int total,
                         bool addToLabel = false);

    /**
     * @brief Set the text label shown above the main progress bar.
     *
     * @param label New label text to display.
     */
    void setMainLabel(const std::string& label);

    /**
     * @brief Update the step (per-operation) progress bar.
     *
     * @param done      Number of completed items in the current step.
     * @param total     Total number of items in the current step.
     * @param addToLabel If true, appends "done / total [ pct% ]" to the current step label.
     */
    void setStepProgress(int done,
                         int total,
                         bool addToLabel = false);

    /**
     * @brief Set the text label shown above the step progress bar.
     *
     * @param label New label text to display.
     */
    void setStepLabel(const std::string& label);
};
