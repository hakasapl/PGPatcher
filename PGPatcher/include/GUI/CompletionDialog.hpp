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
    /**
     * @brief Configure a collapsible pane to host a log-message list control.
     *
     * Sets the minimum/maximum height of the list control, optionally adds a "Show
     * Ignored Warnings" checkbox above it, and wires up expand/collapse events so
     * the dialog resizes accordingly.
     *
     * @param pane          The wxCollapsiblePane that will contain the list control.
     * @param listCtrl      The PGLogMessageListCtrl to embed inside the pane.
     * @param ignoreCheckbox If true, a checkbox to show ignored warnings is added above the list.
     */
    void setupLogMessagePane(wxCollapsiblePane* pane,
                             PGLogMessageListCtrl* listCtrl,
                             bool ignoreCheckbox = true);

    /**
     * @brief Event handler that triggers when the open file location button is pressed
     *
     * @param event wxWidgets event object
     */
    void onOpenOutputLocation(wxCommandEvent& event);

    /**
     * @brief Event handler for the "Open Log File" button.
     *
     * Saves any currently ignored messages to the configuration, then opens the
     * PGPatcher log file using the system's default application and closes the dialog.
     *
     * @param event wxWidgets command event object (unused).
     */
    void onOpenLogFile(wxCommandEvent& event);

    /**
     * @brief Persist the current set of ignored warning messages to the configuration.
     *
     * Reads the ignore map from the warnings list control and forwards it to
     * PGConfig::saveIgnoredMessagesConfig().
     */
    void saveIgnoredMessagesToConfig();
};
