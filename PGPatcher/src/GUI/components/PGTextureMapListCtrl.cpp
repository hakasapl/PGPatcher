#include "GUI/components/PGTextureMapListCtrl.hpp"

#include "GUI/components/PGCustomListctrlChangedEvent.hpp"
#include "GUI/components/PGModifiableListCtrl.hpp"
#include "util/NIFUtil.hpp"

using namespace std;

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

PGTextureMapListCtrl::PGTextureMapListCtrl(
    wxWindow* parent, wxWindowID id, const wxPoint& pt, const wxSize& sz, long style)
    : PGModifiableListCtrl(parent, id, pt, sz, style)
    , m_textureMapTypeCombo(nullptr)
{
    // Bind events
    Bind(wxEVT_LEFT_DCLICK, &PGTextureMapListCtrl::onTextureRulesMapsChangeStart, this);
}

void PGTextureMapListCtrl::onTextureRulesMapsChangeStart(wxMouseEvent& event)
{
    static const auto possibleTexTypes = NIFUtil::getTexTypesStr();
    // convert to wxArrayStr
    wxArrayString wxPossibleTexTypes;
    for (const auto& texType : possibleTexTypes) {
        wxPossibleTexTypes.Add(texType);
    }

    const wxPoint pos = event.GetPosition();
    int flags = 0;
    const long item = HitTest(pos, flags);

    if (item != wxNOT_FOUND && ((flags & wxLIST_HITTEST_ONITEM) != 0)) {
        const int column = getColumnAtPosition(pos, item);
        if (column == 0) {
            // Start editing the first column
            EditLabel(item);
        } else if (column == 1) {
            // Create dropdown for the second column
            wxRect rect;
            GetSubItemRect(item, column, rect);

            m_textureMapTypeCombo = new wxComboBox(this, wxID_ANY, "", rect.GetTopLeft(), rect.GetSize(),
                wxPossibleTexTypes, wxCB_DROPDOWN | wxCB_READONLY);
            m_textureMapTypeCombo->SetFocus();
            m_textureMapTypeCombo->Popup();
            m_textureMapTypeCombo->Bind(wxEVT_COMBOBOX, [item, column, this](wxCommandEvent&) {
                SetItem(item, column, m_textureMapTypeCombo->GetValue());
                m_textureMapTypeCombo->Show(false);

                // Check if it's the last row and add a new blank row
                if (item == GetItemCount() - 1) {
                    InsertItem(GetItemCount(), "");
                }

                // Fire event for list change
                PGCustomListctrlChangedEvent changeEvt(GetId(), item);
                changeEvt.SetEventObject(this);
                wxPostEvent(this, changeEvt);
            });

            m_textureMapTypeCombo->Bind(
                wxEVT_KILL_FOCUS, [this](wxFocusEvent&) { m_textureMapTypeCombo->Show(false); });
        }
    } else {
        event.Skip();
    }
}

auto PGTextureMapListCtrl::getColumnAtPosition(const wxPoint& pos, long item) -> int
{
    wxRect rect;
    for (int col = 0; col < GetColumnCount(); ++col) {
        GetSubItemRect(item, col, rect);
        if (rect.Contains(pos)) {
            return col;
        }
    }
    return -1; // No column found
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
