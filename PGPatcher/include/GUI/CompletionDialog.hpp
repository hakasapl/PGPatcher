#pragma once

#include <wx/wx.h>

/**
 * @brief wxDialog that displays the completion status of an operation and shows timing information.
 */
class CompletionDialog : public wxDialog {
private:
    constexpr static int MIN_WIDTH = 400;

public:
    /**
     * @brief Construct a new Mod Sort Dialog object
     */
    CompletionDialog(const long long& timeTaken);

private:
    /**
     * @brief Event handler that triggers when the open file location button is pressed
     *
     * @param event wxWidgets event object
     */
    void onOpenOutputLocation(wxCommandEvent& event);
};
