// The runtime implementation of the PGPlugin interface: the uses come from the loaded game (see Uses) instead of the
// Mutagen library. Only the functions the shared code calls exist here; the plugin writing functions of the offline
// implementation have no runtime counterpart, because PGLite edits records in memory.
#include "PCH.h"

#include "PGPlugin.hpp"

#include "Runtime.hpp"
#include "Uses.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "util/StringUtil.hpp"

#include <string>
#include <utility>
#include <vector>

auto PGPlugin::modelUses(const std::wstring& modelPath) -> std::vector<std::pair<PGMeshPermutationTracker::FormKey,
                                                                                 MeshUseAttributes>>
{
    return PGLite::Runtime::instance().uses().usesOf(StringUtil::toLowerASCIIFast(modelPath));
}
