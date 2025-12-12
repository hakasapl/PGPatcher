#pragma once

#include "GUI/components/PGLogMessageListCtrl.hpp"

#include <wx/collpane.h>
#include <wx/wx.h>

/**
 * @brief wxDialog that displays the completion status of an operation and shows timing information.
 */
class CompletionDialog : public wxDialog {
private:
    constexpr static int MIN_WIDTH = 400;

    PGLogMessageListCtrl* m_warnListCtrl;
    PGLogMessageListCtrl* m_errListCtrl;

public:
    /**
     * @brief Construct a new Mod Sort Dialog object
     */
    CompletionDialog(const long long& timeTaken);

private:
    void setupLogMessagePane(wxCollapsiblePane* pane, PGLogMessageListCtrl* listCtrl, bool ignoreCheckbox = true);

    /**
     * @brief Event handler that triggers when the open file location button is pressed
     *
     * @param event wxWidgets event object
     */
    void onOpenOutputLocation(wxCommandEvent& event);

    void onOpenLogFile(wxCommandEvent& event);

    void saveIgnoredMessagesToConfig();
};
