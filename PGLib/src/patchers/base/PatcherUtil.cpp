#include "patchers/base/PatcherUtil.hpp"

#include "Logger.hpp"
#include "ModManagerDirectory.hpp"
#include "NIFUtil.hpp"
#include "PGDiag.hpp"
#include "PGGlobals.hpp"
#include <memory>

using namespace std;

auto PatcherUtil::getWinningMatch(const vector<ShaderPatcherMatch>& matches) -> ShaderPatcherMatch
{
    if (matches.empty()) {
        return {};
    }

    unordered_map<shared_ptr<ModManagerDirectory::Mod>, ShaderPatcherMatch, ModManagerDirectory::Mod::ModHash>
        modSetToMatch;
    unordered_set<shared_ptr<ModManagerDirectory::Mod>, ModManagerDirectory::Mod::ModHash> modSet;
    for (const auto& match : matches) {
        // In the case where there are multiple matches from one mod, this loop ensures matches later in the vector win
        modSet.insert(match.mod);
        modSetToMatch[match.mod] = match;
    }

    const auto winningMod = PGGlobals::getMMD()->getWinningMod(modSet);
    if (winningMod == nullptr) {
        // No winning mod, return last match (this usually happens if mod conflict resolution is disabled)
        return matches.back();
    }

    if (!modSetToMatch.contains(winningMod)) {
        // No match for winning mod, return last match (this usually happens if mod conflict resolution is disabled)
        throw runtime_error("No match for winning mod");
    }

    return modSetToMatch[winningMod];
}

auto PatcherUtil::applyTransformIfNeeded(ShaderPatcherMatch& match, const PatcherMeshObjectSet& patchers) -> bool
{
    // Transform if required
    if (match.shaderTransformTo != NIFUtil::ShapeShader::UNKNOWN) {
        // Find transform object
        auto* const transform = patchers.shaderTransformPatchers.at(match.shader).second.get();

        // Transform Shader
        transform->transform(match.match, match.match);

        match.shader = match.shaderTransformTo;
        match.shaderTransformTo = NIFUtil::ShapeShader::UNKNOWN;

        return true;
    }

    return false;
}

auto PatcherUtil::getMatches(const NIFUtil::TextureSet& slots, const PatcherUtil::PatcherMeshObjectSet& patchers,
    const bool& dryRun) -> std::vector<PatcherUtil::ShaderPatcherMatch>
{
    if (PGGlobals::getPGD() == nullptr) {
        throw runtime_error("PGD is null");
    }

    vector<PatcherUtil::ShaderPatcherMatch> matches;
    if (!dryRun) {
        const lock_guard<mutex> lock(PatcherUtil::s_processShapeMutex);
        if (PatcherUtil::s_shaderMatchCache.contains(slots)) {
            return PatcherUtil::s_shaderMatchCache[slots];
        }
    }

    unordered_set<shared_ptr<ModManagerDirectory::Mod>, ModManagerDirectory::Mod::ModHash> modSet;
    for (const auto& [shader, patcher] : patchers.shaderPatchers) {
        if (shader == NIFUtil::ShapeShader::NONE) {
            // TEMPORARILY disable default patcher
            continue;
        }

        // note: name is defined in source code in UTF8-encoded files
        const Logger::Prefix prefixPatches(patcher->getPatcherName());

        // Check if shader should be applied
        vector<PatcherMeshShader::PatcherMatch> curMatches;
        if (!patcher->shouldApply(slots, curMatches)) {
            Logger::trace(L"Rejecting: Shader not applicable");
            continue;
        }

        for (const auto& match : curMatches) {
            PatcherUtil::ShaderPatcherMatch curMatch;
            curMatch.mod = PGGlobals::getPGD()->getMod(match.matchedPath);
            curMatch.shader = shader;
            curMatch.match = match;
            curMatch.shaderTransformTo = NIFUtil::ShapeShader::UNKNOWN;

            // See if transform is possible
            if (patchers.shaderTransformPatchers.contains(shader)) {
                const auto& [shaderTransformTo, transformPatcher] = patchers.shaderTransformPatchers.at(shader);
                // loop from highest element of map to 0
                curMatch.shaderTransformTo = shaderTransformTo;
            }

            matches.push_back(curMatch);
            if (curMatch.mod != nullptr) {
                // add mod to set
                modSet.insert(curMatch.mod);
            }
        }
    }

    // Populate cache
    {
        const lock_guard<mutex> lock(PatcherUtil::s_processShapeMutex);
        PatcherUtil::s_shaderMatchCache[slots] = matches;
    }

    // Populate conflict mods if set
    static auto* mmd = PGGlobals::getMMD();
    if (dryRun) {
        if (modSet.size() > 1) {
            // add mods to conflict set
            for (const auto& match : matches) {
                if (match.mod == nullptr) {
                    continue;
                }

                const lock_guard<mutex> lock(match.mod->mutex);

                match.mod->shaders.insert(match.shader);
                const auto finalSet = mmd->disqualifyModsFromSet(modSet);
                if (finalSet.size() > 1) {
                    mmd->addConflictModSet(finalSet);
                }
            }
        }

        return matches;
    }

    // Populate diag JSON if set with each match
    {
        const PGDiag::Prefix diagShaderPatcherPrefix("shaderPatcherMatches", nlohmann::json::value_t::array);
        for (const auto& match : matches) {
            PGDiag::pushBack(match.getJSON());
        }
    }

    return matches;
}

auto PatcherUtil::createCanApplyMap(const PatcherMeshObjectSet& patchers, nifly::NiShape& shape,
    const NIFUtil::ShapeShader* forceShader) -> std::unordered_map<NIFUtil::ShapeShader, bool>
{
    unordered_map<NIFUtil::ShapeShader, bool> canApplyMap;
    for (const auto& [shader, patcher] : patchers.shaderPatchers) {
        if (forceShader != nullptr && shader != *forceShader) {
            // skip if force shader is set and does not match current shader
            continue;
        }

        canApplyMap[shader] = patcher->canApply(shape);
    }

    return canApplyMap;
}

void PatcherUtil::filterMatches(
    vector<ShaderPatcherMatch>& matches, const unordered_map<NIFUtil::ShapeShader, bool>& canApply)
{
    // remove any matches that cannot apply
    for (auto it = matches.begin(); it != matches.end();) {
        const bool canApplyShader = canApply.contains(it->shader) && canApply.at(it->shader);
        const bool canApplyShaderTransform
            = canApply.contains(it->shaderTransformTo) && canApply.at(it->shaderTransformTo);

        if (!canApplyShader && !canApplyShaderTransform) {
            it = matches.erase(it);
        } else {
            ++it;
        }
    }
}
