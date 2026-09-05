#include "PGRunCache.hpp"

#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGModManager.hpp"
#include "PGPatcher.hpp"
#include "PGPlugin.hpp"
#include "common/BethesdaDirectory.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/BinaryIO.hpp"
#include "util/FileUtil.hpp"
#include "util/HashUtil.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"
#include "util/TaskPoolRunner.hpp"
#include "util/TaskTracker.hpp"

#include <DirectXTex.h>
#include <spdlog/common.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

namespace {

//
// Serialization helpers
//

/**
 * @brief Interns wide strings so the cache file stores every distinct path only once.
 */
class StringTableBuilder {
private:
    unordered_map<wstring, uint32_t> m_ids;
    vector<wstring> m_strings;

public:
    auto id(const wstring& str) -> uint32_t
    {
        const auto it = m_ids.find(str);
        if (it != m_ids.end()) {
            return it->second;
        }

        const auto newId = static_cast<uint32_t>(m_strings.size());
        m_strings.push_back(str);
        m_ids.emplace(str, newId);
        return newId;
    }

    auto id(const string& str) -> uint32_t { return id(StringUtil::utf8toUTF16(str)); }

    auto id(const filesystem::path& path) -> uint32_t { return id(path.wstring()); }

    [[nodiscard]] auto strings() const -> const vector<wstring>& { return m_strings; }
};

/**
 * @brief Resolves string ids read from the cache file.
 */
class StringTable {
private:
    vector<wstring> m_strings;

public:
    explicit StringTable(vector<wstring> strings)
        : m_strings(std::move(strings))
    {
    }

    [[nodiscard]] auto get(uint32_t id) const -> const wstring&
    {
        if (id >= m_strings.size()) {
            throw runtime_error("Update cache: invalid string id");
        }
        return m_strings[id];
    }

    [[nodiscard]] auto getNarrow(uint32_t id) const -> string { return StringUtil::utf16toUTF8(get(id)); }
};

template <typename T> auto readCount(BinaryIO::Reader& reader) -> T
{
    const auto count = reader.read<uint32_t>();
    static constexpr uint32_t MAX_REASONABLE_COUNT = 50'000'000;
    if (count > MAX_REASONABLE_COUNT) {
        throw runtime_error("Update cache: unreasonable element count");
    }
    return static_cast<T>(count);
}

void writeIdentity(BinaryIO::Writer& w,
                   StringTableBuilder& st,
                   const BethesdaDirectory::FileIdentity& identity)
{
    w.write<uint8_t>(static_cast<uint8_t>(identity.kind));
    switch (identity.kind) {
    case BethesdaDirectory::FileIdentity::Kind::LOOSE:
        w.write<int64_t>(identity.mtime);
        w.write<uint64_t>(identity.size);
        break;
    case BethesdaDirectory::FileIdentity::Kind::BSA:
        w.write<uint32_t>(st.id(identity.bsaRelPath));
        w.write<int64_t>(identity.bsaMtime);
        w.write<uint64_t>(identity.bsaSize);
        break;
    default:
        break;
    }
}

auto readIdentity(BinaryIO::Reader& r,
                  const StringTable& st) -> BethesdaDirectory::FileIdentity
{
    BethesdaDirectory::FileIdentity identity;
    identity.kind = static_cast<BethesdaDirectory::FileIdentity::Kind>(r.read<uint8_t>());
    switch (identity.kind) {
    case BethesdaDirectory::FileIdentity::Kind::LOOSE:
        identity.mtime = r.read<int64_t>();
        identity.size = r.read<uint64_t>();
        break;
    case BethesdaDirectory::FileIdentity::Kind::BSA:
        identity.bsaRelPath = st.get(r.read<uint32_t>());
        identity.bsaMtime = r.read<int64_t>();
        identity.bsaSize = r.read<uint64_t>();
        break;
    case BethesdaDirectory::FileIdentity::Kind::NONE:
    case BethesdaDirectory::FileIdentity::Kind::GENERATED:
        break;
    default:
        throw runtime_error("Update cache: invalid file identity kind");
    }
    return identity;
}

void writeFormKey(BinaryIO::Writer& w,
                  StringTableBuilder& st,
                  const PGMeshPermutationTracker::FormKey& formKey)
{
    w.write<uint32_t>(st.id(formKey.modKey));
    w.write<uint32_t>(formKey.formID);
    w.write<uint32_t>(st.id(formKey.subMODL));
}

auto readFormKey(BinaryIO::Reader& r,
                 const StringTable& st) -> PGMeshPermutationTracker::FormKey
{
    PGMeshPermutationTracker::FormKey formKey;
    formKey.modKey = st.get(r.read<uint32_t>());
    formKey.formID = r.read<uint32_t>();
    formKey.subMODL = st.getNarrow(r.read<uint32_t>());
    return formKey;
}

void writeTextureSet(BinaryIO::Writer& w,
                     StringTableBuilder& st,
                     const PGTypes::TextureSet& slots)
{
    for (const auto& slot : slots) {
        w.write<uint32_t>(st.id(slot));
    }
}

auto readTextureSet(BinaryIO::Reader& r,
                    const StringTable& st) -> PGTypes::TextureSet
{
    PGTypes::TextureSet slots;
    for (auto& slot : slots) {
        slot = st.get(r.read<uint32_t>());
    }
    return slots;
}

void writeUses(BinaryIO::Writer& w,
               StringTableBuilder& st,
               const PGRunCache::MeshUses& uses)
{
    w.write<uint32_t>(static_cast<uint32_t>(uses.size()));
    for (const auto& [formKey, attrs] : uses) {
        writeFormKey(w, st, formKey);

        uint8_t flags = 0;
        flags |= attrs.isWeighted ? 1U : 0U;
        flags |= attrs.singlepassMATO ? 2U : 0U;
        flags |= attrs.isFacegen ? 4U : 0U;
        flags |= attrs.isIgnored ? 8U : 0U;
        flags |= attrs.isDummyUse ? 16U : 0U;
        w.write<uint8_t>(flags);
        w.write<uint8_t>(static_cast<uint8_t>(attrs.recType));

        w.write<uint32_t>(static_cast<uint32_t>(attrs.alternateTextures.size()));
        for (const auto& [slotIdx, texSet] : attrs.alternateTextures) {
            w.write<uint32_t>(slotIdx);
            writeTextureSet(w, st, texSet);
        }
    }
}

auto readUses(BinaryIO::Reader& r,
              const StringTable& st) -> PGRunCache::MeshUses
{
    PGRunCache::MeshUses uses;
    const auto count = readCount<size_t>(r);
    uses.reserve(count);
    for (size_t i = 0; i < count; i++) {
        auto formKey = readFormKey(r, st);

        PGPlugin::MeshUseAttributes attrs {};
        const auto flags = r.read<uint8_t>();
        attrs.isWeighted = (flags & 1U) != 0U;
        attrs.singlepassMATO = (flags & 2U) != 0U;
        attrs.isFacegen = (flags & 4U) != 0U;
        attrs.isIgnored = (flags & 8U) != 0U;
        attrs.isDummyUse = (flags & 16U) != 0U;
        attrs.recType = static_cast<PGPlugin::ModelRecordType>(r.read<uint8_t>());

        const auto altCount = readCount<size_t>(r);
        for (size_t j = 0; j < altCount; j++) {
            const auto slotIdx = r.read<uint32_t>();
            attrs.alternateTextures[slotIdx] = readTextureSet(r, st);
        }

        uses.emplace_back(std::move(formKey), std::move(attrs));
    }
    return uses;
}

void writeIntMap(BinaryIO::Writer& w,
                 const unordered_map<int,
                                     int>& map)
{
    w.write<uint32_t>(static_cast<uint32_t>(map.size()));
    for (const auto& [key, value] : map) {
        w.write<int32_t>(key);
        w.write<int32_t>(value);
    }
}

auto readIntMap(BinaryIO::Reader& r) -> unordered_map<int,
                                                      int>
{
    unordered_map<int, int> map;
    const auto count = readCount<size_t>(r);
    for (size_t i = 0; i < count; i++) {
        const auto key = r.read<int32_t>();
        const auto value = r.read<int32_t>();
        map[key] = value;
    }
    return map;
}

void writeMeshResult(BinaryIO::Writer& w,
                     StringTableBuilder& st,
                     const PGMeshPermutationTracker::MeshResult& result)
{
    w.write<uint32_t>(st.id(result.meshPath));

    w.write<uint32_t>(static_cast<uint32_t>(result.altTexResults.size()));
    for (const auto& [formKey, altTexMap] : result.altTexResults) {
        writeFormKey(w, st, formKey);
        w.write<uint32_t>(static_cast<uint32_t>(altTexMap.size()));
        for (const auto& [slotIdx, texSet] : altTexMap) {
            w.write<uint32_t>(slotIdx);
            writeTextureSet(w, st, texSet);
        }
    }

    writeIntMap(w, result.idxCorrections);
    writeIntMap(w, result.inverseIdxCorrectionsPatching);
}

auto readMeshResult(BinaryIO::Reader& r,
                    const StringTable& st) -> PGMeshPermutationTracker::MeshResult
{
    PGMeshPermutationTracker::MeshResult result;
    result.meshPath = st.get(r.read<uint32_t>());

    const auto altCount = readCount<size_t>(r);
    for (size_t i = 0; i < altCount; i++) {
        auto formKey = readFormKey(r, st);
        unordered_map<unsigned int, PGTypes::TextureSet> altTexMap;
        const auto mapCount = readCount<size_t>(r);
        for (size_t j = 0; j < mapCount; j++) {
            const auto slotIdx = r.read<uint32_t>();
            altTexMap[slotIdx] = readTextureSet(r, st);
        }
        result.altTexResults.emplace_back(std::move(formKey), std::move(altTexMap));
    }

    result.idxCorrections = readIntMap(r);
    result.inverseIdxCorrectionsPatching = readIntMap(r);
    return result;
}

void writeStringList(BinaryIO::Writer& w,
                     StringTableBuilder& st,
                     const vector<string>& list)
{
    w.write<uint32_t>(static_cast<uint32_t>(list.size()));
    for (const auto& str : list) {
        w.write<uint32_t>(st.id(str));
    }
}

auto readStringList(BinaryIO::Reader& r,
                    const StringTable& st) -> vector<string>
{
    vector<string> list;
    const auto count = readCount<size_t>(r);
    list.reserve(count);
    for (size_t i = 0; i < count; i++) {
        list.push_back(st.getNarrow(r.read<uint32_t>()));
    }
    return list;
}

void writeMeta(BinaryIO::Writer& w,
               StringTableBuilder& st,
               const PGRunCache::MeshMetaRecord& meta)
{
    writeStringList(w, st, meta.globalPatchersApplied);

    w.write<uint32_t>(static_cast<uint32_t>(meta.formKeys.size()));
    for (const auto& formKey : meta.formKeys) {
        writeFormKey(w, st, formKey);
    }

    w.write<uint32_t>(static_cast<uint32_t>(meta.shapeMeta.size()));
    for (const auto& [idx, shapeMeta] : meta.shapeMeta) {
        w.write<uint64_t>(idx);
        w.write<uint32_t>(shapeMeta.blockID);
        w.write<uint32_t>(st.id(shapeMeta.shapeName));
        writeStringList(w, st, shapeMeta.prePatchersApplied);
        writeStringList(w, st, shapeMeta.postPatchersApplied);

        w.write<uint32_t>(static_cast<uint32_t>(shapeMeta.matches.size()));
        for (const auto& [formKey, matchMetas] : shapeMeta.matches) {
            writeFormKey(w, st, formKey);
            w.write<uint32_t>(static_cast<uint32_t>(matchMetas.size()));
            for (const auto& matchMeta : matchMetas) {
                w.write<uint32_t>(st.id(matchMeta.modName));
                w.write<uint8_t>(static_cast<uint8_t>(matchMeta.shader));
                w.write<uint8_t>(static_cast<uint8_t>(matchMeta.shaderTransformTo));
                w.write<uint32_t>(st.id(matchMeta.matchedPath));
                w.write<uint32_t>(static_cast<uint32_t>(matchMeta.resultTextureMods.size()));
                for (const auto& [slot, modName] : matchMeta.resultTextureMods) {
                    w.write<uint8_t>(static_cast<uint8_t>(slot));
                    w.write<uint32_t>(st.id(modName));
                }
            }
        }
    }
}

auto readMeta(BinaryIO::Reader& r,
              const StringTable& st) -> PGRunCache::MeshMetaRecord
{
    PGRunCache::MeshMetaRecord meta;
    meta.globalPatchersApplied = readStringList(r, st);

    const auto formKeyCount = readCount<size_t>(r);
    meta.formKeys.reserve(formKeyCount);
    for (size_t i = 0; i < formKeyCount; i++) {
        meta.formKeys.push_back(readFormKey(r, st));
    }

    const auto shapeCount = readCount<size_t>(r);
    for (size_t i = 0; i < shapeCount; i++) {
        const auto idx = static_cast<size_t>(r.read<uint64_t>());
        PGRunCache::MeshShapeMetaRecord shapeMeta;
        shapeMeta.blockID = r.read<uint32_t>();
        shapeMeta.shapeName = st.getNarrow(r.read<uint32_t>());
        shapeMeta.prePatchersApplied = readStringList(r, st);
        shapeMeta.postPatchersApplied = readStringList(r, st);

        const auto matchGroupCount = readCount<size_t>(r);
        for (size_t j = 0; j < matchGroupCount; j++) {
            auto formKey = readFormKey(r, st);
            vector<PGRunCache::MatchMetaRecord> matchMetas;
            const auto matchCount = readCount<size_t>(r);
            matchMetas.reserve(matchCount);
            for (size_t k = 0; k < matchCount; k++) {
                PGRunCache::MatchMetaRecord matchMeta;
                matchMeta.modName = st.get(r.read<uint32_t>());
                matchMeta.shader = static_cast<PGEnums::ShapeShader>(r.read<uint8_t>());
                matchMeta.shaderTransformTo = static_cast<PGEnums::ShapeShader>(r.read<uint8_t>());
                matchMeta.matchedPath = st.get(r.read<uint32_t>());
                const auto resultModCount = readCount<size_t>(r);
                for (size_t m = 0; m < resultModCount; m++) {
                    const auto slot = static_cast<PGEnums::TextureSlots>(r.read<uint8_t>());
                    matchMeta.resultTextureMods.emplace_back(slot, st.get(r.read<uint32_t>()));
                }
                matchMetas.push_back(std::move(matchMeta));
            }
            shapeMeta.matches.emplace_back(std::move(formKey), std::move(matchMetas));
        }

        meta.shapeMeta.emplace_back(idx, std::move(shapeMeta));
    }

    return meta;
}

void writeRecord(BinaryIO::Writer& w,
                 StringTableBuilder& st,
                 const PGRunCache::MeshRecord& record)
{
    writeUses(w, st, record.uses);

    w.write<uint32_t>(static_cast<uint32_t>(record.fileIdentityDeps.size()));
    for (const auto& [path, identity] : record.fileIdentityDeps) {
        w.write<uint32_t>(st.id(path));
        writeIdentity(w, st, identity);
    }

    w.write<uint32_t>(static_cast<uint32_t>(record.fileExistsDeps.size()));
    for (const auto& [path, state] : record.fileExistsDeps) {
        w.write<uint32_t>(st.id(path));
        w.write<uint8_t>(static_cast<uint8_t>(state));
    }

    w.write<uint32_t>(static_cast<uint32_t>(record.textureTypeDeps.size()));
    for (const auto& [path, type] : record.textureTypeDeps) {
        w.write<uint32_t>(st.id(path));
        w.write<uint8_t>(static_cast<uint8_t>(type));
    }

    w.write<uint32_t>(static_cast<uint32_t>(record.textureAttributeDeps.size()));
    for (const auto& [path, attribute, value] : record.textureAttributeDeps) {
        w.write<uint32_t>(st.id(path));
        w.write<uint8_t>(static_cast<uint8_t>(attribute));
        w.writeBool(value);
    }

    w.write<uint32_t>(static_cast<uint32_t>(record.textureAttributesDeps.size()));
    for (const auto& [path, mask] : record.textureAttributesDeps) {
        w.write<uint32_t>(st.id(path));
        w.write<uint8_t>(mask);
    }

    w.write<uint32_t>(static_cast<uint32_t>(record.texMatchDeps.size()));
    for (const auto& [base, type, hash] : record.texMatchDeps) {
        w.write<uint32_t>(st.id(base));
        w.write<uint8_t>(static_cast<uint8_t>(type));
        w.write<uint64_t>(hash);
    }

    w.write<uint32_t>(static_cast<uint32_t>(record.modOfFileDeps.size()));
    for (const auto& [path, modName] : record.modOfFileDeps) {
        w.write<uint32_t>(st.id(path));
        w.write<uint32_t>(st.id(modName));
    }

    w.write<uint32_t>(static_cast<uint32_t>(record.modStateDeps.size()));
    for (const auto& [modName, enabled, ignored] : record.modStateDeps) {
        w.write<uint32_t>(st.id(modName));
        w.writeBool(enabled);
        w.writeBool(ignored);
    }

    w.write<uint32_t>(static_cast<uint32_t>(record.matchesDeps.size()));
    for (const auto& dep : record.matchesDeps) {
        writeTextureSet(w, st, dep.slots);
        w.writeBool(dep.singlepassMATO);
        w.write<uint8_t>(static_cast<uint8_t>(dep.recType));
        w.write<uint64_t>(dep.digest);
    }

    w.write<uint32_t>(static_cast<uint32_t>(record.outputFiles.size()));
    for (const auto& [path, size] : record.outputFiles) {
        w.write<uint32_t>(st.id(path));
        w.write<uint64_t>(size);
    }

    w.write<uint32_t>(static_cast<uint32_t>(record.meshResults.size()));
    for (const auto& result : record.meshResults) {
        writeMeshResult(w, st, result);
    }

    w.writeBool(record.hasDiff);
    w.write<uint64_t>(record.crc32Original);
    w.write<uint64_t>(record.crc32Patched);

    writeMeta(w, st, record.meta);

    w.write<uint32_t>(static_cast<uint32_t>(record.hookRegistrations.size()));
    for (const auto& [kind, tex] : record.hookRegistrations) {
        w.write<uint8_t>(static_cast<uint8_t>(kind));
        w.write<uint32_t>(st.id(tex));
    }

    w.write<uint32_t>(static_cast<uint32_t>(record.messages.size()));
    for (const auto& message : record.messages) {
        w.write<uint8_t>(static_cast<uint8_t>(message.level));
        w.write<uint32_t>(st.id(message.text));
    }
}

auto readRecord(BinaryIO::Reader& r,
                const StringTable& st) -> PGRunCache::MeshRecord
{
    PGRunCache::MeshRecord record;
    record.uses = readUses(r, st);

    auto count = readCount<size_t>(r);
    record.fileIdentityDeps.reserve(count);
    for (size_t i = 0; i < count; i++) {
        auto path = st.get(r.read<uint32_t>());
        auto identity = readIdentity(r, st);
        record.fileIdentityDeps.emplace_back(std::move(path), std::move(identity));
    }

    count = readCount<size_t>(r);
    record.fileExistsDeps.reserve(count);
    for (size_t i = 0; i < count; i++) {
        auto path = st.get(r.read<uint32_t>());
        const auto state = static_cast<PGRunCache::FileExistsState>(r.read<uint8_t>());
        record.fileExistsDeps.emplace_back(std::move(path), state);
    }

    count = readCount<size_t>(r);
    record.textureTypeDeps.reserve(count);
    for (size_t i = 0; i < count; i++) {
        auto path = st.get(r.read<uint32_t>());
        const auto type = static_cast<PGEnums::TextureType>(r.read<uint8_t>());
        record.textureTypeDeps.emplace_back(std::move(path), type);
    }

    count = readCount<size_t>(r);
    record.textureAttributeDeps.reserve(count);
    for (size_t i = 0; i < count; i++) {
        auto path = st.get(r.read<uint32_t>());
        const auto attribute = static_cast<PGEnums::TextureAttribute>(r.read<uint8_t>());
        const auto value = r.readBool();
        record.textureAttributeDeps.emplace_back(std::move(path), attribute, value);
    }

    count = readCount<size_t>(r);
    record.textureAttributesDeps.reserve(count);
    for (size_t i = 0; i < count; i++) {
        auto path = st.get(r.read<uint32_t>());
        const auto mask = r.read<uint8_t>();
        record.textureAttributesDeps.emplace_back(std::move(path), mask);
    }

    count = readCount<size_t>(r);
    record.texMatchDeps.reserve(count);
    for (size_t i = 0; i < count; i++) {
        auto base = st.get(r.read<uint32_t>());
        const auto type = static_cast<PGEnums::TextureType>(r.read<uint8_t>());
        const auto hash = r.read<uint64_t>();
        record.texMatchDeps.emplace_back(std::move(base), type, hash);
    }

    count = readCount<size_t>(r);
    record.modOfFileDeps.reserve(count);
    for (size_t i = 0; i < count; i++) {
        auto path = st.get(r.read<uint32_t>());
        auto modName = st.get(r.read<uint32_t>());
        record.modOfFileDeps.emplace_back(std::move(path), std::move(modName));
    }

    count = readCount<size_t>(r);
    record.modStateDeps.reserve(count);
    for (size_t i = 0; i < count; i++) {
        auto modName = st.get(r.read<uint32_t>());
        const auto enabled = r.readBool();
        const auto ignored = r.readBool();
        record.modStateDeps.emplace_back(std::move(modName), enabled, ignored);
    }

    count = readCount<size_t>(r);
    record.matchesDeps.reserve(count);
    for (size_t i = 0; i < count; i++) {
        PGRunCache::MatchesDep dep;
        dep.slots = readTextureSet(r, st);
        dep.singlepassMATO = r.readBool();
        dep.recType = static_cast<PGPlugin::ModelRecordType>(r.read<uint8_t>());
        dep.digest = r.read<uint64_t>();
        record.matchesDeps.push_back(std::move(dep));
    }

    count = readCount<size_t>(r);
    record.outputFiles.reserve(count);
    for (size_t i = 0; i < count; i++) {
        auto path = st.get(r.read<uint32_t>());
        const auto size = r.read<uint64_t>();
        record.outputFiles.emplace_back(std::move(path), size);
    }

    count = readCount<size_t>(r);
    record.meshResults.reserve(count);
    for (size_t i = 0; i < count; i++) {
        record.meshResults.push_back(readMeshResult(r, st));
    }

    record.hasDiff = r.readBool();
    record.crc32Original = r.read<uint64_t>();
    record.crc32Patched = r.read<uint64_t>();

    record.meta = readMeta(r, st);

    count = readCount<size_t>(r);
    record.hookRegistrations.reserve(count);
    for (size_t i = 0; i < count; i++) {
        const auto kind = static_cast<PGRunCache::HookKind>(r.read<uint8_t>());
        auto tex = st.get(r.read<uint32_t>());
        record.hookRegistrations.emplace_back(kind, std::move(tex));
    }

    count = readCount<size_t>(r);
    record.messages.reserve(count);
    for (size_t i = 0; i < count; i++) {
        PGRunCache::Message message;
        message.level = static_cast<spdlog::level::level_enum>(r.read<uint8_t>());
        message.text = st.get(r.read<uint32_t>());
        record.messages.push_back(std::move(message));
    }

    return record;
}

void writeTexMetadata(BinaryIO::Writer& w,
                      const DirectX::TexMetadata& meta)
{
    w.write<uint64_t>(meta.width);
    w.write<uint64_t>(meta.height);
    w.write<uint64_t>(meta.depth);
    w.write<uint64_t>(meta.arraySize);
    w.write<uint64_t>(meta.mipLevels);
    w.write<uint32_t>(meta.miscFlags);
    w.write<uint32_t>(meta.miscFlags2);
    w.write<uint32_t>(static_cast<uint32_t>(meta.format));
    w.write<uint32_t>(static_cast<uint32_t>(meta.dimension));
}

auto readTexMetadata(BinaryIO::Reader& r) -> DirectX::TexMetadata
{
    DirectX::TexMetadata meta {};
    meta.width = static_cast<size_t>(r.read<uint64_t>());
    meta.height = static_cast<size_t>(r.read<uint64_t>());
    meta.depth = static_cast<size_t>(r.read<uint64_t>());
    meta.arraySize = static_cast<size_t>(r.read<uint64_t>());
    meta.mipLevels = static_cast<size_t>(r.read<uint64_t>());
    meta.miscFlags = r.read<uint32_t>();
    meta.miscFlags2 = r.read<uint32_t>();
    meta.format = static_cast<DXGI_FORMAT>(r.read<uint32_t>());
    meta.dimension = static_cast<DirectX::TEX_DIMENSION>(r.read<uint32_t>());
    return meta;
}

auto matchesDepKey(const PGTypes::TextureSet& slots,
                   bool singlepassMATO,
                   const PGPlugin::ModelRecordType& recType) -> wstring
{
    wstring key;
    for (const auto& slot : slots) {
        key += slot;
        key += L'|';
    }
    key += singlepassMATO ? L'1' : L'0';
    key += L'|';
    key += to_wstring(static_cast<int>(recType));
    return key;
}

} // namespace

//
// Helpers
//

auto PGRunCache::pathKey(const wstring& path) -> wstring
{
    auto key = StringUtil::toLowerASCIIFast(path);
    std::ranges::replace(key, L'/', L'\\');
    return key;
}

auto PGRunCache::pathKey(const filesystem::path& path) -> wstring { return pathKey(path.wstring()); }

auto PGRunCache::hookOutputPath(const HookKind& kind,
                                const filesystem::path& texPath) -> filesystem::path
{
    switch (kind) {
    case HookKind::CONVERT_TO_CM:
        return PatcherTextureHookConvertToCM::getOutputFilename(texPath);
    case HookKind::FIX_SSS:
        return PatcherTextureHookFixSSS::getOutputFilename(texPath);
    }

    return {};
}

auto PGRunCache::hashTexMatchResults(const vector<PGTypes::PGTexture>& results) -> uint64_t
{
    vector<pair<wstring, PGEnums::TextureType>> sorted;
    sorted.reserve(results.size());
    for (const auto& result : results) {
        sorted.emplace_back(pathKey(result.path), result.type);
    }
    std::ranges::sort(sorted);

    HashUtil::Fnv1a64 hasher;
    hasher.add(static_cast<uint64_t>(sorted.size()));
    for (const auto& [path, type] : sorted) {
        hasher.add(path);
        hasher.add(type);
    }
    return hasher.value();
}

auto PGRunCache::attributesToMask(const unordered_set<PGEnums::TextureAttribute>& attributes) -> uint8_t
{
    uint8_t mask = 0;
    for (const auto& attribute : attributes) {
        mask |= static_cast<uint8_t>(1U << static_cast<unsigned>(attribute));
    }
    return mask;
}

void PGRunCache::captureLogMessage(spdlog::level::level_enum level,
                                   const wstring& message)
{
    if (s_activeRecorder != nullptr) {
        s_activeRecorder->recordMessage(level, message);
    }
}

//
// MeshRecorder
//

PGRunCache::MeshRecorder::MeshRecorder(filesystem::path nifPath)
    : m_nifPath(std::move(nifPath))
{
    if (s_activeRecorder != nullptr) {
        throw runtime_error("Nested mesh recorders are not supported");
    }

    s_activeRecorder = this;
    s_suspendDepth = 0;
    BethesdaDirectory::setThreadFileQueryObserver(this);
    Logger::setThreadMessageCapture(&PGRunCache::captureLogMessage);
}

PGRunCache::MeshRecorder::~MeshRecorder()
{
    Logger::setThreadMessageCapture(nullptr);
    BethesdaDirectory::setThreadFileQueryObserver(nullptr);
    s_activeRecorder = nullptr;
    s_suspendDepth = 0;
}

void PGRunCache::MeshRecorder::onIsFile(const filesystem::path& relPath,
                                        bool exists,
                                        bool generated)
{
    if (s_suspendDepth > 0) {
        return;
    }

    auto state = FileExistsState::MISSING;
    if (exists) {
        state = generated ? FileExistsState::GENERATED : FileExistsState::EXISTS;
    }

    // Dependencies store the path exactly as it was queried so evaluation replays the same lookup; the normalized
    // key is only used to avoid recording the same lookup twice
    const auto key = pathKey(relPath) + L'|' + to_wstring(static_cast<int>(state));
    if (m_existsDeps.insert(key).second) {
        m_record.fileExistsDeps.emplace_back(relPath.wstring(), state);
    }
}

void PGRunCache::MeshRecorder::onGetFile(const filesystem::path& relPath,
                                         const BethesdaDirectory::FileIdentity& identity)
{
    if (s_suspendDepth > 0) {
        return;
    }

    if (m_identityDeps.emplace(pathKey(relPath), identity).second) {
        m_record.fileIdentityDeps.emplace_back(relPath.wstring(), identity);
    }
}

void PGRunCache::MeshRecorder::recordTextureType(const filesystem::path& path,
                                                 const PGEnums::TextureType& type)
{
    if (m_textureTypeDeps.emplace(pathKey(path), type).second) {
        m_record.textureTypeDeps.emplace_back(path.wstring(), type);
    }
}

void PGRunCache::MeshRecorder::recordTextureAttribute(const filesystem::path& path,
                                                      const PGEnums::TextureAttribute& attribute,
                                                      bool value)
{
    if (m_textureAttributeDeps.insert(pathKey(path) + L'|' + to_wstring(static_cast<int>(attribute))).second) {
        m_record.textureAttributeDeps.emplace_back(path.wstring(), attribute, value);
    }
}

void PGRunCache::MeshRecorder::recordTextureAttributes(const filesystem::path& path,
                                                       const unordered_set<PGEnums::TextureAttribute>& attributes)
{
    const auto mask = attributesToMask(attributes);
    if (m_textureAttributesDeps.emplace(pathKey(path), mask).second) {
        m_record.textureAttributesDeps.emplace_back(path.wstring(), mask);
    }
}

void PGRunCache::MeshRecorder::recordTexMatch(const wstring& base,
                                              const PGEnums::TextureType& type,
                                              const vector<PGTypes::PGTexture>& results)
{
    if (m_texMatchDeps.insert(pathKey(base) + L'|' + to_wstring(static_cast<int>(type))).second) {
        m_record.texMatchDeps.emplace_back(base, type, hashTexMatchResults(results));
    }
}

void PGRunCache::MeshRecorder::recordModOfFile(const filesystem::path& path,
                                               const wstring& modName)
{
    if (m_modOfFileDeps.emplace(pathKey(path), modName).second) {
        m_record.modOfFileDeps.emplace_back(path.wstring(), modName);
    }
}

void PGRunCache::MeshRecorder::recordModState(const wstring& modName,
                                              bool isEnabled,
                                              bool areMeshesIgnored)
{
    if (m_modStateDeps.emplace(modName, make_pair(isEnabled, areMeshesIgnored)).second) {
        m_record.modStateDeps.emplace_back(modName, isEnabled, areMeshesIgnored);
    }
}

void PGRunCache::MeshRecorder::recordMatches(const PGTypes::TextureSet& slots,
                                             bool singlepassMATO,
                                             const PGPlugin::ModelRecordType& recType,
                                             uint64_t digest)
{
    if (m_matchesDeps.insert(matchesDepKey(slots, singlepassMATO, recType)).second) {
        m_record.matchesDeps.push_back(
            {.slots = slots, .singlepassMATO = singlepassMATO, .recType = recType, .digest = digest});
    }
}

void PGRunCache::MeshRecorder::recordHookRegistration(const HookKind& kind,
                                                      const filesystem::path& texPath)
{
    const auto key = pathKey(texPath);
    if (m_hookRegistrations.insert(key + L'|' + to_wstring(static_cast<int>(kind))).second) {
        m_record.hookRegistrations.emplace_back(kind, key);
    }
}

void PGRunCache::MeshRecorder::recordOutputFile(const filesystem::path& relPath,
                                                uint64_t size)
{
    m_record.outputFiles.emplace_back(pathKey(relPath), size);
}

void PGRunCache::MeshRecorder::recordMessage(const spdlog::level::level_enum& level,
                                             const wstring& message)
{
    if (level == spdlog::level::critical) {
        // A mesh that produced a critical error must never be considered successfully patched
        m_valid = false;
        return;
    }

    if (level != spdlog::level::warn && level != spdlog::level::err) {
        return;
    }

    m_record.messages.push_back({.level = level, .text = message});
}

void PGRunCache::MeshRecorder::setUses(const MeshUses& uses) { m_record.uses = uses; }

void PGRunCache::MeshRecorder::setMeshResults(const vector<PGMeshPermutationTracker::MeshResult>& results)
{
    m_record.meshResults = results;
}

void PGRunCache::MeshRecorder::setDiff(uint64_t crc32Original,
                                       uint64_t crc32Patched)
{
    m_record.hasDiff = true;
    m_record.crc32Original = crc32Original;
    m_record.crc32Patched = crc32Patched;
}

void PGRunCache::MeshRecorder::setMeta(const PGPatcher::MeshMeta& meta)
{
    MeshMetaRecord record;
    record.globalPatchersApplied = meta.globalPatchersApplied;
    record.formKeys = meta.formKeys;

    for (const auto& [idx, shapeMeta] : meta.shapeMeta) {
        MeshShapeMetaRecord shapeRecord;
        shapeRecord.blockID = shapeMeta.blockID;
        shapeRecord.shapeName = shapeMeta.shapeName;
        shapeRecord.prePatchersApplied = shapeMeta.prePatchersApplied;
        shapeRecord.postPatchersApplied = shapeMeta.postPatchersApplied;

        // Keep a deterministic order for the match groups
        vector<PGMeshPermutationTracker::FormKey> formKeys;
        formKeys.reserve(shapeMeta.matches.size());
        for (const auto& [formKey, matchMetas] : shapeMeta.matches) {
            formKeys.push_back(formKey);
        }
        std::ranges::sort(formKeys,
                          [](const PGMeshPermutationTracker::FormKey& a,
                             const PGMeshPermutationTracker::FormKey& b) -> bool { return a < b; });

        for (const auto& formKey : formKeys) {
            vector<MatchMetaRecord> matchRecords;
            for (const auto& matchMeta : shapeMeta.matches.at(formKey)) {
                MatchMetaRecord matchRecord;
                matchRecord.modName = matchMeta.mod == nullptr ? L"" : matchMeta.mod->name;
                matchRecord.shader = matchMeta.shader;
                matchRecord.shaderTransformTo = matchMeta.shaderTransformTo;
                matchRecord.matchedPath = matchMeta.matchedPath;
                for (const auto& [slot, slotMod] : matchMeta.resultTextureMods) {
                    if (slotMod == nullptr) {
                        continue;
                    }
                    matchRecord.resultTextureMods.emplace_back(slot, slotMod->name);
                }
                matchRecords.push_back(std::move(matchRecord));
            }
            shapeRecord.matches.emplace_back(formKey, std::move(matchRecords));
        }

        record.shapeMeta.emplace_back(idx, std::move(shapeRecord));
    }

    m_record.meta = std::move(record);
}

void PGRunCache::MeshRecorder::commit()
{
    if (m_committed || !m_valid || !s_enabled) {
        return;
    }

    m_committed = true;

    const lock_guard<mutex> lock(s_runMutex);
    s_currentRecords[pathKey(m_nifPath)] = m_record;
}

//
// SuspendRecording
//

PGRunCache::SuspendRecording::SuspendRecording() { s_suspendDepth++; }

PGRunCache::SuspendRecording::~SuspendRecording()
{
    if (s_suspendDepth > 0) {
        s_suspendDepth--;
    }
}

//
// Lifecycle
//

void PGRunCache::initialize(const filesystem::path& cacheFile,
                            bool enabled,
                            bool ignoreExisting)
{
    s_cacheFile = cacheFile;
    s_enabled = enabled;
    s_previous.reset();
    s_configFingerprint = 0;
    s_pluginFingerprint = 0;

    {
        const unique_lock lock(s_sessionMutex);
        s_sessionVotes.clear();
        s_sessionCM.clear();
    }

    beginRun();

    if (!enabled) {
        Logger::debug("Update cache disabled for this run (zip output)");
        return;
    }

    error_code ec;
    const bool cacheExists = filesystem::exists(cacheFile, ec);

    if (ignoreExisting) {
        if (cacheExists) {
            Logger::info("Regenerating the output from scratch, the previous output in the output directory is "
                         "replaced (use Update Output to keep unchanged meshes)");
        } else {
            Logger::info("Generating the output from scratch");
        }
        return;
    }

    if (!cacheExists) {
        // An update was requested (Update Output button or --autostart-update) but there is nothing to update
        Logger::warn("No previous output found in the output directory, generating the output from scratch instead of "
                     "updating it");
        return;
    }

    s_previous = loadFromFile(cacheFile);
    if (s_previous == nullptr) {
        Logger::warn("The update cache in the output directory could not be used, performing a full run");
        return;
    }

    Logger::info("Previous output found in the output directory, it will be updated ({} meshes, {} textures known)",
                 s_previous->meshRecords.size(),
                 s_previous->textures.size());
}

auto PGRunCache::isEnabled() -> bool { return s_enabled; }

auto PGRunCache::hasPreviousRun() -> bool { return s_enabled && s_previous != nullptr; }

auto PGRunCache::arePreviousRecordsValid() -> bool
{
    return hasPreviousRun() && s_previous->configFingerprint == s_configFingerprint;
}

auto PGRunCache::arePreviousMeshUsesValid() -> bool
{
    return hasPreviousRun() && s_pluginFingerprint != 0 && s_previous->pluginFingerprint == s_pluginFingerprint;
}

void PGRunCache::setConfigFingerprint(uint64_t fingerprint)
{
    s_configFingerprint = fingerprint;

    if (hasPreviousRun() && s_previous->configFingerprint != fingerprint) {
        Logger::info("Patching configuration changed since the previous run, all meshes will be re-patched");
    }
}

void PGRunCache::setPluginFingerprint(uint64_t fingerprint)
{
    s_pluginFingerprint = fingerprint;

    if (hasPreviousRun()) {
        if (s_previous->pluginFingerprint == fingerprint) {
            Logger::debug("Plugin load order unchanged since the previous run, reusing cached mesh uses");
        } else {
            Logger::info("Plugin load order changed since the previous run, mesh uses will be re-read from plugins");
        }
    }
}

void PGRunCache::seedTextureMetadata()
{
    if (!hasPreviousRun()) {
        return;
    }

    auto* const pgd = PGGlobals::getPGD();
    auto* const pgd3d = PGGlobals::getPGD3D();

    size_t seeded = 0;
    for (const auto& [texture, info] : s_previous->textures) {
        if (!info.hasMeta) {
            continue;
        }

        const auto identity = pgd->getFileIdentity(texture);
        if (identity.kind == BethesdaDirectory::FileIdentity::Kind::NONE || identity != info.identity) {
            continue;
        }

        pgd3d->seedDDSMetadata(texture, info.meta);
        seeded++;
    }

    Logger::debug("Update cache: seeded metadata for {} unchanged textures", seeded);
}

void PGRunCache::addProtectedOutput(const filesystem::path& relPath)
{
    const lock_guard<mutex> lock(s_runMutex);
    s_protectedOutputs.insert(pathKey(relPath));
}

void PGRunCache::beginRun()
{
    const lock_guard<mutex> lock(s_runMutex);
    s_currentRecords.clear();
    s_currentHookOutputs.clear();
    s_reusedHooks.clear();
    s_outputSnapshot.clear();
    s_skippable.clear();
    s_protectedOutputs.clear();
}

auto PGRunCache::outputRoot() -> filesystem::path
{
    // A trailing separator on the configured output directory would break relative path computation
    auto root = PGGlobals::getPGD()->getGeneratedPath().lexically_normal();
    if (root.filename().empty() && root.has_parent_path()) {
        root = root.parent_path();
    }
    return root;
}

void PGRunCache::snapshotOutputDirectory()
{
    const lock_guard<mutex> lock(s_runMutex);
    s_outputSnapshot.clear();

    const auto generatedPath = outputRoot();
    for (const auto& folder : {L"meshes", L"textures"}) {
        const auto folderPath = generatedPath / folder;
        error_code ec;
        if (!filesystem::is_directory(folderPath, ec)) {
            continue;
        }

        for (auto it = filesystem::recursive_directory_iterator(
                 folderPath, filesystem::directory_options::skip_permission_denied, ec);
             it != filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }

            const auto& entry = *it;
            if (!entry.is_regular_file(ec)) {
                ec.clear();
                continue;
            }

            const auto relPath = entry.path().lexically_relative(generatedPath);
            const auto size = entry.file_size(ec);
            if (ec) {
                ec.clear();
                continue;
            }

            s_outputSnapshot[pathKey(relPath)] = size;
        }
    }

    Logger::debug("Update cache: {} files currently in the output directory", s_outputSnapshot.size());
}

auto PGRunCache::finishRun(bool save) -> bool
{
    if (!s_enabled) {
        return true;
    }

    auto* const pgd = PGGlobals::getPGD();
    auto* const pgd3d = PGGlobals::getPGD3D();

    auto data = make_unique<CacheData>();
    data->pgVersion = PG_FULL_VERSION;
    data->configFingerprint = s_configFingerprint;
    data->pluginFingerprint = s_pluginFingerprint;

    // Textures: identity + classification + metadata
    const auto metadataCache = pgd3d->getDDSMetadataCacheSnapshot();
    {
        const shared_lock sessionLock(s_sessionMutex);
        for (const auto& texture : pgd->getTextures()) {
            const auto key = pathKey(texture);
            TextureInfo info;
            info.identity = pgd->getFileIdentity(texture);
            if (info.identity.kind == BethesdaDirectory::FileIdentity::Kind::NONE
                || info.identity.kind == BethesdaDirectory::FileIdentity::Kind::GENERATED) {
                continue;
            }

            const auto cmIt = s_sessionCM.find(key);
            if (cmIt != s_sessionCM.end() && cmIt->second.first == info.identity) {
                info.hasCM = true;
                info.cm = cmIt->second.second;
            }

            const auto metaIt = metadataCache.find(texture);
            if (metaIt != metadataCache.end()) {
                info.hasMeta = true;
                info.meta = metaIt->second;
            }

            if (!info.hasCM && !info.hasMeta) {
                // nothing worth caching for this texture
                continue;
            }

            data->textures.emplace(key, std::move(info));
        }

        // Meshes: votes + uses
        for (const auto& [mesh, nifCache] : pgd->getMeshes()) {
            const auto key = pathKey(mesh);

            const auto votesIt = s_sessionVotes.find(key);
            if (votesIt != s_sessionVotes.end()) {
                data->meshVotes.emplace(key, votesIt->second);
            }

            data->meshUses.emplace(key, nifCache.meshUses);
        }
    }

    {
        const lock_guard<mutex> runLock(s_runMutex);
        data->hookOutputs = std::move(s_currentHookOutputs);
        data->meshRecords = std::move(s_currentRecords);
        s_currentHookOutputs.clear();
        s_currentRecords.clear();
        s_reusedHooks.clear();
        s_skippable.clear();
        s_outputSnapshot.clear();
    }

    bool success = true;
    if (save) {
        success = saveToFile(s_cacheFile, *data);
        if (success) {
            Logger::info(L"Saved update cache ({} meshes) to {}", data->meshRecords.size(), s_cacheFile.wstring());
        } else {
            Logger::warn(L"Failed to save update cache to {}", s_cacheFile.wstring());
        }
    }

    s_previous = std::move(data);
    return success;
}

void PGRunCache::discard()
{
    s_previous.reset();

    error_code ec;
    if (!s_cacheFile.empty() && filesystem::exists(s_cacheFile, ec)) {
        filesystem::remove(s_cacheFile, ec);
    }
}

//
// Classification caches
//

auto PGRunCache::tryGetCachedMeshVotes(const filesystem::path& nifPath,
                                       const BethesdaDirectory::FileIdentity& identity,
                                       vector<TextureVote>& votes) -> bool
{
    if (!hasPreviousRun() || identity.kind == BethesdaDirectory::FileIdentity::Kind::NONE
        || identity.kind == BethesdaDirectory::FileIdentity::Kind::GENERATED) {
        return false;
    }

    const auto key = pathKey(nifPath);
    const auto it = s_previous->meshVotes.find(key);
    if (it == s_previous->meshVotes.end() || it->second.identity != identity) {
        return false;
    }

    votes = it->second.votes;

    const unique_lock lock(s_sessionMutex);
    s_sessionVotes[key] = it->second;
    return true;
}

void PGRunCache::storeMeshVotes(const filesystem::path& nifPath,
                                const BethesdaDirectory::FileIdentity& identity,
                                const vector<TextureVote>& votes)
{
    if (!s_enabled || identity.kind == BethesdaDirectory::FileIdentity::Kind::NONE
        || identity.kind == BethesdaDirectory::FileIdentity::Kind::GENERATED) {
        return;
    }

    const unique_lock lock(s_sessionMutex);
    s_sessionVotes[pathKey(nifPath)] = {.identity = identity, .votes = votes};
}

auto PGRunCache::tryGetCachedMeshUses(const filesystem::path& nifPath,
                                      MeshUses& uses) -> bool
{
    if (!arePreviousMeshUsesValid()) {
        return false;
    }

    const auto it = s_previous->meshUses.find(pathKey(nifPath));
    if (it == s_previous->meshUses.end()) {
        return false;
    }

    uses = it->second;
    return true;
}

auto PGRunCache::tryGetCachedCMClassification(const filesystem::path& texture,
                                              const BethesdaDirectory::FileIdentity& identity,
                                              CMClassification& result) -> bool
{
    if (!hasPreviousRun() || identity.kind == BethesdaDirectory::FileIdentity::Kind::NONE
        || identity.kind == BethesdaDirectory::FileIdentity::Kind::GENERATED) {
        return false;
    }

    const auto key = pathKey(texture);
    const auto it = s_previous->textures.find(key);
    if (it == s_previous->textures.end() || !it->second.hasCM || it->second.identity != identity) {
        return false;
    }

    result = it->second.cm;

    const unique_lock lock(s_sessionMutex);
    s_sessionCM[key] = {identity, result};
    return true;
}

void PGRunCache::storeCMClassification(const filesystem::path& texture,
                                       const BethesdaDirectory::FileIdentity& identity,
                                       const CMClassification& result)
{
    if (!s_enabled || identity.kind == BethesdaDirectory::FileIdentity::Kind::NONE
        || identity.kind == BethesdaDirectory::FileIdentity::Kind::GENERATED) {
        return;
    }

    const unique_lock lock(s_sessionMutex);
    s_sessionCM[pathKey(texture)] = {identity, result};
}

//
// Recording
//

auto PGRunCache::isRecording() -> bool { return s_activeRecorder != nullptr && s_suspendDepth == 0; }

void PGRunCache::recordTextureType(const filesystem::path& path,
                                   const PGEnums::TextureType& type)
{
    if (isRecording()) {
        s_activeRecorder->recordTextureType(path, type);
    }
}

void PGRunCache::recordTextureAttribute(const filesystem::path& path,
                                        const PGEnums::TextureAttribute& attribute,
                                        bool value)
{
    if (isRecording()) {
        s_activeRecorder->recordTextureAttribute(path, attribute, value);
    }
}

void PGRunCache::recordTextureAttributes(const filesystem::path& path,
                                         const unordered_set<PGEnums::TextureAttribute>& attributes)
{
    if (isRecording()) {
        s_activeRecorder->recordTextureAttributes(path, attributes);
    }
}

void PGRunCache::recordTexMatch(const wstring& base,
                                const PGEnums::TextureType& type,
                                const vector<PGTypes::PGTexture>& results)
{
    if (isRecording()) {
        s_activeRecorder->recordTexMatch(base, type, results);
    }
}

void PGRunCache::recordModOfFile(const filesystem::path& path,
                                 const wstring& modName)
{
    if (isRecording()) {
        s_activeRecorder->recordModOfFile(path, modName);
    }
}

void PGRunCache::recordModState(const wstring& modName,
                                bool isEnabled,
                                bool areMeshesIgnored)
{
    if (isRecording()) {
        s_activeRecorder->recordModState(modName, isEnabled, areMeshesIgnored);
    }
}

void PGRunCache::recordMatches(const PGTypes::TextureSet& slots,
                               bool singlepassMATO,
                               const PGPlugin::ModelRecordType& recType,
                               uint64_t digest)
{
    if (isRecording()) {
        s_activeRecorder->recordMatches(slots, singlepassMATO, recType, digest);
    }
}

void PGRunCache::recordHookRegistration(const HookKind& kind,
                                        const filesystem::path& texPath)
{
    // Hook registrations are recorded even while suspended: they are side effects, not queries
    if (s_activeRecorder != nullptr) {
        s_activeRecorder->recordHookRegistration(kind, texPath);
    }
}

void PGRunCache::recordOutputFile(const filesystem::path& relPath,
                                  uint64_t size)
{
    if (s_activeRecorder != nullptr) {
        s_activeRecorder->recordOutputFile(relPath, size);
    }
}

void PGRunCache::recordHookOutput(const HookKind& kind,
                                  const filesystem::path& source,
                                  const filesystem::path& output)
{
    if (!s_enabled) {
        return;
    }

    auto* const pgd = PGGlobals::getPGD();

    error_code ec;
    const auto size = filesystem::file_size(pgd->getGeneratedPath() / output, ec);
    if (ec) {
        return;
    }

    HookOutputRecord record;
    record.kind = kind;
    record.source = pathKey(source);
    record.sourceIdentity = pgd->getFileIdentity(source);
    record.output = pathKey(output);
    record.size = size;

    const lock_guard<mutex> lock(s_runMutex);
    s_currentHookOutputs[record.output] = std::move(record);
}

void PGRunCache::appendDeferredMessage(const filesystem::path& nifPath,
                                       const spdlog::level::level_enum& level,
                                       const wstring& message)
{
    if (!s_enabled) {
        return;
    }

    const lock_guard<mutex> lock(s_runMutex);
    const auto it = s_currentRecords.find(pathKey(nifPath));
    if (it == s_currentRecords.end()) {
        return;
    }

    it->second.messages.push_back({.level = level, .text = message});
}

//
// Evaluation
//

auto PGRunCache::evaluateMesh(const filesystem::path& nifPath,
                              const PGDirectory::NifCache& nifCache,
                              const CacheData& previous) -> bool
{
    const auto recordIt = previous.meshRecords.find(pathKey(nifPath));
    if (recordIt == previous.meshRecords.end()) {
        return false;
    }

    const auto& record = recordIt->second;
    const Logger::Prefix prefix(L"UpdateCache: " + nifPath.wstring());

    auto* const pgd = PGGlobals::getPGD();
    PGModManager* pgmm = PGGlobals::isPGMMSet() ? PGGlobals::getPGMM() : nullptr;

    // Plugin uses
    if (record.uses != nifCache.meshUses) {
        Logger::trace("Re-patching: plugin uses changed");
        return false;
    }

    // Source file identities
    for (const auto& [path, identity] : record.fileIdentityDeps) {
        if (pgd->getFileIdentity(path) != identity) {
            Logger::trace(L"Re-patching: file changed: {}", path);
            return false;
        }
    }

    // Outputs of this mesh's own hook registrations are generated during patching, so a dependency on their existence
    // is satisfied by replaying the registrations
    unordered_set<wstring> ownHookOutputs;
    for (const auto& [kind, tex] : record.hookRegistrations) {
        ownHookOutputs.insert(pathKey(hookOutputPath(kind, tex)));
    }

    // File existence
    for (const auto& [path, state] : record.fileExistsDeps) {
        const bool exists = pgd->isFile(path);
        switch (state) {
        case FileExistsState::MISSING:
            if (exists) {
                Logger::trace(L"Re-patching: file now exists: {}", path);
                return false;
            }
            break;
        case FileExistsState::EXISTS:
            if (!exists || pgd->isGenerated(path)) {
                Logger::trace(L"Re-patching: file no longer exists: {}", path);
                return false;
            }
            break;
        case FileExistsState::GENERATED:
            if (!ownHookOutputs.contains(pathKey(path)) && !(exists && pgd->isGenerated(path))) {
                Logger::trace(L"Re-patching: generated file dependency cannot be satisfied: {}", path);
                return false;
            }
            break;
        }
    }

    // Mod ownership
    for (const auto& [path, modName] : record.modOfFileDeps) {
        wstring currentName;
        if (pgmm != nullptr) {
            const auto mod = pgmm->getModByFileSmart(path);
            if (mod != nullptr) {
                currentName = mod->name;
            }
        }

        if (currentName != modName) {
            Logger::trace(L"Re-patching: owning mod changed for {}", path);
            return false;
        }
    }

    // Mod state
    for (const auto& [modName, enabled, ignored] : record.modStateDeps) {
        if (pgmm == nullptr) {
            return false;
        }

        const auto mod = pgmm->getMod(modName);
        if (mod == nullptr) {
            Logger::trace(L"Re-patching: mod no longer exists: {}", modName);
            return false;
        }

        const shared_lock modLock(mod->mutex);
        if (mod->isEnabled != enabled || mod->areMeshesIgnored != ignored) {
            Logger::trace(L"Re-patching: mod state changed: {}", modName);
            return false;
        }
    }

    // Texture types and attributes
    for (const auto& [path, type] : record.textureTypeDeps) {
        if (pgd->getTextureType(path) != type) {
            Logger::trace(L"Re-patching: texture type changed: {}", path);
            return false;
        }
    }

    for (const auto& [path, attribute, value] : record.textureAttributeDeps) {
        if (pgd->hasTextureAttribute(path, attribute) != value) {
            Logger::trace(L"Re-patching: texture attribute changed: {}", path);
            return false;
        }
    }

    for (const auto& [path, mask] : record.textureAttributesDeps) {
        if (attributesToMask(pgd->getTextureAttributes(path)) != mask) {
            Logger::trace(L"Re-patching: texture attributes changed: {}", path);
            return false;
        }
    }

    // Texture map lookups
    for (const auto& [base, type, hash] : record.texMatchDeps) {
        const auto slot = PGNIFUtil::getSlotFromTexType(type);
        if (slot == PGEnums::TextureSlots::UNKNOWN) {
            return false;
        }

        const auto results = PGNIFUtil::getTexMatch(base, type, pgd->getTextureMapConst(slot));
        if (hashTexMatchResults(results) != hash) {
            Logger::trace(L"Re-patching: texture matches changed for {}", base);
            return false;
        }
    }

    // Shader matches per shape
    for (const auto& dep : record.matchesDeps) {
        if (PGPatcher::computeMatchesDigest(nifPath, dep.slots, dep.singlepassMATO, dep.recType) != dep.digest) {
            Logger::trace("Re-patching: shader matches changed");
            return false;
        }
    }

    // Outputs must still be present and intact
    for (const auto& [output, size] : record.outputFiles) {
        const auto it = s_outputSnapshot.find(output);
        if (it == s_outputSnapshot.end() || it->second != size) {
            Logger::trace(L"Re-patching: output file missing or modified: {}", output);
            return false;
        }
    }

    return true;
}

auto PGRunCache::evaluateMeshes(const unordered_map<filesystem::path,
                                                    PGDirectory::NifCache>& meshes,
                                bool multiThread,
                                const function<void(size_t,
                                                    size_t)>& progressCallback) -> unordered_set<filesystem::path>
{
    unordered_set<filesystem::path> skippable;

    if (!arePreviousRecordsValid()) {
        return skippable;
    }

    const CacheData& previous = *s_previous;

    TaskTracker taskTracker("Evaluating previous output", meshes.size());
    if (progressCallback) {
        taskTracker.setCallbackFunc(progressCallback);
    }

    mutex resultMutex;
    TaskPoolRunner runner(multiThread);
    for (const auto& [mesh, nifCache] : meshes) {
        runner.addTask([&taskTracker, &resultMutex, &skippable, &mesh, &nifCache, &previous] {
            const bool canSkip = evaluateMesh(mesh, nifCache, previous);
            if (canSkip) {
                const lock_guard<mutex> lock(resultMutex);
                skippable.insert(mesh);
            }
            taskTracker.completeJob(TaskTracker::Result::SUCCESS);
        });
    }
    runner.runTasks();

    // Weighted _0/_1 counterparts validate each other while patching, so they must be patched together
    vector<filesystem::path> unskip;
    for (const auto& mesh : skippable) {
        const auto& uses = meshes.at(mesh).meshUses;
        const bool weighted = std::ranges::any_of(uses, [](const auto& use) -> bool { return use.second.isWeighted; });
        if (!weighted) {
            continue;
        }

        const auto partner = PGMeshPermutationTracker::getOtherWeightVariant(mesh);
        if (partner == mesh || skippable.contains(partner)) {
            continue;
        }

        const auto partnerIt = meshes.find(partner);
        if (partnerIt == meshes.end()) {
            continue;
        }

        const bool partnerWeighted = std::ranges::any_of(partnerIt->second.meshUses,
                                                         [](const auto& use) -> bool { return use.second.isWeighted; });
        if (partnerWeighted) {
            unskip.push_back(mesh);
        }
    }

    for (const auto& mesh : unskip) {
        Logger::trace(L"Re-patching {} because its weighted counterpart is being re-patched", mesh.wstring());
        skippable.erase(mesh);
    }

    {
        const lock_guard<mutex> lock(s_runMutex);
        s_skippable.clear();
        for (const auto& mesh : skippable) {
            s_skippable.insert(pathKey(mesh));
        }
    }

    Logger::info("Update cache: {} of {} meshes are unchanged since the previous run and will not be re-patched",
                 skippable.size(),
                 meshes.size());

    return skippable;
}

void PGRunCache::pruneStaleOutputs(const unordered_set<filesystem::path>& skippable)
{
    if (!hasPreviousRun()) {
        return;
    }

    const auto generatedPath = PGGlobals::getPGD()->getGeneratedPath();

    unordered_set<wstring> keep;
    {
        const lock_guard<mutex> lock(s_runMutex);
        keep = s_protectedOutputs;
    }

    for (const auto& mesh : skippable) {
        const auto it = s_previous->meshRecords.find(pathKey(mesh));
        if (it == s_previous->meshRecords.end()) {
            continue;
        }

        for (const auto& [output, size] : it->second.outputFiles) {
            keep.insert(output);
        }
    }

    // Generated hook textures are kept until the texture phase decides whether they are still needed
    for (const auto& [output, record] : s_previous->hookOutputs) {
        keep.insert(output);
    }

    size_t removed = 0;
    {
        const lock_guard<mutex> lock(s_runMutex);
        for (auto it = s_outputSnapshot.begin(); it != s_outputSnapshot.end();) {
            if (keep.contains(it->first)) {
                ++it;
                continue;
            }

            error_code ec;
            filesystem::remove(generatedPath / it->first, ec);
            if (ec) {
                Logger::warn(L"Failed to remove stale output file: {}", it->first);
            } else {
                removed++;
            }

            it = s_outputSnapshot.erase(it);
        }
    }

    removeEmptyDirectories(generatedPath / "meshes");
    removeEmptyDirectories(generatedPath / "textures");

    Logger::info("Update cache: removed {} stale output files", removed);
}

void PGRunCache::removeEmptyDirectories(const filesystem::path& root)
{
    error_code ec;
    if (!filesystem::is_directory(root, ec)) {
        return;
    }

    for (const auto& entry :
         filesystem::directory_iterator(root, filesystem::directory_options::skip_permission_denied, ec)) {
        if (entry.is_directory(ec)) {
            removeEmptyDirectories(entry.path());
        }
        ec.clear();
    }

    if (filesystem::is_empty(root, ec) && !ec) {
        filesystem::remove(root, ec);
    }
}

auto PGRunCache::getPreviousRecord(const filesystem::path& nifPath) -> const MeshRecord*
{
    if (!hasPreviousRun()) {
        return nullptr;
    }

    const auto it = s_previous->meshRecords.find(pathKey(nifPath));
    if (it == s_previous->meshRecords.end()) {
        return nullptr;
    }

    return &it->second;
}

void PGRunCache::carryOverRecord(const filesystem::path& nifPath)
{
    if (!hasPreviousRun()) {
        return;
    }

    const auto key = pathKey(nifPath);
    const auto it = s_previous->meshRecords.find(key);
    if (it == s_previous->meshRecords.end()) {
        return;
    }

    // The previous record of a replayed mesh is not read again during this run, so it is moved rather than copied to
    // keep peak memory low on large load orders. Moving the value does not alter the map structure, so concurrent
    // lookups of other meshes stay safe.
    const lock_guard<mutex> lock(s_runMutex);
    s_currentRecords[key] = std::move(it->second);
}

//
// Hooks
//

auto PGRunCache::tryReuseHookOutput(const HookKind& kind,
                                    const filesystem::path& texPath) -> bool
{
    if (!hasPreviousRun()) {
        return false;
    }

    const auto output = hookOutputPath(kind, texPath);
    const auto outputKey = pathKey(output);
    const auto sourceKey = pathKey(texPath);

    const auto it = s_previous->hookOutputs.find(outputKey);
    if (it == s_previous->hookOutputs.end()) {
        return false;
    }

    const auto& previousRecord = it->second;
    if (previousRecord.kind != kind || previousRecord.source != sourceKey) {
        return false;
    }

    auto* const pgd = PGGlobals::getPGD();
    if (pgd->getFileIdentity(texPath) != previousRecord.sourceIdentity) {
        return false;
    }

    {
        const lock_guard<mutex> lock(s_runMutex);

        const auto snapshotIt = s_outputSnapshot.find(outputKey);
        if (snapshotIt == s_outputSnapshot.end() || snapshotIt->second != previousRecord.size) {
            return false;
        }

        if (!s_currentHookOutputs.contains(outputKey)) {
            s_currentHookOutputs[outputKey] = previousRecord;
            s_reusedHooks.emplace_back(kind, sourceKey);
        }
    }

    pgd->addGeneratedFile(output);
    return true;
}

void PGRunCache::replayHookRegistration(const HookKind& kind,
                                        const filesystem::path& texPath)
{
    // addToProcessList reuses the previous output when possible and schedules regeneration otherwise
    switch (kind) {
    case HookKind::CONVERT_TO_CM:
        PatcherTextureHookConvertToCM::addToProcessList(texPath);
        break;
    case HookKind::FIX_SSS:
        PatcherTextureHookFixSSS::addToProcessList(texPath);
        break;
    }
}

void PGRunCache::finalizeHooks()
{
    if (!s_enabled) {
        return;
    }

    vector<pair<HookKind, wstring>> reused;
    unordered_set<wstring> needed;
    {
        const lock_guard<mutex> lock(s_runMutex);
        reused = s_reusedHooks;

        for (const auto& [mesh, record] : s_currentRecords) {
            for (const auto& [kind, tex] : record.hookRegistrations) {
                needed.insert(pathKey(hookOutputPath(kind, tex)));
            }
        }
    }

    // Replay the texture map side effects of reused outputs
    for (const auto& [kind, source] : reused) {
        switch (kind) {
        case HookKind::CONVERT_TO_CM:
            PatcherTextureHookConvertToCM::replayGenerated(source);
            break;
        case HookKind::FIX_SSS:
            PatcherTextureHookFixSSS::replayGenerated(source);
            break;
        }
    }

    // Delete generated textures from the previous run that no mesh needs anymore
    if (s_previous != nullptr) {
        const auto generatedPath = PGGlobals::getPGD()->getGeneratedPath();
        size_t removed = 0;
        for (const auto& [output, record] : s_previous->hookOutputs) {
            if (needed.contains(output)) {
                continue;
            }

            error_code ec;
            if (filesystem::remove(generatedPath / output, ec)) {
                removed++;
            }
        }

        if (removed > 0) {
            Logger::info("Update cache: removed {} generated textures that are no longer needed", removed);
        }
    }
}

//
// Replay helpers
//

auto PGRunCache::buildMeshMeta(const MeshMetaRecord& record) -> PGPatcher::MeshMeta
{
    PGModManager* pgmm = PGGlobals::isPGMMSet() ? PGGlobals::getPGMM() : nullptr;

    auto resolveMod = [pgmm](const wstring& modName) -> shared_ptr<PGModManager::Mod> {
        if (modName.empty() || pgmm == nullptr) {
            return nullptr;
        }
        return pgmm->getMod(modName);
    };

    PGPatcher::MeshMeta meta;
    meta.globalPatchersApplied = record.globalPatchersApplied;
    meta.formKeys = record.formKeys;

    for (const auto& [idx, shapeRecord] : record.shapeMeta) {
        PGPatcher::MeshShapeMeta shapeMeta;
        shapeMeta.blockID = shapeRecord.blockID;
        shapeMeta.shapeName = shapeRecord.shapeName;
        shapeMeta.prePatchersApplied = shapeRecord.prePatchersApplied;
        shapeMeta.postPatchersApplied = shapeRecord.postPatchersApplied;

        for (const auto& [formKey, matchRecords] : shapeRecord.matches) {
            auto& matchMetas = shapeMeta.matches[formKey];
            for (const auto& matchRecord : matchRecords) {
                PGPatcher::MatchMeta matchMeta;
                matchMeta.mod = resolveMod(matchRecord.modName);
                matchMeta.shader = matchRecord.shader;
                matchMeta.shaderTransformTo = matchRecord.shaderTransformTo;
                matchMeta.matchedPath = matchRecord.matchedPath;
                for (const auto& [slot, modName] : matchRecord.resultTextureMods) {
                    auto slotMod = resolveMod(modName);
                    if (slotMod == nullptr) {
                        continue;
                    }
                    matchMeta.resultTextureMods.emplace_back(slot, std::move(slotMod));
                }
                matchMetas.push_back(std::move(matchMeta));
            }
        }

        meta.shapeMeta[idx] = std::move(shapeMeta);
    }

    return meta;
}

void PGRunCache::replayMessages(const MeshRecord& record)
{
    for (const auto& message : record.messages) {
        switch (message.level) {
        case spdlog::level::err:
            Logger::error(L"{}", message.text);
            break;
        case spdlog::level::warn:
            Logger::warn(L"{}", message.text);
            break;
        default:
            break;
        }
    }
}

//
// Persistence
//

auto PGRunCache::isUpdateAvailable(const filesystem::path& outputDir) -> bool
{
    if (outputDir.empty()) {
        return false;
    }

    error_code ec;
    const auto cacheFile = outputDir / CACHE_FILENAME;
    if (!filesystem::is_regular_file(cacheFile, ec)) {
        return false;
    }

    try {
        // Only the header is needed: magic, format version and the PGPatcher version string
        static constexpr size_t HEADER_READ_SIZE = 256;
        ifstream in(cacheFile, ios::binary);
        if (!in.is_open()) {
            return false;
        }

        vector<std::byte> header(HEADER_READ_SIZE);
        in.read(reinterpret_cast<char*>(header.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                static_cast<streamsize>(header.size()));
        header.resize(static_cast<size_t>(in.gcount()));

        BinaryIO::Reader reader(header);
        if (reader.read<uint32_t>() != FORMAT_MAGIC || reader.read<uint32_t>() != FORMAT_VERSION) {
            return false;
        }

        return reader.readString() == PG_FULL_VERSION;
    } catch (const exception&) {
        return false;
    }
}

auto PGRunCache::loadFromFile(const filesystem::path& cacheFile) -> unique_ptr<CacheData>
{
    try {
        const auto bytes = FileUtil::getFileBytes(cacheFile);
        if (bytes.empty()) {
            Logger::debug("Update cache file is empty");
            return nullptr;
        }

        BinaryIO::Reader reader(bytes);

        if (reader.read<uint32_t>() != FORMAT_MAGIC) {
            Logger::debug("Update cache file has an invalid header");
            return nullptr;
        }

        if (reader.read<uint32_t>() != FORMAT_VERSION) {
            Logger::info("Update cache was written by an incompatible version, performing a full run");
            return nullptr;
        }

        auto data = make_unique<CacheData>();
        data->pgVersion = reader.readString();
        if (data->pgVersion != PG_FULL_VERSION) {
            Logger::info("Update cache was written by PGPatcher {} (current: {}), performing a full run",
                         data->pgVersion,
                         PG_FULL_VERSION);
            return nullptr;
        }

        data->configFingerprint = reader.read<uint64_t>();
        data->pluginFingerprint = reader.read<uint64_t>();

        // String table
        const auto stringCount = readCount<size_t>(reader);
        vector<wstring> strings;
        strings.reserve(stringCount);
        for (size_t i = 0; i < stringCount; i++) {
            strings.push_back(reader.readWString());
        }
        const StringTable st(std::move(strings));

        // Textures
        auto count = readCount<size_t>(reader);
        data->textures.reserve(count);
        for (size_t i = 0; i < count; i++) {
            const auto path = st.get(reader.read<uint32_t>());
            TextureInfo info;
            info.identity = readIdentity(reader, st);
            const auto flags = reader.read<uint8_t>();
            info.hasCM = (flags & 1U) != 0U;
            info.cm.isCM = (flags & 2U) != 0U;
            info.cm.hasEnvMask = (flags & 4U) != 0U;
            info.cm.hasGlossiness = (flags & 8U) != 0U;
            info.cm.hasMetalness = (flags & 16U) != 0U;
            info.hasMeta = (flags & 32U) != 0U;
            if (info.hasMeta) {
                info.meta = readTexMetadata(reader);
            }
            data->textures.emplace(path, std::move(info));
        }

        // Mesh votes
        count = readCount<size_t>(reader);
        data->meshVotes.reserve(count);
        for (size_t i = 0; i < count; i++) {
            const auto path = st.get(reader.read<uint32_t>());
            MeshVotes votes;
            votes.identity = readIdentity(reader, st);
            const auto voteCount = readCount<size_t>(reader);
            votes.votes.reserve(voteCount);
            for (size_t j = 0; j < voteCount; j++) {
                TextureVote vote;
                vote.texture = st.get(reader.read<uint32_t>());
                vote.slot = static_cast<PGEnums::TextureSlots>(reader.read<uint8_t>());
                vote.type = static_cast<PGEnums::TextureType>(reader.read<uint8_t>());
                votes.votes.push_back(std::move(vote));
            }
            data->meshVotes.emplace(path, std::move(votes));
        }

        // Mesh uses
        count = readCount<size_t>(reader);
        data->meshUses.reserve(count);
        for (size_t i = 0; i < count; i++) {
            const auto path = st.get(reader.read<uint32_t>());
            data->meshUses.emplace(path, readUses(reader, st));
        }

        // Hook outputs
        count = readCount<size_t>(reader);
        data->hookOutputs.reserve(count);
        for (size_t i = 0; i < count; i++) {
            HookOutputRecord record;
            record.kind = static_cast<HookKind>(reader.read<uint8_t>());
            record.source = st.get(reader.read<uint32_t>());
            record.sourceIdentity = readIdentity(reader, st);
            record.output = st.get(reader.read<uint32_t>());
            record.size = reader.read<uint64_t>();
            data->hookOutputs.emplace(record.output, std::move(record));
        }

        // Mesh records
        count = readCount<size_t>(reader);
        data->meshRecords.reserve(count);
        for (size_t i = 0; i < count; i++) {
            const auto path = st.get(reader.read<uint32_t>());
            data->meshRecords.emplace(path, readRecord(reader, st));
        }

        if (!reader.atEnd()) {
            Logger::debug("Update cache file has trailing data");
            return nullptr;
        }

        return data;
    } catch (const exception& e) {
        Logger::debug("Failed to read update cache: {}", e.what());
        return nullptr;
    }
}

auto PGRunCache::saveToFile(const filesystem::path& cacheFile,
                            const CacheData& data) -> bool
{
    try {
        StringTableBuilder st;
        BinaryIO::Writer body;

        // Textures
        body.write<uint32_t>(static_cast<uint32_t>(data.textures.size()));
        for (const auto& [path, info] : data.textures) {
            body.write<uint32_t>(st.id(path));
            writeIdentity(body, st, info.identity);
            uint8_t flags = 0;
            flags |= info.hasCM ? 1U : 0U;
            flags |= info.cm.isCM ? 2U : 0U;
            flags |= info.cm.hasEnvMask ? 4U : 0U;
            flags |= info.cm.hasGlossiness ? 8U : 0U;
            flags |= info.cm.hasMetalness ? 16U : 0U;
            flags |= info.hasMeta ? 32U : 0U;
            body.write<uint8_t>(flags);
            if (info.hasMeta) {
                writeTexMetadata(body, info.meta);
            }
        }

        // Mesh votes
        body.write<uint32_t>(static_cast<uint32_t>(data.meshVotes.size()));
        for (const auto& [path, votes] : data.meshVotes) {
            body.write<uint32_t>(st.id(path));
            writeIdentity(body, st, votes.identity);
            body.write<uint32_t>(static_cast<uint32_t>(votes.votes.size()));
            for (const auto& vote : votes.votes) {
                body.write<uint32_t>(st.id(vote.texture));
                body.write<uint8_t>(static_cast<uint8_t>(vote.slot));
                body.write<uint8_t>(static_cast<uint8_t>(vote.type));
            }
        }

        // Mesh uses
        body.write<uint32_t>(static_cast<uint32_t>(data.meshUses.size()));
        for (const auto& [path, uses] : data.meshUses) {
            body.write<uint32_t>(st.id(path));
            writeUses(body, st, uses);
        }

        // Hook outputs
        body.write<uint32_t>(static_cast<uint32_t>(data.hookOutputs.size()));
        for (const auto& [output, record] : data.hookOutputs) {
            body.write<uint8_t>(static_cast<uint8_t>(record.kind));
            body.write<uint32_t>(st.id(record.source));
            writeIdentity(body, st, record.sourceIdentity);
            body.write<uint32_t>(st.id(record.output));
            body.write<uint64_t>(record.size);
        }

        // Mesh records
        body.write<uint32_t>(static_cast<uint32_t>(data.meshRecords.size()));
        for (const auto& [path, record] : data.meshRecords) {
            body.write<uint32_t>(st.id(path));
            writeRecord(body, st, record);
        }

        // Header + string table + body
        BinaryIO::Writer file;
        file.write<uint32_t>(FORMAT_MAGIC);
        file.write<uint32_t>(FORMAT_VERSION);
        file.writeString(data.pgVersion);
        file.write<uint64_t>(data.configFingerprint);
        file.write<uint64_t>(data.pluginFingerprint);

        file.write<uint32_t>(static_cast<uint32_t>(st.strings().size()));
        for (const auto& str : st.strings()) {
            file.writeWString(str);
        }

        file.writeBytes(body.data().data(), body.size());

        return file.saveToFile(cacheFile);
    } catch (const exception& e) {
        Logger::warn("Failed to write update cache: {}", e.what());
        return false;
    }
}
