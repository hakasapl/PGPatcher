// The runtime implementation of the PGPlugin interface: the uses come from the loaded game (see Uses) instead of the
// Mutagen library. Only the functions the shared code calls exist here; the plugin writing functions of the offline
// implementation have no runtime counterpart, because PGLite edits records in memory.
#include "PCH.h"

#include "PGPlugin.hpp"

#include "pgutil/PGMeshPermutationTracker.hpp"

#include <string>
#include <utility>
#include <vector>

auto PGPlugin::modelUses([[maybe_unused]] const std::wstring& modelPath)
    -> std::vector<std::pair<PGMeshPermutationTracker::FormKey,
                             MeshUseAttributes>>
{
    // The mesh reading pass stores what this returns with every mesh of the directory, and nothing at runtime reads
    // that copy: the plan takes the uses from Uses, which holds them once. Nothing is kept per mesh.
    return { };
}
