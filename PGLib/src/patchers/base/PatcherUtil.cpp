#include "patchers/base/PatcherUtil.hpp"

#include "Logger.hpp"
#include "NIFUtil.hpp"
#include "PGDiag.hpp"
#include "PGGlobals.hpp"

using namespace std;

// TODO these methods should probably move into shader and transform classes respectively
auto PatcherUtil::getWinningMatch(
    const vector<ShaderPatcherMatch>& matches, const unordered_map<wstring, int>* modPriority) -> ShaderPatcherMatch
{
    // Find winning mod
    int maxPriority = -1;
    auto winningShaderMatch = PatcherUtil::ShaderPatcherMatch();

    for (const auto& match : matches) {
        const Logger::Prefix prefixMod(match.mod);
        Logger::trace(L"Checking mod");

        int curPriority = -1;
        if (modPriority != nullptr && modPriority->find(match.mod) != modPriority->end()) {
            curPriority = modPriority->at(match.mod);
        }

        if (curPriority < maxPriority) {
            // skip mods with lower priority than current winner
            Logger::trace(L"Rejecting: Mod has lower priority than current winner");
            continue;
        }

        Logger::trace(L"Mod accepted");
        maxPriority = curPriority;
        winningShaderMatch = match;
    }

    Logger::trace(L"Winning mod: {}", winningShaderMatch.mod);
    return winningShaderMatch;
}

auto PatcherUtil::applyTransformIfNeeded(
    ShaderPatcherMatch& match, const PatcherMeshObjectSet& patchers, const bool& matchOnly) -> bool
{
    // Transform if required
    if (match.shaderTransformTo != NIFUtil::ShapeShader::UNKNOWN) {
        // Find transform object
        auto* const transform = patchers.shaderTransformPatchers.at(match.shader).at(match.shaderTransformTo).get();

        // Transform Shader
        if (!matchOnly) {
            transform->transform(match.match, match.match);
        }

        match.shader = match.shaderTransformTo;
        match.shaderTransformTo = NIFUtil::ShapeShader::UNKNOWN;

        return true;
    }

    return false;
}

auto PatcherUtil::getMatches(const NIFUtil::TextureSet& slots, const PatcherUtil::PatcherMeshObjectSet& patchers,
    const std::unordered_map<NIFUtil::ShapeShader, bool>& canApply, PatcherUtil::ConflictModResults* conflictMods)
    -> std::vector<PatcherUtil::ShaderPatcherMatch>
{
    if (PGGlobals::getPGD() == nullptr) {
        throw runtime_error("PGD is null");
    }

    vector<PatcherUtil::ShaderPatcherMatch> matches;

    if (conflictMods == nullptr) {
        // only check cache if conflictmods is null
        const lock_guard<mutex> lock(PatcherUtil::s_processShapeMutex);
        if (PatcherUtil::s_shaderMatchCache.contains(slots)) {
            matches = PatcherUtil::s_shaderMatchCache[slots];
            return matches;
        }
    }

    unordered_set<wstring> modSet;
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
                const auto& availableTransforms = patchers.shaderTransformPatchers.at(shader);
                // loop from highest element of map to 0
                for (const auto& availableTransform : ranges::reverse_view(availableTransforms)) {
                    if (canApply.contains(availableTransform.first) && canApply.at(availableTransform.first)) {
                        // Found a transform that can apply, set the transform in the match
                        curMatch.shaderTransformTo = availableTransform.first;
                        break;
                    }
                }
            }

            // Add to matches if shader can apply (or if transform shader exists and can apply)
            if ((canApply.contains(shader) && canApply.at(shader))
                || curMatch.shaderTransformTo != NIFUtil::ShapeShader::UNKNOWN) {
                matches.push_back(curMatch);
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
    if (conflictMods != nullptr) {
        if (modSet.size() > 1) {
            const lock_guard<mutex> lock(conflictMods->mutex);

            // add mods to conflict set
            for (const auto& match : matches) {
                if (conflictMods->mods.find(match.mod) == conflictMods->mods.end()) {
                    conflictMods->mods.insert({ match.mod, { set<NIFUtil::ShapeShader>(), unordered_set<wstring>() } });
                }

                get<0>(conflictMods->mods[match.mod]).insert(match.shader);
                get<1>(conflictMods->mods[match.mod]).insert(modSet.begin(), modSet.end());
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
