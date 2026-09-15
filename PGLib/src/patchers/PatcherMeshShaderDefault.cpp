#include "patchers/PatcherMeshShaderDefault.hpp"

#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include <boost/algorithm/string.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

auto PatcherMeshShaderDefault::factory() -> PatcherMeshShader::PatcherMeshShaderFactory
{
    return [](const std::filesystem::path& nifPath, nifly::NifFile* nif) -> std::unique_ptr<PatcherMeshShader> {
        return std::make_unique<PatcherMeshShaderDefault>(nifPath, nif);
    };
}

PGEnums::ShapeShader PatcherMeshShaderDefault::shaderType() { return PGEnums::ShapeShader::None; }

PatcherMeshShaderDefault::PatcherMeshShaderDefault(std::filesystem::path nifPath,
                                                   nifly::NifFile* nif)
    : PatcherMeshShader(std::move(nifPath),
                        nif,
                        "Default")
{
}

bool PatcherMeshShaderDefault::canApply([[maybe_unused]] nifly::NiShape& nifShape,
                                        [[maybe_unused]] bool isSinglepassMATO,
                                        [[maybe_unused]] const PGPlugin::ModelRecordType& modelRecordType)
{
    return true;
}

bool PatcherMeshShaderDefault::shouldApply(nifly::NiShape& nifShape,
                                           std::vector<PatcherMatch>& matches)
{
    return shouldApply(textureSet(nifPath(), *nif(), nifShape), matches);
}

bool PatcherMeshShaderDefault::shouldApply(const PGTypes::TextureSet& oldSlots,
                                           std::vector<PatcherMatch>& matches)
{
    auto* pgd = PGGlobals::pgd();

    matches.clear();

    // Loop through slots (only diffuse and normal).
    for (size_t slot = 0; slot <= 1; slot++) {
        if (oldSlots.at(slot).empty())
            continue;

        // Check if file exists.
        if (!pgd->isFile(oldSlots.at(slot)))
            continue;

        // Add match.
        PatcherMatch curMatch;
        curMatch.matchedPath = oldSlots.at(slot);
        matches.push_back(curMatch);
    }

    return !matches.empty();
}

void PatcherMeshShaderDefault::applyPatch(PGTypes::TextureSet& slots,
                                          [[maybe_unused]] nifly::NiShape& nifShape,
                                          [[maybe_unused]] const PatcherMatch& match)
{
}

void PatcherMeshShaderDefault::applyPatchSlots([[maybe_unused]] PGTypes::TextureSet& slots,
                                               [[maybe_unused]] const PatcherMatch& match)
{
}

void PatcherMeshShaderDefault::applyShader([[maybe_unused]] nifly::NiShape& nifShape) { }
