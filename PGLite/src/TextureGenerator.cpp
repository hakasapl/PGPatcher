#include "PCH.h"

#include "TextureGenerator.hpp"

#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGRunObserver.hpp"
#include "PatchStream.hpp"
#include "Resources.hpp"
#include "Settings.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "patchers/base/PatcherTextureHook.hpp"
#include "pgutil/PGEnums.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <DirectXTex.h>
#include <boost/crc.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>
#include <winerror.h>

namespace PGLite {

namespace {

/// @brief The first line of every key file; a change here invalidates every cached texture.
constexpr std::string_view keyHeader = "PGLite texture cache 1";

/// @brief The textures the patch running on this thread registered, until generatePending() takes them.
thread_local std::vector<std::pair<PGRunObserver::HookKind, std::filesystem::path>> s_pending;

std::wstring hookName(PGRunObserver::HookKind kind)
{
    return StringUtil::utf8toUTF16(PatcherTextureHook::hookNameOf(kind));
}

std::filesystem::path keyFileOf(const std::filesystem::path& cacheFile)
{
    auto keyFile = cacheFile;
    keyFile += L".key";
    return keyFile;
}

std::string pathString(const std::filesystem::path& path) { return StringUtil::utf16toWindows1252(path.wstring()); }

bool readText(const std::filesystem::path& file,
              std::string& text)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
        return false;

    text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return !stream.bad();
}

}

TextureGenerator::TextureGenerator(std::filesystem::path cacheFolder)
    : m_cacheFolder(std::move(cacheFolder))
    , m_noSource(std::make_shared<SourceStream>())
{
}

void TextureGenerator::noteRegistration(HookKind kind,
                                        const std::filesystem::path& texPath)
{
    s_pending.emplace_back(kind, texPath);
}

void TextureGenerator::generatePending()
{
    std::vector<std::pair<HookKind, std::filesystem::path>> pending;
    pending.swap(s_pending);

    // A texture is registered once per shape that names it, and this open gets one try per texture.
    std::unordered_set<std::filesystem::path> outputs;
    for (const auto& [kind, texPath] : pending)
        if (outputs.insert(PatcherTextureHook::outputPathOf(kind, texPath)).second)
            process(kind, texPath);
}

void TextureGenerator::onEnvironmentReplaced(PGDirectory& directory)
{
    const std::scoped_lock lock(m_mutex);
    for (auto& [output, entry] : m_entries) {
        if (!entry.isServed) {
            // A new start for a texture that failed: its attempts are given back, the first open that names it tries
            // at once. Nothing is in flight while this runs.
            entry.attempts = 0;
            entry.failureTime = { };
            continue;
        }

        directory.addGeneratedFile(output);
        directory.setTextureType(output, entry.type);
    }

    // The next patch registers its textures with the new directory, as the first patch did with the old one.
    PatcherTextureHookConvertToCM::reset();
    PatcherTextureHookFixSSS::reset();
}

TextureGenerator::Stats TextureGenerator::stats() const
{
    Stats stats { .generated = m_generated, .fromCache = m_fromCache, .failed = 0 };
    const std::scoped_lock lock(m_mutex);
    for (const auto& [output, entry] : m_entries)
        if (entry.isDone && !entry.isServed)
            stats.failed++;

    return stats;
}

void TextureGenerator::process(HookKind kind,
                               const std::filesystem::path& texPath)
{
    const auto output = PatcherTextureHook::outputPathOf(kind, texPath);
    Entry* entry = nullptr;
    {
        std::unique_lock lock(m_mutex);
        const auto [it, isNew] = m_entries.try_emplace(output);
        entry = &it->second; // the map is node based, so the entry stays where it is
        if (isNew) {
            entry->type = PatcherTextureHook::outputTypeOf(kind);
        } else {
            // Made already, or being made by another thread: the mesh that names it is about to be parsed, so wait.
            const auto attemptsSeen = entry->attempts;
            m_done.wait(lock, [entry] { return entry->isDone; });
            if (entry->isServed)
                return;

            // A texture that failed is tried again by the first open that names it after the retry interval, since
            // the failure may have been the engine's or the disk's at the time; a generation that finished while this
            // open waited was its try, and so is one that failed moments ago, as the opens of one burst share it. A
            // texture that keeps failing stays missing once the attempts since the last Apply are spent.
            if (entry->attempts != attemptsSeen || entry->attempts >= maxAttempts
                || std::chrono::steady_clock::now() - entry->failureTime < readFailureRetryInterval) {
                return;
            }

            entry->isDone = false;
        }
    }

    bool isServed = false;
    {
        const std::scoped_lock makeLock(m_makeMutex);
        isServed = make(kind, texPath, output);
    }
    {
        const std::scoped_lock lock(m_mutex);
        entry->isServed = isServed;
        entry->attempts++;
        if (!isServed)
            entry->failureTime = std::chrono::steady_clock::now();
        entry->isDone = true;
    }
    m_done.notify_all();
}

bool TextureGenerator::make(HookKind kind,
                            const std::filesystem::path& texPath,
                            const std::filesystem::path& output)
{
    const auto type = PatcherTextureHook::outputTypeOf(kind);
    try {
        const auto started = std::chrono::steady_clock::now();

        // The source as the engine serves it: the loose file or archive entry the game itself loads for the path.
        std::vector<std::byte> source;
        if (!Resources::readFile(pathString(texPath), source)) {
            Logger::error(L"Unable to read {} to generate {}", texPath.wstring(), output.wstring());
            return false;
        }

        const auto key = cacheKey(kind, texPath, source);
        const auto cacheFile = m_cacheFolder / output;
        if (isCached(cacheFile, key)) {
            m_fromCache++;
            Logger::debug(L"The cached {} is current", output.wstring());
        } else {
            if (!generate(kind, texPath, source, cacheFile, key))
                return false;

            m_generated++;
            const auto elapsed
                = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
            Logger::info(L"Generated {} from {} ({}, {} ms)",
                         output.wstring(),
                         texPath.wstring(),
                         hookName(kind),
                         elapsed.count());
        }

        // As offline: the type for the texture type lookups, no texture map entry.
        PGGlobals::pgd()->setTextureType(output, type);

        const auto outputStr = pathString(output);
        {
            const std::scoped_lock lock(m_cacheFilesMutex);
            m_cacheFiles[outputStr] = cacheFile;
        }

        // Registered like a mesh (DESIGN.md section 3.1): a file of the same name in the load order is shadowed, as the
        // offline output folder shadows it. The registry keeps the stream for the session.
        RE::BSTSmartPointer<RE::BSResource::Stream> stream(new PatchStream(outputStr, m_noSource, *this));
        Resources::registerStream(outputStr, stream);

        return true;
    } catch (const std::exception& e) {
        Logger::error(L"Unable to generate {} from {}: {}",
                      output.wstring(),
                      texPath.wstring(),
                      StringUtil::utf8toUTF16(e.what()));
        return false;
    } catch (...) {
        // Whatever is thrown, the caller marks the entry done, so no thread waits for it forever.
        Logger::error(L"Unable to generate {} from {}", output.wstring(), texPath.wstring());
        return false;
    }
}

std::string TextureGenerator::cacheKey(HookKind kind,
                                       const std::filesystem::path& texPath,
                                       const std::vector<std::byte>& source)
{
    boost::crc_32_type crc;
    crc.process_bytes(source.data(), source.size());

    // Everything the output depends on. The generator signature covers the shader bytecode, the formats and the
    // parameters of the hook; the PGLite version covers the code around them (every commit changes it).
    return std::format(
        "{}\nhook={}\ngenerator={:08x}\ndirectxtex={}\npglite={}\nsource={}\nsourceSize={}\nsourceCrc32={:08x}\n",
        keyHeader,
        PatcherTextureHook::hookNameOf(kind),
        PatcherTextureHook::generatorSignatureOf(kind),
        DIRECTX_TEX_VERSION,
        PG_FULL_VERSION,
        StringUtil::utf16toUTF8(texPath.wstring()),
        source.size(),
        crc.checksum());
}

bool TextureGenerator::isCached(const std::filesystem::path& cacheFile,
                                const std::string& key)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(cacheFile, ec);
    if (ec)
        return false;

    std::string text;
    if (!readText(keyFileOf(cacheFile), text))
        return false;

    return text == key + std::format("outputSize={}\n", size);
}

bool TextureGenerator::generate(HookKind kind,
                                const std::filesystem::path& texPath,
                                const std::vector<std::byte>& source,
                                const std::filesystem::path& cacheFile,
                                const std::string& key)
{
    DirectX::ScratchImage image;
    if (FAILED(DirectX::LoadFromDDSMemory(source.data(), source.size(), DirectX::DDS_FLAGS_NONE, nullptr, image))) {
        Logger::error(
            L"Unable to generate {}: {} is not a DDS file", cacheFile.filename().wstring(), texPath.wstring());
        return false;
    }

    // The hook's generation is the offline one: the compute shader, then the block compression of the output file.
    DirectX::ScratchImage compressed;
    const bool isGenerated = kind == HookKind::ConvertToCM
        ? PatcherTextureHookConvertToCM(texPath, &image).generate(compressed)
        : PatcherTextureHookFixSSS(texPath, &image).generate(compressed);
    if (!isGenerated) {
        Logger::error(
            L"Unable to generate {} from {} ({})", cacheFile.filename().wstring(), texPath.wstring(), hookName(kind));
        return false;
    }

    DirectX::Blob blob;
    if (FAILED(DirectX::SaveToDDSMemory(compressed.GetImages(),
                                        compressed.GetImageCount(),
                                        compressed.GetMetadata(),
                                        DirectX::DDS_FLAGS_NONE,
                                        blob))) {
        Logger::error(L"Unable to serialize the texture generated from {}", texPath.wstring());
        return false;
    }

    // The old key goes first: a key never describes a texture it was not written for, whatever happens in between.
    std::error_code ec;
    std::filesystem::create_directories(cacheFile.parent_path(), ec);
    std::filesystem::remove(keyFileOf(cacheFile), ec);
    if (!Settings::writeFileAtomically(cacheFile, blob.GetBufferPointer(), blob.GetBufferSize())) {
        Logger::error(L"Unable to write the generated texture {}", cacheFile.wstring());
        return false;
    }

    // The texture is served from this session either way; without its key the next session generates it again.
    const auto fullKey = key + std::format("outputSize={}\n", blob.GetBufferSize());
    if (!Settings::writeFileAtomically(keyFileOf(cacheFile), fullKey.data(), fullKey.size()))
        Logger::warn(L"Unable to write the cache key of {}", cacheFile.wstring());

    return true;
}

std::shared_ptr<const std::vector<std::byte>> TextureGenerator::bytesFor(const std::string& path,
                                                                         [[maybe_unused]] SourceStream& source)
{
    std::filesystem::path cacheFile;
    {
        const std::scoped_lock lock(m_cacheFilesMutex);
        const auto it = m_cacheFiles.find(path);
        if (it == m_cacheFiles.end()) {
            Logger::error("No generated texture is registered for {}", path);
            return nullptr;
        }

        cacheFile = it->second;
    }

    try {
        std::error_code ec;
        const auto size = std::filesystem::file_size(cacheFile, ec);
        std::ifstream stream(cacheFile, std::ios::binary);
        if (ec || !stream) {
            Logger::error(L"Unable to open the generated texture {}", cacheFile.wstring());
            return nullptr;
        }

        auto bytes = std::make_shared<std::vector<std::byte>>(size);
        stream.read(reinterpret_cast<char*>(bytes->data()),
                    static_cast<std::streamsize>(size)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        if (!stream) {
            Logger::error(L"Unable to read the generated texture {}", cacheFile.wstring());
            return nullptr;
        }

        Logger::debug("Serving the generated texture {}", path);
        return bytes;
    } catch (const std::exception& e) {
        Logger::error("Unable to read the generated texture {}: {}", path, e.what());
        return nullptr;
    }
}

}
