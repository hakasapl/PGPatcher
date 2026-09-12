#pragma once

#include <wx/animate.h>
#include <wx/wx.h>

/**
 * @brief wxAnimationCtrl that keeps the same on-screen size at every DPI scale factor
 *
 * wxAnimationCtrl draws the frames of the animation pixel for pixel, so on a monitor with a scale factor above 100%
 * the animation shrinks relative to the rest of the UI. This control treats the pixel size of the animation as its
 * size in DIPs (the animation is drawn for 100% scaling) and paints every frame scaled to the window's DPI.
 */
class PGAnimationCtrl : public wxAnimationCtrl {
public:
    /**
     * @brief Construct a new PGAnimationCtrl object
     *
     * @param parent parent window
     * @param id window ID
     * @param anim animation to show
     */
    PGAnimationCtrl(wxWindow* parent,
                    wxWindowID id,
                    const wxAnimation& anim);

protected:
    /**
     * @brief Best size of the control: the size of the animation scaled to the window's DPI
     *
     * @return wxSize best size in pixels
     */
    [[nodiscard]] auto DoGetBestSize() const -> wxSize override;

private:
    /**
     * @brief Paint event handler that draws the current frame scaled to the window's DPI
     *
     * @param event wxWidgets event object
     */
    void onPaint(wxPaintEvent& event);
};
