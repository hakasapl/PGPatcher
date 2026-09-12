#include "GUI/components/PGAnimationCtrl.hpp"

#include <wx/dcclient.h>
#include <wx/graphics.h>

#include <memory>

// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(readability-convert-member-functions-to-static)

PGAnimationCtrl::PGAnimationCtrl(wxWindow* parent,
                                 wxWindowID id,
                                 const wxAnimation& anim)
    : wxAnimationCtrl(parent,
                      id,
                      anim)
{
    // Dynamically bound handlers run before the static event table of wxGenericAnimationCtrl and the event is not
    // skipped, so this replaces the base class paint handler, which would draw the frame unscaled
    Bind(wxEVT_PAINT, &PGAnimationCtrl::onPaint, this);
}

auto PGAnimationCtrl::DoGetBestSize() const -> wxSize
{
    const wxAnimation anim = GetAnimation();
    if (anim.IsOk() && !HasFlag(wxAC_NO_AUTORESIZE)) {
        // The animation is drawn for 100% scaling, so its pixel size is its size in DIPs
        return FromDIP(anim.GetSize());
    }

    return wxAnimationCtrl::DoGetBestSize();
}

void PGAnimationCtrl::onPaint([[maybe_unused]] wxPaintEvent& event)
{
    // The paint DC must be created in any case, even if there is nothing to draw
    wxPaintDC dc(this);

    if (m_backingStore.IsOk()) {
        // The backing store always holds the current frame at the animation's own size
        const wxSize drawSize = FromDIP(m_backingStore.GetSize());
        const std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (gc != nullptr) {
            gc->SetInterpolationQuality(wxINTERPOLATION_BEST);
            gc->DrawBitmap(m_backingStore, 0, 0, drawSize.GetWidth(), drawSize.GetHeight());
        } else {
            dc.SetUserScale(GetDPIScaleFactor(), GetDPIScaleFactor());
            dc.DrawBitmap(m_backingStore, 0, 0, false);
        }
    } else {
        // No valid animation, so no backing store: clear to the background colour
        DisposeToBackground(dc);
    }

    // wxGenericAnimationCtrl schedules the next frame once the current one has been painted, do the same here
    if (IsPlaying() && !m_timer.IsRunning()) {
        int delay = GetAnimation().GetDelay(m_currentFrame);
        if (delay <= 0) {
            delay = 1; // 0 is not a valid timeout for wxTimer
        }
        m_timer.StartOnce(delay);
    }
}

// NOLINTEND(readability-convert-member-functions-to-static)
