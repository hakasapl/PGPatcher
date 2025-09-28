#include "GUI/components/PGCheckedDragListCtrlEvtItemDragged.hpp"

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

wxDEFINE_EVENT(pgEVT_CDLC_ITEM_DRAGGED, PGCheckedDragListCtrlEvtItemDragged);
auto PGCheckedDragListCtrlEvtItemDragged::Clone() const -> wxEvent*
{
    return new PGCheckedDragListCtrlEvtItemDragged(*this);
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
