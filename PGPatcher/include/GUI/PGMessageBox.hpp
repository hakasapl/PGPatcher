#pragma once

#include <wx/wx.h>

/**
 * @brief Drop-in replacement for wxMessageBox that renders correctly in dark mode
 *
 * The native Windows message box does not support dark mode, so when dark mode is active this shows the wx-drawn
 * generic message dialog instead, which follows the application appearance.
 *
 * @param message Message to show in the dialog
 * @param caption Title of the dialog
 * @param style wxMessageBox style flags (wxOK, wxYES_NO, wxICON_*, etc.)
 * @param parent Parent window (optional)
 * @return int wxOK / wxCANCEL / wxYES / wxNO / wxHELP depending on the button pressed (same as wxMessageBox)
 */
auto PGMessageBox(const wxString& message,
                  const wxString& caption,
                  int style = wxOK | wxCENTRE,
                  wxWindow* parent = nullptr) -> int;
