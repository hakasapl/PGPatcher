#include "GUI/components/PGCheckedDragListCtrlEvtMeshesIgnoredChanged.hpp"

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

wxDEFINE_EVENT(pgEVT_CDLC_MESHES_IGNORED_CHANGED,
               PGCheckedDragListCtrlEvtMeshesIgnoredChanged);
auto PGCheckedDragListCtrlEvtMeshesIgnoredChanged::Clone() const -> wxEvent*
{
    return new PGCheckedDragListCtrlEvtMeshesIgnoredChanged(*this);
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
