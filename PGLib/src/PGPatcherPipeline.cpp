// The per-mesh patching pipeline of PGPatcher: the functions that patch one staged mesh for one plugin use. Shared with
// the PGLite runtime, which drives them from its own file loading; the offline runners live in PGPatcher.cpp.
#include "PGPatcher.hpp"

#include "PGGlobals.hpp"
#include "PGModManager.hpp"
#include "PGPlugin.hpp"
#include "PGRunObserver.hpp"
#include "patchers/PatcherMeshPreStockMarker.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "patchers/base/PatcherUtil.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/HashUtil.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Statics.
PatcherUtil::PatcherMeshSet PGPatcher::s_meshPatchers;
PatcherUtil::PatcherTextureSet PGPatcher::s_texPatchers;

void PGPatcher::loadPatchers(const PatcherUtil::PatcherMeshSet& meshPatchers,
                             const PatcherUtil::PatcherTextureSet& texPatchers)
{
    s_meshPatchers = meshPatchers;
    s_texPatchers = texPatchers;
}

bool PGPatcher::isUseUnpatched(const PGPlugin::MeshUseAttributes& use,
                               const std::unordered_set<PGPlugin::ModelRecordType>& allowedModelRecTypes,
                               bool shouldCheckAllowedRecTypes)
{
    return use.isIgnored
        || (shouldCheckAllowedRecTypes && !use.isDummyUse && !allowedModelRecTypes.contains(use.recType));
}

void PGPatcher::sortMatches(std::vector<PatcherUtil::ShaderPatcherMatch>& matches)
{
    std::ranges::sort(matches, [&](const PatcherUtil::ShaderPatcherMatch& a, const PatcherUtil::ShaderPatcherMatch& b) {
        if (a.mod && !b.mod)
            return true;

        if (!a.mod && b.mod)
            return false;

        if ((a.mod && b.mod) && (a.mod->priority != b.mod->priority))
            return a.mod->priority > b.mod->priority;

        const auto aShader = a.shaderTransformTo != PGEnums::ShapeShader::Unknown ? a.shaderTransformTo : a.shader;
        const auto bShader = b.shaderTransformTo != PGEnums::ShapeShader::Unknown ? b.shaderTransformTo : b.shader;
        if (aShader != bShader)
            return static_cast<int>(aShader) > static_cast<int>(bShader);

        return a.match.matchedPath < b.match.matchedPath;
    });
}

bool PGPatcher::processNIF(const std::filesystem::path& nifPath,
                           nifly::NifFile* nif,
                           MeshMeta& meshMeta,
                           const PGPlugin::MeshUseAttributes& meshUse,
                           bool isSinglepassMATO,
                           const PGMeshPermutationTracker::FormKey& formKey,
                           const PGPlugin::ModelRecordType& modelRecordType,
                           std::unordered_map<unsigned,
                                              PGTypes::TextureSet>& alternateTextures,
                           std::unordered_set<unsigned>& nonAltTexShapes)
{
    // Create patcher objects.
    const auto patcherObjects = createNIFPatcherObjects(nifPath, nif, meshUse);

    // Get shapes and index 3ds (this is in the order as they would show up as 3d indices in plugins).
    const auto shapes = PGNIFUtil::shapesWith3DIdx(nif);

    // Patching a shape can delete blocks, such as its alpha property, which renumbers the blocks behind them, so the
    // block IDs of the shapes are taken from the NIF as it was loaded.
    std::vector<uint32_t> shapeBlockIDs;
    shapeBlockIDs.reserve(shapes.size());
    for (const auto& shape : shapes)
        shapeBlockIDs.push_back(nif->GetBlockID(shape.first));

    meshMeta.formKeys.push_back(formKey);

    size_t shapeIdx = 0;
    size_t shapeMetaIdx = 0;
    for (const auto& [nifShape, oldIndex3D] : shapes) {
        const auto shapeBlockID = shapeBlockIDs[shapeIdx++];
        const auto shapeName = nifShape->name.get();
        const Logger::Prefix shapePrefix(std::to_string(shapeBlockID) + "/" + shapeName + "/"
                                         + std::to_string(oldIndex3D));

        if (!nifShape) {
            // Skip if shape is null (invalid shapes).
            Logger::trace(L"Skipping: Shape is null");
            continue;
        }

        if (!PGNIFUtil::isPatchableShape(*nif, *nifShape)) {
            // Skip if not patchable shape.
            Logger::trace(L"Skipping: Shape is not patchable");
            continue;
        }

        auto& curMeshShapeMeta = meshMeta.shapeMeta[shapeMetaIdx++];
        curMeshShapeMeta.blockID = shapeBlockID;
        curMeshShapeMeta.shapeName = shapeName;

        // The engine replaces the texture set of skin tint shapes with the actor's skin texture, so an
        // alternate texture on one never renders. Ignore it to match vanilla behavior.
        const auto* const shaderBSSP = dynamic_cast<nifly::BSShaderProperty*>(nif->GetShader(nifShape));
        const bool isSkinTint
            = (shaderBSSP != nullptr) && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF1_FACEGEN_RGB_TINT);

        PGTypes::TextureSet* ptrAltTex = nullptr;
        if (!isSkinTint && alternateTextures.contains(oldIndex3D)) {
            ptrAltTex = &alternateTextures.at(oldIndex3D);
        } else {
            // We want to include any texture sets that do not have alternate textures defined to be compared.
            nonAltTexShapes.insert(oldIndex3D);
        }
        if (!processNIFShape(nif,
                             nifShape,
                             curMeshShapeMeta,
                             patcherObjects,
                             isSinglepassMATO,
                             formKey,
                             modelRecordType,
                             ptrAltTex)) {
            return false;
        }
    }

    // Stock marker blocks that hold no differences are deleted only now, because deleting a block renumbers the
    // blocks behind it.
    PatcherMeshPreStockMarker::removeEmptyMarkers(*nif);

    // Run global patchers.
    for (const auto& globalPatcher : patcherObjects.globalPatchers) {
        const Logger::Prefix prefixPatches(StringUtil::utf8toUTF16(globalPatcher->patcherName()));
        if (globalPatcher->applyPatch())
            meshMeta.globalPatchersApplied.push_back(globalPatcher->patcherName());
    }

    return true;
}

bool PGPatcher::processNIFShape(nifly::NifFile* nif,
                                nifly::NiShape* nifShape,
                                MeshShapeMeta& meshShapeMeta,
                                const PatcherUtil::PatcherMeshObjectSet& patchers,
                                bool isSinglepassMATO,
                                const PGMeshPermutationTracker::FormKey& formKey,
                                const PGPlugin::ModelRecordType& modelRecordType,
                                PGTypes::TextureSet* alternateTexture)
{
    if (!nif)
        throw std::runtime_error("NIF is null");

    if (!nifShape)
        throw std::runtime_error("NIFShape is null");

    // A shader other shapes share is copied for this shape before any patcher writes it, see PGNIFUtil.
    PGNIFUtil::unshareShader(*nif, *nifShape);

    // Prep.
    PGTypes::TextureSet slots;
    if (!alternateTexture) {
        slots = PGNIFUtil::textureSlots(nif, nifShape);
    } else {
        Logger::trace("Alternate texture exist for this shape");
        slots = *alternateTexture;
    }

    // Log slots.
    Logger::trace("Texture Slots: {}", PGTypes::strFromTextureSlots(slots));

    // Apply prepatchers.
    for (const auto& prePatcher : patchers.prePatchers) {
        const Logger::Prefix prefixPatches(prePatcher->patcherName());
        if (prePatcher->applyPatch(slots, *nifShape)) {
            meshShapeMeta.prePatchersApplied.push_back(prePatcher->patcherName());

            if (nif->GetBlockID(nifShape) == nifly::NIF_NPOS) {
                // Shape was deleted, nothing else to do.
                return true;
            }
        }
    }

    if (PGNIFUtil::isShaderPatchableShape(*nif, *nifShape)) {
        // Allowed shaders from result of patchers.
        const auto matches
            = PGPatcher::matches(slots, patchers, isSinglepassMATO, modelRecordType, &patchers, nifShape);
        std::vector<PatcherUtil::ShaderPatcherMatch> enabledMatches;

        // Add matches to mesh shape meta.
        for (const auto& match : matches) {
            MatchMeta matchMeta;
            matchMeta.mod = match.mod;
            matchMeta.shader = match.shader;
            matchMeta.shaderTransformTo = match.shaderTransformTo;
            matchMeta.matchedPath = match.match.matchedPath;

            if (PGGlobals::isPGMMSet()) {
                // Record which mod supplies each result texture so the conflict viewer can flag
                // matches whose result textures come from different mods. Transformed matches get
                // their slots from the transform's target shader (running the actual transform here
                // would schedule texture generation); the source match path is kept for attribution
                // since transform-generated files belong to no mod anyway.
                auto resultShader = match.shader;
                if (match.shaderTransformTo != PGEnums::ShapeShader::Unknown
                    && patchers.shaderPatchers.contains(match.shaderTransformTo)) {
                    resultShader = match.shaderTransformTo;
                }

                PGTypes::TextureSet resultSlots = slots;
                patchers.shaderPatchers.at(resultShader)->applyPatchSlots(resultSlots, match.match);
                for (size_t slot = 0; slot < resultSlots.size(); slot++) {
                    if (resultSlots.at(slot).empty())
                        continue;

                    auto slotMod = PGGlobals::pgmm()->modByFileSmart(resultSlots.at(slot));
                    if (slotMod) {
                        matchMeta.resultTextureMods.emplace_back(static_cast<PGEnums::TextureSlots>(slot),
                                                                 std::move(slotMod));
                    }
                }
            }

            meshShapeMeta.matches[formKey].push_back(matchMeta);

            if (match.mod) {
                const std::shared_lock lk(match.mod->mutex);
                if (!match.mod->isEnabled)
                    continue;
            }

            enabledMatches.push_back(match);
        }

        PatcherUtil::ShaderPatcherMatch winningShaderMatch;
        // Get winning match.
        if (!enabledMatches.empty()) {
            winningShaderMatch = enabledMatches.at(0);

            // Apply transforms.
            if (applyTransformIfNeeded(winningShaderMatch, patchers))
                Logger::trace("Shader transform was applied");

            Logger::trace(L"Winning Match: {} / {} / {}",
                          StringUtil::utf8toUTF16(PGEnums::strFromShader(winningShaderMatch.shader)),
                          !winningShaderMatch.mod ? L"" : winningShaderMatch.mod->name,
                          winningShaderMatch.match.matchedPath);

            // Loop through patchers.
            patchers.shaderPatchers.at(winningShaderMatch.shader)
                ->applyPatch(slots, *nifShape, winningShaderMatch.match);

            if (nif->GetBlockID(nifShape) == nifly::NIF_NPOS) {
                // Shape was deleted, nothing else to do.
                return true;
            }
        }
    }

    // Apply postpatchers.
    for (const auto& postPatcher : patchers.postPatchers) {
        const Logger::Prefix prefixPatches(postPatcher->patcherName());
        if (postPatcher->applyPatch(slots, *nifShape)) {
            meshShapeMeta.postPatchersApplied.push_back(postPatcher->patcherName());

            if (nif->GetBlockID(nifShape) == nifly::NIF_NPOS) {
                // Shape was deleted, nothing else to do.
                return true;
            }
        }
    }

    if (!alternateTexture) {
        // Assign texture set to nif. A texture set other shapes share is copied for this shape first, see PGNIFUtil.
        PGNIFUtil::setTextureSlots(nif, nifShape, slots);
    } else {
        // Assign new slots to propogate upstream for alternate textures.
        *alternateTexture = slots;
    }

    Logger::trace("Texture Slots Modified: {}", PGTypes::strFromTextureSlots(slots));

    // Every patcher is done with the shape: keep only what they changed in its stock marker, if it has one.
    PatcherMeshPreStockMarker::finalizeMarker(*nif, *nifShape);

    return true;
}

uint64_t PGPatcher::digestMatches(const std::vector<PatcherUtil::ShaderPatcherMatch>& matches,
                                  const PatcherUtil::PatcherMeshObjectSet& patchers)
{
    // Everything about the ordered match list that influences how the winning match is chosen and applied. Mod
    // priorities are deliberately not part of the digest: only their effect (the order of the list) matters, so
    // renumbering priorities when a mod is added does not invalidate meshes whose matches did not change.
    HashUtil::Fnv1a64 hasher;
    hasher.add(static_cast<uint64_t>(matches.size()));
    for (const auto& match : matches) {
        hasher.add(match.shader);
        hasher.add(StringUtil::toLowerASCIIFast(match.match.matchedPath));

        if (match.mod) {
            const std::shared_lock modLock(match.mod->mutex);
            hasher.add(match.mod->name);
            hasher.add(match.mod->isEnabled);
        } else {
            hasher.add(std::wstring());
            hasher.add(false);
        }

        const auto patcherIt = patchers.shaderPatchers.find(match.shader);
        if (patcherIt != patchers.shaderPatchers.end())
            hasher.add(patcherIt->second->matchExtraDataHash(match.match));
    }

    return hasher.value();
}

uint64_t PGPatcher::computeMatchesDigest(const std::filesystem::path& nifPath,
                                         const PGTypes::TextureSet& slots,
                                         bool isSinglepassMATO,
                                         const PGPlugin::ModelRecordType& modelRecordType)
{
    // Facegen and BodySlide ShapeData meshes are only ever patched for a dummy use whose flags follow from the path,
    // and the TruePBR patcher applies "nif_filter" depending on those flags, so the digest has to see them too.
    PGPlugin::MeshUseAttributes meshUse;
    meshUse.isFacegen = PGNIFUtil::isFacegenMesh(nifPath);
    meshUse.isBodySlideShapeData = PGNIFUtil::isBodySlideShapeDataMesh(nifPath);

    // Shader patchers only need a NIF for canApply, which is not part of the digest.
    PatcherUtil::PatcherMeshObjectSet patchers;
    for (const auto& [shader, factory] : s_meshPatchers.shaderPatchers) {
        auto patcher = factory(nifPath, nullptr);
        patcher->setMeshUse(meshUse);
        patchers.shaderPatchers.emplace(shader, std::move(patcher));
    }

    const auto matches = PGPatcher::matches(slots, patchers, isSinglepassMATO, modelRecordType);
    return digestMatches(matches, patchers);
}

std::vector<PatcherUtil::ShaderPatcherMatch> PGPatcher::matches(const PGTypes::TextureSet& slots,
                                                                const PatcherUtil::PatcherMeshObjectSet& patchers,
                                                                bool isSinglepassMATO,
                                                                const PGPlugin::ModelRecordType& modelRecordType,
                                                                const PatcherUtil::PatcherMeshObjectSet* patcherObjects,
                                                                nifly::NiShape* shape)
{
    std::vector<PatcherUtil::ShaderPatcherMatch> matches;
    uint64_t matchesDigest = 0;

    // Every lookup made while building the match list is covered by the digest recorded below, so individual
    // lookups are not recorded as dependencies.
    {
        const PGRunObserver::SuspendRecording suspendRecording;

        std::unordered_set<std::shared_ptr<PGModManager::Mod>, PGModManager::Mod::ModHash> modSet;
        if (patcherObjects && patchers.shaderPatchers.size() != patcherObjects->shaderPatchers.size())
            throw std::runtime_error("Patcher objects size mismatch");

        if ((patcherObjects && !shape) || (!patcherObjects && shape))
            throw std::runtime_error("If shape or patcherObjects is set, both must be set");

        for (const auto& [shader, patcher] : patchers.shaderPatchers) {
            // Note: name is defined in source code in UTF8-encoded files.
            const Logger::Prefix prefixPatches(patcher->patcherName());

            // Check if shader should be applied.
            std::vector<PatcherMeshShader::PatcherMatch> curMatches;
            if (!patcher->shouldApply(slots, curMatches)) {
                Logger::trace(L"Rejecting: Shader not applicable");
                continue;
            }

            for (const auto& match : curMatches) {
                if (!PGGlobals::pgd()->isFile(match.matchedPath)) {
                    Logger::trace(L"Rejecting: Matched path '{}' is not a file", match.matchedPath);
                    continue;
                }

                PatcherUtil::ShaderPatcherMatch curMatch;
                if (PGGlobals::isPGMMSet())
                    curMatch.mod = PGGlobals::pgmm()->modByFileSmart(match.matchedPath);

                curMatch.shader = shader;
                curMatch.match = match;
                curMatch.shaderTransformTo = PGEnums::ShapeShader::Unknown;

                matches.push_back(curMatch);
                if (curMatch.mod) {
                    // Add mod to set.
                    modSet.insert(curMatch.mod);
                }
            }
        }

        // Populate conflict mods if set.
        if (!modSet.empty()) {
            // Add mods to conflict set.
            for (const auto& match : matches) {
                if (!match.mod)
                    continue;

                const std::unique_lock lock(match.mod->mutex);

                for (const auto& conflictMod : modSet)
                    if (conflictMod != match.mod)
                        match.mod->conflicts.insert(conflictMod);
            }
        }

        // Sort before filtering (filtering preserves relative order) so the digest covers the complete ordered list,
        // which is what incremental runs recompute without a loaded mesh.
        sortMatches(matches);
        matchesDigest = digestMatches(matches, patchers);

        // Loop through matches and delete any that cannot apply.
        // Verify shape can apply.
        if (patcherObjects) {
            for (auto it = matches.begin(); it != matches.end();) {
                auto& curMatch = *it;

                // Check canApply for this shape.
                bool canApplyBaseShader = false;
                {
                    const auto& curPatcher = patcherObjects->shaderPatchers.at(curMatch.shader);
                    canApplyBaseShader = curPatcher->canApply(*shape, isSinglepassMATO, modelRecordType);
                }
                bool canApplyTransformShader = false;

                // See if transform is possible.
                if (patchers.shaderTransformPatchers.contains(curMatch.shader)) {
                    // A transform patcher is avilable, see if it should be applied.
                    // Get objects.
                    const auto& transformPatcherPair = patcherObjects->shaderTransformPatchers.at(curMatch.shader);
                    auto* const transformPatcher = transformPatcherPair.second.get();

                    // Check if transform should be applied.
                    if (transformPatcher->shouldTransform(curMatch.match, canApplyBaseShader)) {
                        // Transform can apply.
                        const auto transformToShader = transformPatcherPair.first;
                        {
                            const auto& curPatcher = patcherObjects->shaderPatchers.at(transformToShader);
                            canApplyTransformShader = curPatcher->canApply(*shape, isSinglepassMATO, modelRecordType);
                        }

                        if (canApplyTransformShader)
                            curMatch.shaderTransformTo = transformToShader;
                    }
                }

                if (!canApplyBaseShader) {
                    // Base shaders can't do it, lets check transforms.
                    if (curMatch.shaderTransformTo == PGEnums::ShapeShader::Unknown) {
                        it = matches.erase(it);
                        continue;
                    }

                    if (!canApplyTransformShader) {
                        it = matches.erase(it);
                        continue;
                    }
                }
                ++it;
            }

            // Above, canApply assigned the transform targets and those take part in the ranking, so sort again to
            // restore the final order the patchers rely on (the match at index 0 wins).
            sortMatches(matches);
        }
    }

    // Record the digest as a dependency of the mesh being patched (no-op unless recording).
    PGRunObserver::current().recordMatches(slots, isSinglepassMATO, modelRecordType, matchesDigest);

    return matches;
}

bool PGPatcher::applyTransformIfNeeded(PatcherUtil::ShaderPatcherMatch& match,
                                       const PatcherUtil::PatcherMeshObjectSet& patchers)
{
    // Transform if required.
    if (match.shaderTransformTo != PGEnums::ShapeShader::Unknown) {
        // Find transform object.
        auto* const transform = patchers.shaderTransformPatchers.at(match.shader).second.get();

        // Transform Shader.
        transform->transform(match.match, match.match);

        match.shader = match.shaderTransformTo;
        match.shaderTransformTo = PGEnums::ShapeShader::Unknown;

        return true;
    }

    return false;
}

PatcherUtil::PatcherMeshObjectSet PGPatcher::createNIFPatcherObjects(const std::filesystem::path& nifPath,
                                                                     nifly::NifFile* nif,
                                                                     const PGPlugin::MeshUseAttributes& meshUse)
{
    auto patcherObjects = PatcherUtil::PatcherMeshObjectSet();
    for (const auto& factory : s_meshPatchers.prePatchers) {
        auto patcher = factory(nifPath, nif);
        patcher->setMeshUse(meshUse);
        patcherObjects.prePatchers.emplace_back(std::move(patcher));
    }
    for (const auto& [shader, factory] : s_meshPatchers.shaderPatchers) {
        auto patcher = factory(nifPath, nif);
        patcher->setMeshUse(meshUse);
        patcherObjects.shaderPatchers.emplace(shader, std::move(patcher));
    }
    for (const auto& [shader, factory] : s_meshPatchers.shaderTransformPatchers) {
        auto transform = factory.second(nifPath, nif);
        transform->setMeshUse(meshUse);
        patcherObjects.shaderTransformPatchers[shader] = { factory.first, std::move(transform) };
    }
    for (const auto& factory : s_meshPatchers.postPatchers) {
        auto patcher = factory(nifPath, nif);
        patcher->setMeshUse(meshUse);
        patcherObjects.postPatchers.emplace_back(std::move(patcher));
    }
    for (const auto& factory : s_meshPatchers.globalPatchers) {
        auto patcher = factory(nifPath, nif);
        patcher->setMeshUse(meshUse);
        patcherObjects.globalPatchers.emplace_back(std::move(patcher));
    }

    return patcherObjects;
}
