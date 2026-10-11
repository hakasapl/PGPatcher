#include "PCH.h"

#include "CMCache.hpp"

#include "common/BethesdaDirectory.hpp"
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

namespace PGLite {

namespace {

constexpr std::uint32_t cacheMagic = 0x4D434750; // "PGCM"
constexpr std::uint32_t cacheVersion = 1;

/// @brief Writes plain values and length prefixed wide strings, see Reader.
class Writer {
public:
    explicit Writer(std::ofstream& stream)
        : m_stream(&stream)
    {
    }

    template<typename T>
    void write(const T& value)
    {
        m_stream->write(reinterpret_cast<const char*>(&value), sizeof(value)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    }

    void write(const std::wstring& value)
    {
        write(static_cast<std::uint32_t>(value.size()));
        m_stream->write(reinterpret_cast<const char*>(value.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                        static_cast<std::streamsize>(value.size() * sizeof(wchar_t)));
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

    template<typename T>
    [[nodiscard]] bool read(T& value)
    {
        m_stream->read(reinterpret_cast<char*>(&value), sizeof(value)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        return m_stream->good();
    }

    [[nodiscard]] bool read(std::wstring& value)
    {
        static constexpr std::uint32_t maxLength = 32768;

        std::uint32_t length = 0;
        if (!read(length) || length > maxLength)
            return false;

        value.resize(length);
        m_stream->read(reinterpret_cast<char*>(value.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                       static_cast<std::streamsize>(length * sizeof(wchar_t)));
        return m_stream->good();
    }

private:
    std::ifstream* m_stream;
};

}

void CMCache::load(const std::filesystem::path& file)
{
    const std::scoped_lock lock(m_mutex);
    m_file = file;
    m_entries.clear();
    m_isDirty = false;

    std::ifstream stream(file, std::ios::binary);
    if (!stream.is_open())
        return;

    Reader reader(stream);
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    if (!reader.read(magic) || !reader.read(version) || !reader.read(count) || magic != cacheMagic
        || version != cacheVersion) {
        Logger::warn(L"The complex material cache {} is not readable and is rebuilt", file.wstring());
        return;
    }

    for (std::uint32_t i = 0; i < count; i++) {
        std::wstring texture;
        std::uint8_t kind = 0;
        Entry entry;
        std::uint8_t flags = 0;
        if (!reader.read(texture) || !reader.read(kind) || !reader.read(entry.identity.mtime)
            || !reader.read(entry.identity.size) || !reader.read(entry.identity.bsaRelPath)
            || !reader.read(entry.identity.bsaMtime) || !reader.read(entry.identity.bsaSize) || !reader.read(flags)) {
            Logger::warn(L"The complex material cache {} is truncated, {} of {} entries are used",
                         file.wstring(),
                         m_entries.size(),
                         count);
            break;
        }

        entry.identity.kind = static_cast<BethesdaDirectory::FileIdentity::Kind>(kind);
        entry.classification.isCM = flags & 1U;
        entry.classification.hasEnvMask = flags & 2U;
        entry.classification.hasGlossiness = flags & 4U;
        entry.classification.hasMetalness = flags & 8U;
        m_entries[std::filesystem::path(texture)] = std::move(entry);
    }

    Logger::debug(L"Loaded {} complex material classifications from {}", m_entries.size(), file.wstring());
}

void CMCache::save() const
{
    const std::scoped_lock lock(m_mutex);
    if (!m_isDirty || m_file.empty())
        return;

    const auto temporary = m_file.wstring() + L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            Logger::error(L"Unable to write the complex material cache {}", m_file.wstring());
            return;
        }

        Writer writer(stream);
        writer.write(cacheMagic);
        writer.write(cacheVersion);
        writer.write(static_cast<std::uint32_t>(m_entries.size()));
        for (const auto& [texture, entry] : m_entries) {
            writer.write(texture.wstring());
            writer.write(static_cast<std::uint8_t>(entry.identity.kind));
            writer.write(entry.identity.mtime);
            writer.write(entry.identity.size);
            writer.write(entry.identity.bsaRelPath);
            writer.write(entry.identity.bsaMtime);
            writer.write(entry.identity.bsaSize);
            const std::uint8_t flags = (entry.classification.isCM ? 1U : 0U) | (entry.classification.hasEnvMask ? 2U : 0U)
                | (entry.classification.hasGlossiness ? 4U : 0U) | (entry.classification.hasMetalness ? 8U : 0U);
            writer.write(flags);
        }
    }

    std::error_code ec;
    std::filesystem::rename(temporary, m_file, ec);
    if (ec) {
        Logger::error(L"Unable to replace the complex material cache {}: {}",
                      m_file.wstring(),
                      std::filesystem::path(ec.message()).wstring());
        return;
    }

    m_isDirty = false;
    Logger::debug(L"Saved {} complex material classifications to {}", m_entries.size(), m_file.wstring());
}

std::size_t CMCache::size() const
{
    const std::scoped_lock lock(m_mutex);
    return m_entries.size();
}

bool CMCache::tryGetCachedCMClassification(const std::filesystem::path& texture,
                                           const BethesdaDirectory::FileIdentity& identity,
                                           PGTypes::CMClassification& result)
{
    if (identity.kind == BethesdaDirectory::FileIdentity::Kind::None)
        return false;

    const std::scoped_lock lock(m_mutex);
    const auto it = m_entries.find(texture);
    if (it == m_entries.end() || it->second.identity != identity)
        return false;

    result = it->second.classification;
    return true;
}

void CMCache::storeCMClassification(const std::filesystem::path& texture,
                                    const BethesdaDirectory::FileIdentity& identity,
                                    const PGTypes::CMClassification& result)
{
    if (identity.kind == BethesdaDirectory::FileIdentity::Kind::None)
        return;

    const std::scoped_lock lock(m_mutex);
    m_entries[texture] = { .identity = identity, .classification = result };
    m_isDirty = true;
}

}
