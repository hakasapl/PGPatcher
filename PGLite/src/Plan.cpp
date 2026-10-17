#include "PCH.h"

#include "Plan.hpp"

#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGModManager.hpp"
#include "PGPatcher.hpp"
#include "PGPlugin.hpp"
#include "RecordEdits.hpp"
#include "Resources.hpp"
#include "Runtime.hpp"
#include "Uses.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <fmt/xchar.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace PGLite {

namespace {

constexpr std::wstring_view meshesPrefix = L"meshes\\";
constexpr std::wstring_view variantFolder = L"meshes\\_pgpatcher_dups\\";

/// @brief The virtual path of a variant: the offline duplicate folder with the session's variant number.
std::filesystem::path variantPath(const std::filesystem::path& basePath,
                                  unsigned number)
{
    auto rest = basePath.wstring();
    if (rest.starts_with(meshesPrefix))
        rest.erase(0, meshesPrefix.size());

    return std::wstring(variantFolder) + std::to_wstring(number) + L"\\" + rest;
}

Context contextOf(const RecordUse& use)
{
    return {
        .alternateTextures = use.attributes.alternateTextures,
        .isSinglepassMATO = use.attributes.isSinglepassMATO,
        .isGrass = use.attributes.recType == PGPlugin::ModelRecordType::Grass,
    };
}

/// @brief The form key a member is committed under, which identifies the member in the tracker's outputs.
PGMeshPermutationTracker::FormKey memberKey(std::size_t member)
{
    return { .modKey = L"PGLite", .formID = static_cast<unsigned>(member), .subMODL = "" };
}

std::shared_ptr<const std::vector<std::byte>> toBytes(const std::string& data)
{
    auto bytes = std::make_shared<std::vector<std::byte>>(data.size());
    std::ranges::transform(data, bytes->begin(), [](char ch) { return static_cast<std::byte>(ch); });
    return bytes;
}

std::wstring useLabel(const PGMeshPermutationTracker::FormKey& key)
{
    return fmt::format(L"{}:{:06X}:{}", key.modKey, key.formID, StringUtil::utf8toUTF16(key.subMODL));
}

bool isSameTextureSet(const PGTypes::TextureSet& a,
                      const PGTypes::TextureSet& b)
{
    for (std::size_t slot = 0; slot < numTextureSlots - 1; slot++)
        if (!StringUtil::asciiFastIEquals(a.at(slot), b.at(slot)))
            return false;

    return true;
}

/// @brief The texture files the lookups of this thread could not read, as the observer reported them, and the last
/// one: a patch compares the count before and after its run (Family::noteTextureReadFailure).
thread_local unsigned s_textureReadFailures = 0;
thread_local std::filesystem::path s_lastTextureReadFailure;
}

//
// Family.
//

void Family::noteTextureReadFailure(const std::filesystem::path& texPath)
{
    s_textureReadFailures++;
    s_lastTextureReadFailure = texPath;
}

std::shared_ptr<const std::vector<std::byte>> Family::bytes(std::size_t member,
                                                            std::size_t pathIndex,
                                                            TextureSetFactory& textureSets,
                                                            unsigned generation)
{
    const std::scoped_lock lock(m_patchMutex);
    const Logger::Prefix prefix(basePaths.at(pathIndex).wstring());

    // A failed first patch edited no record, so the family stays unpatched for the session: the failure is the
    // mesh's and would repeat. A failed later open keeps the state: the records carry the edits of the first patch,
    // and the next open produces the member again.
    const auto keepUnpatched = [this] {
        if (m_state == State::Unpatched)
            m_state = State::Failed;
    };

    try {
        return serve(member, pathIndex, textureSets, generation);
    } catch (const RememberedReadFailure&) {
        // An open inside the retry interval of an earlier failure: the original is served, no attempt is spent.
        Logger::debug(L"The original mesh of {} is used for this load, an input could not be read moments ago",
                      basePaths.at(pathIndex).wstring());
    } catch (const EngineReadError& e) {
        // An input that could not be read this time, which a later attempt may read: nothing of the patch is kept. A
        // first patch throws before it edits a record, so the family stays unpatched, and the first open after the
        // retry interval patches it again, until the attempts are spent; then the family stays unpatched for the
        // session, as before the retries.
        m_readFailures++;
        m_readFailureTime = std::chrono::steady_clock::now();
        if (m_state == State::Unpatched && m_readFailures > maxPatchReadFailures) {
            Logger::error(L"Unable to patch {} in {} attempts, the original mesh is used for the session: {}",
                          basePaths.at(pathIndex).wstring(),
                          m_readFailures,
                          StringUtil::utf8toUTF16(e.what()));
            keepUnpatched();
        } else {
            Logger::error(L"Unable to patch {}, the original mesh is used for this load: {}",
                          basePaths.at(pathIndex).wstring(),
                          StringUtil::utf8toUTF16(e.what()));
        }
    } catch (const std::exception& e) {
        Logger::error(L"Unable to patch {}, the original mesh is used: {}",
                      basePaths.at(pathIndex).wstring(),
                      StringUtil::utf8toUTF16(e.what()));
        keepUnpatched();
    } catch (...) {
        Logger::error(L"Unable to patch {}, the original mesh is used", basePaths.at(pathIndex).wstring());
        keepUnpatched();
    }

    try {
        return original(pathIndex);
    } catch (...) {
        return nullptr;
    }
}

std::shared_ptr<const std::vector<std::byte>> Family::serve(std::size_t member,
                                                            std::size_t pathIndex,
                                                            TextureSetFactory& textureSets,
                                                            unsigned generation)
{
    if (m_state == State::Unpatched) {
        // An input could not be read moments ago: until the retry interval passed, every open of the family serves
        // the original without patching again, so the opens of one burst stay consistent with each other and the
        // next attempt is a new read (PGD3D remembers a failed header read for the same time).
        if (m_readFailures && std::chrono::steady_clock::now() - m_readFailureTime < readFailureRetryInterval)
            throw RememberedReadFailure("an input could not be read moments ago");

        std::shared_ptr<const std::vector<std::byte>> wanted;
        patch(member, pathIndex, textureSets, generation, wanted);
        if (wanted)
            return wanted;
    }

    if (m_state != State::Patched)
        return original(pathIndex);

    const auto& output = m_outputs.at(pathIndex).at(member);
    if (output.isOriginalKept)
        return original(pathIndex);

    return produce(m_leaders.at(member), pathIndex);
}

void Family::patch(std::size_t wantedMember,
                   std::size_t wantedPath,
                   TextureSetFactory& textureSets,
                   unsigned generation,
                   std::shared_ptr<const std::vector<std::byte>>& wantedBytes)
{
    // Mod-level "meshes ignored" is a per-path rule that needs the mod manager, so it is applied here (DESIGN.md 4.4).
    if (!isFacegen && PGGlobals::isPGMMSet()) {
        const auto mod = PGGlobals::pgmm()->modByFileSmart(basePaths.front());
        if (mod) {
            const std::shared_lock modLock(mod->mutex);
            if (mod->areMeshesIgnored) {
                Logger::trace(L"Skipping NIF patching for mod with ignored meshes: {}", mod->name);
                m_state = State::IgnoredByMod;
                return;
            }
        }
    }

    std::vector<std::vector<MemberOutput>> outputs(basePaths.size());
    bool hasUnresolvedFile = false;
    for (std::size_t pathIndex = 0; pathIndex < basePaths.size(); pathIndex++) {
        const Logger::Prefix pathPrefix(basePaths.at(pathIndex).wstring());
        auto& pathOutputs = outputs.at(pathIndex);
        pathOutputs.resize(members.size());

        if (pathIndex >= sources.size() || !sources.at(pathIndex)) {
            // A file of a weighted pair the engine cannot resolve: nothing is registered for it, the other file of
            // the pair is patched on its own.
            hasUnresolvedFile = true;
            continue;
        }

        const auto originalBytes = original(pathIndex);
        if (!originalBytes)
            throw EngineReadError("the original mesh cannot be read");

        PGMeshPermutationTracker tracker(basePaths.at(pathIndex), *originalBytes);
        for (std::size_t member = 0; member < members.size(); member++) {
            if (!patchMember(tracker, member, pathIndex, pathOutputs.at(member)))
                throw std::runtime_error("a shape could not be processed");

            if (pathOutputs.at(member).isOriginalKept && pathIndex == wantedPath && member == wantedMember)
                wantedBytes = originalBytes;
        }

        const auto serialized = tracker.serializeMeshes();
        const bool hasOutputs
            = std::ranges::any_of(pathOutputs, [](const MemberOutput& output) { return !output.isOriginalKept; });
        if (hasOutputs && serialized.empty())
            throw std::runtime_error("the patched mesh cannot be serialized");

        for (std::size_t outputIndex = 0; outputIndex < serialized.size(); outputIndex++) {
            const auto& serializedOutput = serialized.at(outputIndex);
            for (const auto& [key, altTex] : serializedOutput.result.altTexResults) {
                if (key.formID >= members.size())
                    continue;

                auto& output = pathOutputs.at(key.formID);
                output.output = outputIndex;
                output.idxCorrections = serializedOutput.result.idxCorrections;
                output.blockKinds = serializedOutput.blockKinds;
                if (pathIndex == wantedPath && key.formID == wantedMember)
                    wantedBytes = toBytes(serializedOutput.bytes);
            }
        }
    }

    // The weighted layout check of the offline program, on the two outputs of each context.
    if (basePaths.size() == 2 && !hasUnresolvedFile) {
        for (std::size_t member = 0; member < members.size(); member++) {
            const auto& a = outputs.at(0).at(member);
            const auto& b = outputs.at(1).at(member);
            if (a.isOriginalKept || b.isOriginalKept)
                continue;

            if (a.blockKinds != b.blockKinds) {
                Logger::error(L"Weighted mesh variants '{}' and '{}' do not match.",
                              basePaths.at(0).wstring(),
                              basePaths.at(1).wstring());
                break;
            }
        }
    }

    // A context merges into an earlier one only when their results are identical in every file of the family.
    m_leaders.assign(members.size(), 0);
    for (std::size_t member = 0; member < members.size(); member++) {
        m_leaders.at(member) = member;
        for (std::size_t leader = 0; leader < member; leader++) {
            bool isSameGroup = true;
            for (std::size_t pathIndex = 0; pathIndex < basePaths.size() && isSameGroup; pathIndex++) {
                const auto& a = outputs.at(pathIndex).at(leader);
                const auto& b = outputs.at(pathIndex).at(member);
                isSameGroup = a.isOriginalKept == b.isOriginalKept && (a.isOriginalKept || a.output == b.output);
            }

            if (isSameGroup) {
                m_leaders.at(member) = leader;
                break;
            }
        }
    }

    m_outputs = std::move(outputs);
    editRecords(textureSets);
    retargetMergedMembers(generation);
    m_state = State::Patched;

    std::size_t groups = 0;
    for (std::size_t member = 0; member < members.size(); member++)
        if (m_leaders.at(member) == member && !m_outputs.front().at(member).isOriginalKept)
            groups++;

    Logger::debug(
        L"Patched {} ({} contexts, {} distinct outputs)", basePaths.front().wstring(), members.size(), groups);
}

bool Family::patchMember(PGMeshPermutationTracker& tracker,
                         std::size_t member,
                         std::size_t pathIndex,
                         MemberOutput& output,
                         bool shouldCompareWithOriginal)
{
    const auto& memberInfo = members.at(member);
    const Logger::Prefix dupPrefix(useLabel(memberInfo.formKey));

    auto* const staged = tracker.stageMesh();
    PGPatcher::MeshMeta meshMeta;
    auto alternateTextures = memberInfo.context.alternateTextures;
    std::unordered_set<unsigned> nonAltTexShapes;
    const auto readFailuresBefore = s_textureReadFailures;
    const bool isProcessed = PGPatcher::processNIF(basePaths.at(pathIndex),
                                                   staged,
                                                   meshMeta,
                                                   memberInfo.attributes,
                                                   memberInfo.context.isSinglepassMATO,
                                                   memberInfo.formKey,
                                                   memberInfo.attributes.recType,
                                                   alternateTextures,
                                                   nonAltTexShapes);
    if (s_textureReadFailures != readFailuresBefore) {
        // A texture header a lookup could not read (the observer reports it): the match it was for was dropped. A
        // first patch is not kept, so that a later attempt can read it, until the family's attempts are spent. A later
        // open, or a spent budget, keeps the result: the texture is treated as missing, as before the retries, and
        // the shape order stays the one the records were edited for.
        if (m_state == State::Unpatched && m_readFailures < maxPatchReadFailures) {
            throw EngineReadError("a texture could not be read: "
                                  + StringUtil::utf16toUTF8(s_lastTextureReadFailure.wstring()));
        }

        Logger::warn(L"A texture could not be read while patching {}, the result treats it as missing: {}",
                     basePaths.at(pathIndex).wstring(),
                     s_lastTextureReadFailure.wstring());
    }
    if (!isProcessed)
        return false;

    const auto result
        = tracker.commitMesh(memberKey(member), false, alternateTextures, nonAltTexShapes, shouldCompareWithOriginal);
    output.isOriginalKept = result == PGMeshPermutationTracker::CommitResult::UnchangedFromOriginal;
    output.altTexResults = std::move(alternateTextures);
    return true;
}

std::shared_ptr<const std::vector<std::byte>> Family::produce(std::size_t member,
                                                              std::size_t pathIndex)
{
    const auto originalBytes = original(pathIndex);
    if (!originalBytes)
        throw EngineReadError("the original mesh cannot be read");

    // The member leads a group, so its first patch became an output even where the result equals the original (it
    // followed other members in that tracker); the same bytes are served again, not the original.
    PGMeshPermutationTracker tracker(basePaths.at(pathIndex), *originalBytes);
    MemberOutput output;
    if (!patchMember(tracker, member, pathIndex, output, false))
        throw std::runtime_error("a shape could not be processed");

    const auto serialized = tracker.serializeMeshes();
    if (serialized.empty())
        throw std::runtime_error("the patched mesh cannot be serialized");

    return toBytes(serialized.front().bytes);
}

std::shared_ptr<const std::vector<std::byte>> Family::original(std::size_t pathIndex)
{
    if (pathIndex >= sources.size() || !sources.at(pathIndex) || !sources.at(pathIndex)->stream)
        return nullptr;

    auto& source = *sources.at(pathIndex);
    auto bytes = std::make_shared<std::vector<std::byte>>();
    const std::scoped_lock lock(source.mutex);
    if (!Resources::readStream(*source.stream, *bytes))
        return nullptr;

    return bytes;
}

std::size_t Family::resolvedPathIndex(std::size_t pathIndex) const
{
    if (pathIndex < sources.size() && sources.at(pathIndex))
        return pathIndex;

    for (std::size_t i = 0; i < sources.size(); i++)
        if (sources.at(i))
            return i;

    return pathIndex;
}

void Family::editRecords(TextureSetFactory& textureSets)
{
    auto& snapshot = Runtime::instance().snapshot();
    for (std::size_t member = 0; member < members.size(); member++) {
        for (const auto& ref : members.at(member).uses) {
            auto* const model = ref.use->element.model;
            auto* const swap = ref.use->element.swap;
            if (!model || !swap || !swap->alternateTextures)
                continue;

            // A record whose model string another plugin replaced loads another mesh: its entries are left alone.
            if (!snapshot.isModelOwned(*model))
                continue;

            // The output of the file the record names, or of the resolved file when the named one is missing from
            // the pair (the engine derives the resolved file's variant for such a record).
            const auto& output = m_outputs.at(resolvedPathIndex(ref.pathIndex)).at(member);
            for (std::uint32_t i = 0; i < swap->numAlternateTextures; i++) {
                const auto& entry = swap->alternateTextures[i];
                const auto oldIndex = entry.index3D;
                auto index3D = oldIndex;
                auto* textureSet = entry.textureSet;

                // The shape's index in the sorted output, -1 when the shape was deleted (the engine skips the entry).
                const auto corrected = output.idxCorrections.find(static_cast<int>(oldIndex));
                if (corrected != output.idxCorrections.end() && corrected->second != static_cast<int>(oldIndex))
                    index3D = static_cast<std::uint32_t>(corrected->second);

                const auto patched = output.altTexResults.find(oldIndex);
                if (patched != output.altTexResults.end()
                    && !isSameTextureSet(patched->second, Uses::textureSet(entry.textureSet))) {
                    if (auto* const set = textureSets.textureSet(patched->second))
                        textureSet = set;
                }

                if (index3D == oldIndex && textureSet == entry.textureSet)
                    continue;

                // false: the entry keeps the value another plugin set after PGLite planned.
                std::ignore = snapshot.setAlternateTexture(*model, *swap, i, index3D, textureSet);
            }
        }
    }
}

void Family::retargetMergedMembers(unsigned generation)
{
    std::vector<std::pair<RE::TESModel*, std::filesystem::path>> edits;
    for (std::size_t member = 0; member < members.size(); member++) {
        const auto leader = m_leaders.at(member);
        if (leader == member)
            continue;

        for (const auto& ref : members.at(member).uses)
            edits.emplace_back(ref.use->element.model, members.at(leader).paths.at(ref.pathIndex));
    }

    if (edits.empty())
        return;

    // Later instances share the leader's model; the ones already created keep an identical copy until evicted.
    SKSE::GetTaskInterface()->AddTask([edits = std::move(edits), generation] {
        auto& runtime = Runtime::instance();
        if (generation != runtime.planGeneration())
            return;

        for (const auto& [model, path] : edits)
            runtime.snapshot().setModel(*model, path);
    });
}

//
// Plan.
//

Plan::Plan(unsigned generation)
    : m_generation(generation)
{
}

void Plan::build(const Uses& uses,
                 const PGParams& params,
                 unsigned& variantCounter)
{
    const auto& allowed = params.processing.allowedModelRecordTypes;
    const auto& blockList = params.processing.blockList;
    const auto& allowList = params.processing.allowList;
    const auto isExcluded = [&blockList, &allowList](const std::filesystem::path& path) {
        const auto str = path.wstring();
        if (!allowList.empty() && !PGDirectory::checkGlobMatchInVector(str, allowList))
            return true;

        return !blockList.empty() && PGDirectory::checkGlobMatchInVector(str, blockList);
    };

    // Planned in path order, so the variant numbers of a load order are the same in every session.
    std::vector<std::filesystem::path> paths;
    paths.reserve(uses.byPath().size());
    for (const auto& [path, recordUses] : uses.byPath())
        paths.push_back(path);
    std::ranges::sort(paths);

    std::size_t excluded = 0;
    std::size_t unpatched = 0;
    std::unordered_set<std::filesystem::path> planned;
    for (const auto& path : paths) {
        if (planned.contains(path))
            continue;

        planned.insert(path);
        if (isExcluded(path)) {
            excluded++;
            continue;
        }

        if (PGNIFUtil::isFacegenMesh(path)) {
            // The offline program warns about this and leaves the mesh unpatched (PGPatcher::patchNIF).
            Logger::warn(L"NIF has mesh uses but is detected as a facegen mesh: {}", path.wstring());
            unpatched++;
            continue;
        }

        auto family = std::make_unique<Family>();
        family->basePaths.push_back(path);

        const auto& pathUses = uses.byPath().at(path);
        const bool isWeighted
            = std::ranges::any_of(pathUses, [](const RecordUse& use) { return use.attributes.isWeighted; });
        if (isWeighted) {
            // Both files of a weighted pair form one family.
            const auto counterpart = Uses::weightCounterpart(path);
            if (!counterpart.empty() && uses.byPath().contains(counterpart) && !planned.contains(counterpart)
                && !isExcluded(counterpart)) {
                family->basePaths.push_back(counterpart);
                planned.insert(counterpart);
            }
        }
        std::ranges::sort(family->basePaths); // _0 before _1

        std::unordered_set<const RE::TESModel*> seenElements;
        for (std::size_t pathIndex = 0; pathIndex < family->basePaths.size(); pathIndex++) {
            for (const auto& use : uses.byPath().at(family->basePaths.at(pathIndex))) {
                // A weighted use is listed under both files; its record names one of them.
                if (!seenElements.insert(use.element.model).second)
                    continue;

                std::size_t recordPath = pathIndex;
                const auto named = Uses::normalizedModelPath(use.element.model->GetModel());
                for (std::size_t i = 0; i < family->basePaths.size(); i++)
                    if (family->basePaths.at(i) == named)
                        recordPath = i;

                if (PGPatcher::isUseUnpatched(use.attributes, allowed, true)) {
                    // Exactly ignoreBaseMesh offline: the original path keeps the unpatched mesh.
                    family->isBaseUnpatched = true;
                    family->unpatchedUses.push_back({ .use = &use, .pathIndex = recordPath });
                    continue;
                }

                const auto context = contextOf(use);
                auto member = std::ranges::find_if(family->members, [&context](const Family::Member& candidate) {
                    return candidate.context == context;
                });
                if (member == family->members.end()) {
                    Family::Member newMember;
                    newMember.context = context;
                    newMember.attributes = use.attributes;
                    newMember.formKey = use.formKey;
                    family->members.push_back(std::move(newMember));
                    member = std::prev(family->members.end());
                }

                member->uses.push_back({ .use = &use, .pathIndex = recordPath });
            }
        }

        if (family->members.empty()) {
            // Every use keeps the original mesh; nothing is registered for the path.
            unpatched++;
            continue;
        }

        for (std::size_t member = 0; member < family->members.size(); member++) {
            auto& memberInfo = family->members.at(member);
            if (!member && !family->isBaseUnpatched) {
                memberInfo.paths = family->basePaths;
                continue;
            }

            memberInfo.variantNumber = ++variantCounter;
            m_variantCount++;
            for (const auto& basePath : family->basePaths)
                memberInfo.paths.push_back(variantPath(basePath, memberInfo.variantNumber));
        }

        addTargets(*family);
        m_families.push_back(std::move(family));
    }

    Logger::info("Planned {} mesh families with {} variant paths ({} meshes excluded by the mesh rules, {} unpatched "
                 "for their record types)",
                 m_families.size(),
                 m_variantCount,
                 excluded,
                 unpatched);
}

void Plan::retargetRecords(RecordSnapshot& snapshot) const
{
    std::size_t retargeted = 0;
    std::size_t kept = 0;
    std::size_t unregistered = 0;
    for (const auto& family : m_families) {
        if (family->sources.empty()) {
            // Nothing is registered for the family, so its records keep the original paths the engine can load.
            unregistered++;
            continue;
        }

        for (const auto& member : family->members) {
            if (!member.variantNumber)
                continue;

            for (const auto& ref : member.uses) {
                const auto& path = member.paths.at(ref.pathIndex);
                if (snapshot.setModel(*ref.use->element.model, path)) {
                    Logger::trace(L"Pointed {} at {}", useLabel(ref.use->formKey), path.wstring());
                    retargeted++;
                } else {
                    kept++;
                }
            }
        }
    }

    Logger::info("Pointed {} record models at variant paths ({} keep another plugin's value, {} families have no "
                 "streams)",
                 retargeted,
                 kept,
                 unregistered);
}

Family& Plan::addFacegenFamily(const std::filesystem::path& path)
{
    auto family = std::make_unique<Family>();
    family->basePaths.push_back(path);
    family->isFacegen = true;

    // The dummy use the offline program patches a facegen head for: it bypasses the record type filter and disables
    // the TruePBR nif_filter.
    Family::Member member;
    member.attributes.isFacegen = true;
    member.attributes.isDummyUse = true;
    member.attributes.recType = PGPlugin::ModelRecordType::Unknown;
    member.paths.push_back(path);
    family->members.push_back(std::move(member));

    addTargets(*family);
    m_families.push_back(std::move(family));
    return *m_families.back();
}

Plan::Target* Plan::target(const std::string& path)
{
    const std::scoped_lock lock(m_targetsMutex);
    const auto it = m_targets.find(path);
    return it == m_targets.end() ? nullptr : &it->second;
}

std::size_t Plan::patchedFamilyCount() const
{
    std::size_t count = 0;
    for (const auto& family : m_families)
        if (family->state() == Family::State::Patched)
            count++;

    return count;
}

void Plan::addTargets(Family& family)
{
    const std::scoped_lock lock(m_targetsMutex);
    for (std::size_t member = 0; member < family.members.size(); member++) {
        const auto& paths = family.members.at(member).paths;
        for (std::size_t pathIndex = 0; pathIndex < paths.size(); pathIndex++) {
            m_targets[StringUtil::utf16toWindows1252(paths.at(pathIndex).wstring())]
                = { .family = &family, .member = member, .pathIndex = pathIndex };
        }
    }
}

}
