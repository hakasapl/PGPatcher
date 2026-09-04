#pragma once

#include <wx/wx.h>

/**
 * @brief wxStaticText that re-wraps its label to whatever width its sizer gives it
 *
 * wxStaticText::Wrap() hard wraps a label at a fixed pixel width, so a label that fits in one language
 * gets clipped in another once the translation needs more lines. This control keeps the unwrapped label
 * around, re-wraps it every time its width changes, and asks the containing top level window to lay out
 * again so the label always gets the height it actually needs.
 *
 * Add it to a sizer with wxEXPAND so that it receives the full available width.
 */
class PGWrappingStaticText : public wxStaticText {
public:
    /**
     * @brief Construct a new PGWrappingStaticText object
     *
     * @param parent parent window
     * @param id window ID
     * @param label text to display, wrapped to the width the control is given
     * @param initialWrapWidth width in pixels used for the initial wrap, which determines the best size the
     *                         parent dialog is fitted to before it is ever shown
     */
    PGWrappingStaticText(wxWindow* parent,
                         wxWindowID id,
                         const wxString& label,
                         int initialWrapWidth);

private:
    wxString m_unwrappedLabel; /** Label as given by the caller, before any wrapping is applied */
    int m_wrappedWidth; /** Width the label is currently wrapped to, avoids re-wrapping on every size event */
    bool m_rewrapping {false}; /** Guards against re-entering rewrap() from the size events it causes */

    /**
     * @brief Event handler that re-wraps the label whenever the control's width changes
     *
     * @param event wxWidgets event object
     */
    void onSize(wxSizeEvent& event);

    /**
     * @brief Re-wraps the unwrapped label to the given width and relayouts the window if the height changed
     *
     * @param width width in pixels to wrap to
     */
    void rewrap(int width);
};
