#pragma once

#include <wx/event.h>

class PGCheckedDragListCtrlEvtMeshesIgnoredChanged;
wxDECLARE_EVENT(pgEVT_CDLC_MESHES_IGNORED_CHANGED, // NOLINT(readability-identifier-naming)
                PGCheckedDragListCtrlEvtMeshesIgnoredChanged);

/**
 * @brief Custom wxCommandEvent fired when one or more items change mesh-ignore state in PGCheckedDragListCtrl.
 */
class PGCheckedDragListCtrlEvtMeshesIgnoredChanged : public wxCommandEvent {
public:
    /**
     * @brief Construct a new PGCheckedDragListCtrlEvtMeshesIgnoredChanged event.
     *
     * @param id Window or event ID.
     */
    explicit PGCheckedDragListCtrlEvtMeshesIgnoredChanged(long id = wxID_ANY)
        : wxCommandEvent(pgEVT_CDLC_MESHES_IGNORED_CHANGED,
                         id)
    {
    }

    /**
     * @brief Create a heap-allocated copy of this event (required for wxPostEvent).
     *
     * @return Pointer to a cloned copy of this event.
     */
    [[nodiscard]] auto Clone() const -> wxEvent* override;
};
