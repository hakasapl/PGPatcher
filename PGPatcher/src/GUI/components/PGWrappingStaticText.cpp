#include "GUI/components/PGWrappingStaticText.hpp"

PGWrappingStaticText::PGWrappingStaticText(wxWindow* parent,
                                           wxWindowID id,
                                           const wxString& label,
                                           int initialWrapWidth)
    : wxStaticText(parent,
                   id,
                   label)
    , m_unwrappedLabel(label)
    , m_wrappedWidth(initialWrapWidth)
{
    // Wrap once up front so that the parent has a sensible best size to fit itself to before being shown
    Wrap(initialWrapWidth);

    Bind(wxEVT_SIZE, &PGWrappingStaticText::onSize, this);
}

void PGWrappingStaticText::onSize(wxSizeEvent& event)
{
    event.Skip();

    const int width = event.GetSize().GetWidth();
    if (width > 0 && width != m_wrappedWidth) {
        rewrap(width);
    }
}

void PGWrappingStaticText::rewrap(int width)
{
    if (m_rewrapping) {
        // Wrap() sets the label, which fires another size event - ignore it, the label is already correct
        return;
    }

    m_rewrapping = true;

    m_wrappedWidth = width;
    const int oldHeight = GetSize().GetHeight();

    SetLabel(m_unwrappedLabel);
    Wrap(width);
    InvalidateBestSize();

    m_rewrapping = false;

    if (GetBestSize().GetHeight() != oldHeight) {
        // The label needs a different number of lines than the sizer allocated space for, so lay out again to
        // give it the height it needs
        auto* topLevel = wxGetTopLevelParent(this);
        if (topLevel != nullptr) {
            topLevel->Layout();
        }
    }
}
