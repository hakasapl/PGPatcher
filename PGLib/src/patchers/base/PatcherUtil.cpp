#include "patchers/base/PatcherUtil.hpp"

#include "Logger.hpp"
#include "ModManagerDirectory.hpp"
#include "NIFUtil.hpp"
#include "PGDiag.hpp"
#include "PGGlobals.hpp"
#include <memory>

using namespace std;

// TODO these methods should probably move into shader and transform classes respectively
auto PatcherUtil::getWinningMatch(const vector<ShaderPatcherMatch>& matches) -> ShaderPatcherMatch
{
    // Find winning mod
    int maxPriority = -1;
    auto winningShaderMatch = PatcherUtil::ShaderPatcherMatch();

    for (const auto& match : matches) {
        wstring modName;
        int curPriority = -1;
        if (match.mod != nullptr) {
            modName = match.mod->name;
            curPriority = match.mod->priority;
        }

        const Logger::Prefix prefixMod(modName);
        Logger::trace("Checking mod");

        if (curPriority < maxPriority) {
            // skip mods with lower priority than current winner
            Logger::trace(L"Rejecting: Mod has lower priority than current winner");
            continue;
        }

        Logger::trace(L"Mod accepted");
        maxPriority = curPriority;
        winningShaderMatch = match;
    }

    Logger::trace(L"Winning mod: {}", winningShaderMatch.mod == nullptr ? L"" : winningShaderMatch.mod->name);
    return winningShaderMatch;
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
    if (dryRun) {
        if (modSet.size() > 1) {
            // add mods to conflict set
            for (const auto& match : matches) {
                if (match.mod == nullptr) {
                    continue;
                }

                const lock_guard<mutex> lock(match.mod->mutex);

                match.mod->shaders.insert(match.shader);
                match.mod->conflicts.insert(modSet.begin(), modSet.end());
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
