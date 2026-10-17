#include "PCH.h"

#include "ClassificationCache.hpp"

#include "common/BethesdaDirectory.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/Logger.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace PGLite {

namespace {

constexpr std::uint32_t cacheMagic = 0x43434750; // "PGCC"
constexpr std::uint32_t cacheVersion = 1;

/// @brief Writes plain values and length prefixed strings, see Reader.
class Writer {
public:
    explicit Writer(std::ofstream& stream)
        : m_stream(&stream)
    {
    }

    template<typename T> void write(const T& value)
    {
        m_stream->write(reinterpret_cast<const char*>(&value),
                        sizeof(value)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    }

    void write(const std::string& value)
    {
        write(static_cast<std::uint32_t>(value.size()));
        m_stream->write(value.data(), static_cast<std::streamsize>(value.size()));
    }

    void write(const std::wstring& value)
    {
        write(static_cast<std::uint32_t>(value.size()));
        m_stream->write(
            reinterpret_cast<const char*>(value.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            static_cast<std::streamsize>(value.size() * sizeof(wchar_t)));
    }

    void write(const BethesdaDirectory::FileIdentity& identity)
    {
        write(static_cast<std::uint8_t>(identity.kind));
        write(identity.mtime);
        write(identity.size);
        write(identity.bsaRelPath);
        write(identity.bsaMtime);
        write(identity.bsaSize);
    }

private:
    std::ofstream* m_stream;
};

/// @brief Reads what Writer wrote; every function returns false at the end of the stream or on a corrupt length.
class Reader {
public:
    explicit Reader(std::ifstream& stream)
        : m_stream(&stream)
    {
    }

    template<typename T> [[nodiscard]] bool read(T& value)
    {
        m_stream->read(reinterpret_cast<char*>(&value),
                       sizeof(value)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        return m_stream->good();
    }

    [[nodiscard]] bool read(std::string& value)
    {
        std::uint32_t length = 0;
        if (!read(length) || length > maxLength)
            return false;

        value.resize(length);
        m_stream->read(value.data(), static_cast<std::streamsize>(length));
        return m_stream->good();
    }

    [[nodiscard]] bool read(std::wstring& value)
    {
        std::uint32_t length = 0;
        if (!read(length) || length > maxLength)
            return false;

        value.resize(length);
        m_stream->read(reinterpret_cast<char*>(value.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                       static_cast<std::streamsize>(length * sizeof(wchar_t)));
        return m_stream->good();
    }

    [[nodiscard]] bool read(BethesdaDirectory::FileIdentity& identity)
    {
        std::uint8_t kind = 0;
        if (!read(kind) || !read(identity.mtime) || !read(identity.size) || !read(identity.bsaRelPath)
            || !read(identity.bsaMtime) || !read(identity.bsaSize)) {
            return false;
        }

        identity.kind = static_cast<BethesdaDirectory::FileIdentity::Kind>(kind);
        return true;
    }

private:
    static constexpr std::uint32_t maxLength = 32768;

    std::ifstream* m_stream;
};

bool isCacheable(const BethesdaDirectory::FileIdentity& identity)
{
    // A file that is not in the load order, or a generated one, has no identity to compare against.
    return identity.kind == BethesdaDirectory::FileIdentity::Kind::Loose
        || identity.kind == BethesdaDirectory::FileIdentity::Kind::BSA;
}

}

void ClassificationCache::load(const std::filesystem::path& file)
{
    const std::scoped_lock lock(m_mutex);
    m_file = file;
    m_cmEntries.clear();
    m_meshEntries.clear();
    m_isDirty = false;

    std::ifstream stream(file, std::ios::binary);
    if (!stream.is_open())
        return;

    Reader reader(stream);
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::string pgliteVersion;
    if (!reader.read(magic) || !reader.read(version) || magic != cacheMagic || version != cacheVersion
        || !reader.read(pgliteVersion)) {
        Logger::warn(L"The classification cache {} is not readable and is rebuilt", file.wstring());
        return;
    }

    if (pgliteVersion != PG_FULL_VERSION) {
        Logger::info("The classification cache was written by PGLite {} and is rebuilt", pgliteVersion);
        return;
    }

    std::uint32_t cmCount = 0;
    if (!reader.read(cmCount)) {
        Logger::warn(L"The classification cache {} is truncated and is rebuilt", file.wstring());
        return;
    }

    for (std::uint32_t i = 0; i < cmCount; i++) {
        std::wstring texture;
        CMEntry entry;
        std::uint8_t flags = 0;
        if (!reader.read(texture) || !reader.read(entry.identity) || !reader.read(flags)) {
            Logger::warn(L"The classification cache {} is truncated, {} of {} complex material entries are used",
                         file.wstring(),
                         m_cmEntries.size(),
                         cmCount);
            return;
        }

        entry.classification.isCM = flags & 1U;
        entry.classification.hasEnvMask = flags & 2U;
        entry.classification.hasGlossiness = flags & 4U;
        entry.classification.hasMetalness = flags & 8U;
        m_cmEntries[std::filesystem::path(texture)] = std::move(entry);
    }

    std::uint32_t meshCount = 0;
    if (!reader.read(meshCount)) {
        Logger::warn(L"The classification cache {} is truncated, no mesh entries are used", file.wstring());
        return;
    }

    for (std::uint32_t i = 0; i < meshCount; i++) {
        std::wstring mesh;
        MeshEntry entry;
        std::uint32_t voteCount = 0;
        bool isComplete = reader.read(mesh) && reader.read(entry.identity) && reader.read(voteCount);
        for (std::uint32_t j = 0; isComplete && j < voteCount; j++) {
            PGTypes::TextureVote vote;
            std::uint8_t slot = 0;
            std::uint8_t type = 0;
            isComplete = reader.read(vote.texture) && reader.read(slot) && reader.read(type);
            vote.slot = static_cast<PGEnums::TextureSlots>(slot);
            vote.type = static_cast<PGEnums::TextureType>(type);
            entry.votes.push_back(std::move(vote));
        }

        if (!isComplete) {
            Logger::warn(L"The classification cache {} is truncated, {} of {} mesh entries are used",
                         file.wstring(),
                         m_meshEntries.size(),
                         meshCount);
            return;
        }

        m_meshEntries[std::filesystem::path(mesh)] = std::move(entry);
    }

    Logger::debug(L"Loaded {} complex material classifications and {} mesh votes from {}",
                  m_cmEntries.size(),
                  m_meshEntries.size(),
                  file.wstring());
}

void ClassificationCache::unload()
{
    const std::scoped_lock lock(m_mutex);
    m_cmEntries.clear();
    m_meshEntries.clear();
    m_isDirty = false;
}

void ClassificationCache::setHookRegistrationListener(HookRegistrationListener listener)
{
    m_hookRegistrationListener = std::move(listener);
}

void ClassificationCache::setTextureReadFailureListener(TextureReadFailureListener listener)
{
    m_textureReadFailureListener = std::move(listener);
}

void ClassificationCache::recordHookRegistration(const HookKind& kind,
                                                 const std::filesystem::path& texPath)
{
    if (m_hookRegistrationListener)
        m_hookRegistrationListener(kind, texPath);
}

void ClassificationCache::recordTextureReadFailure(const std::filesystem::path& path)
{
    if (m_textureReadFailureListener)
        m_textureReadFailureListener(path);
}

void ClassificationCache::save() const
{
    const std::scoped_lock lock(m_mutex);
    if (!m_isDirty || m_file.empty())
        return;

    std::size_t cmWritten = 0;
    std::size_t meshesWritten = 0;
    const auto temporary = m_file.wstring() + L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            Logger::error(L"Unable to write the classification cache {}", m_file.wstring());
            return;
        }

        Writer writer(stream);
        writer.write(cacheMagic);
        writer.write(cacheVersion);
        writer.write(std::string(PG_FULL_VERSION));

        for (const auto& [texture, entry] : m_cmEntries)
            if (entry.isCurrent)
                cmWritten++;
        writer.write(static_cast<std::uint32_t>(cmWritten));
        for (const auto& [texture, entry] : m_cmEntries) {
            if (!entry.isCurrent)
                continue;

            writer.write(texture.wstring());
            writer.write(entry.identity);
            const std::uint8_t flags = (entry.classification.isCM ? 1U : 0U)
                | (entry.classification.hasEnvMask ? 2U : 0U) | (entry.classification.hasGlossiness ? 4U : 0U)
                | (entry.classification.hasMetalness ? 8U : 0U);
            writer.write(flags);
        }

        for (const auto& [mesh, entry] : m_meshEntries)
            if (entry.isCurrent)
                meshesWritten++;
        writer.write(static_cast<std::uint32_t>(meshesWritten));
        for (const auto& [mesh, entry] : m_meshEntries) {
            if (!entry.isCurrent)
                continue;

            writer.write(mesh.wstring());
            writer.write(entry.identity);
            writer.write(static_cast<std::uint32_t>(entry.votes.size()));
            for (const auto& vote : entry.votes) {
                writer.write(vote.texture);
                writer.write(static_cast<std::uint8_t>(vote.slot));
                writer.write(static_cast<std::uint8_t>(vote.type));
            }
        }

        stream.close(); // the last buffered block is written by the close, so the check comes after it
        if (!stream) {
            // A truncated file must not replace the good one (a full disk).
            Logger::error(L"Unable to write the classification cache {}", m_file.wstring());
            std::error_code removeError;
            std::filesystem::remove(temporary, removeError);
            return;
        }
    }

    std::error_code ec;
    std::filesystem::rename(temporary, m_file, ec);
    if (ec) {
        Logger::error(L"Unable to replace the classification cache {}: {}",
                      m_file.wstring(),
                      std::filesystem::path(ec.message()).wstring());
        std::filesystem::remove(temporary, ec);
        return;
    }

    m_isDirty = false;
    Logger::debug(L"Saved {} complex material classifications and {} mesh votes to {}",
                  cmWritten,
                  meshesWritten,
                  m_file.wstring());
}

std::size_t ClassificationCache::cmCount() const
{
    const std::scoped_lock lock(m_mutex);
    return m_cmEntries.size();
}

std::size_t ClassificationCache::meshCount() const
{
    const std::scoped_lock lock(m_mutex);
    return m_meshEntries.size();
}

bool ClassificationCache::tryGetCachedMeshVotes(const std::filesystem::path& nifPath,
                                                const BethesdaDirectory::FileIdentity& identity,
                                                std::vector<PGTypes::TextureVote>& votes)
{
    if (!isCacheable(identity))
        return false;

    const std::scoped_lock lock(m_mutex);
    const auto it = m_meshEntries.find(nifPath);
    if (it == m_meshEntries.end() || it->second.identity != identity)
        return false;

    it->second.isCurrent = true;
    votes = it->second.votes;
    return true;
}

void ClassificationCache::storeMeshVotes(const std::filesystem::path& nifPath,
                                         const BethesdaDirectory::FileIdentity& identity,
                                         const std::vector<PGTypes::TextureVote>& votes)
{
    if (!isCacheable(identity))
        return;

    const std::scoped_lock lock(m_mutex);
    m_meshEntries[nifPath] = { .identity = identity, .votes = votes, .isCurrent = true };
    m_isDirty = true;
}

bool ClassificationCache::tryGetCachedCMClassification(const std::filesystem::path& texture,
                                                       const BethesdaDirectory::FileIdentity& identity,
                                                       PGTypes::CMClassification& result)
{
    if (!isCacheable(identity))
        return false;

    const std::scoped_lock lock(m_mutex);
    const auto it = m_cmEntries.find(texture);
    if (it == m_cmEntries.end() || it->second.identity != identity)
        return false;

    it->second.isCurrent = true;
    result = it->second.classification;
    return true;
}

void ClassificationCache::storeCMClassification(const std::filesystem::path& texture,
                                                const BethesdaDirectory::FileIdentity& identity,
                                                const PGTypes::CMClassification& result)
{
    if (!isCacheable(identity))
        return;

    const std::scoped_lock lock(m_mutex);
    m_cmEntries[texture] = { .identity = identity, .classification = result, .isCurrent = true };
    m_isDirty = true;
}

}
