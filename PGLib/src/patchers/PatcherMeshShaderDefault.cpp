#include "patchers/PatcherMeshShaderDefault.hpp"

#include <Geometry.hpp>
#include <boost/algorithm/string.hpp>

#include "util/NIFUtil.hpp"

using namespace std;

auto PatcherMeshShaderDefault::getFactory() -> PatcherMeshShader::PatcherMeshShaderFactory
{
    return [](const filesystem::path& nifPath, nifly::NifFile* nif) -> unique_ptr<PatcherMeshShader> {
        return make_unique<PatcherMeshShaderDefault>(nifPath, nif);
    };
}

auto PatcherMeshShaderDefault::getShaderType() -> NIFUtil::ShapeShader { return NIFUtil::ShapeShader::NONE; }

PatcherMeshShaderDefault::PatcherMeshShaderDefault(filesystem::path nifPath, nifly::NifFile* nif)
    : PatcherMeshShader(std::move(nifPath), nif, "Default")
{
}

auto PatcherMeshShaderDefault::canApply([[maybe_unused]] NiShape& nifShape, [[maybe_unused]] bool singlepassMATO)
    -> bool
{
    return true;
}

auto PatcherMeshShaderDefault::shouldApply(nifly::NiShape& nifShape, std::vector<PatcherMatch>& matches) -> bool
{
    return shouldApply(getTextureSet(getNIFPath(), *getNIF(), nifShape), matches);
}

auto PatcherMeshShaderDefault::shouldApply(const NIFUtil::TextureSet& oldSlots, std::vector<PatcherMatch>& matches)
    -> bool
{
    matches.clear();

    // Loop through slots (only diffuse and normal)
    for (size_t slot = 0; slot <= 1; slot++) {
        if (oldSlots.at(slot).empty()) {
            continue;
        }

        // Check if file exists
        if (!getPGD()->isFile(oldSlots.at(slot))) {
            continue;
        }

        // Add match
        PatcherMatch curMatch;
        curMatch.matchedPath = oldSlots.at(slot);
        curMatch.matchedFrom.insert(static_cast<NIFUtil::TextureSlots>(slot));
        matches.push_back(curMatch);
    }

    return !matches.empty();
}

auto PatcherMeshShaderDefault::applyPatch(const NIFUtil::TextureSet& oldSlots,
    [[maybe_unused]] nifly::NiShape& nifShape, [[maybe_unused]] const PatcherMatch& match,
    NIFUtil::TextureSet& newSlots) -> bool
{
    newSlots = oldSlots;
    return false;
}

auto PatcherMeshShaderDefault::applyPatchSlots(const NIFUtil::TextureSet& oldSlots,
    [[maybe_unused]] const PatcherMatch& match, NIFUtil::TextureSet& newSlots) -> bool
{
    newSlots = oldSlots;
    return false;
}

auto PatcherMeshShaderDefault::applyShader([[maybe_unused]] nifly::NiShape& nifShape) -> bool
{
    // Set texture set to itself so that it is preserved as "used"
    setTextureSet(getNIFPath(), *getNIF(), nifShape, NIFUtil::getTextureSlots(getNIF(), &nifShape));

    return false;
}
