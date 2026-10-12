#include "PCH.h"

#include "TextureGenerator.hpp"

#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGRunObserver.hpp"
#include "PatchStream.hpp"
#include "Resources.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
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
#include <utility>
#include <vector>
#include <winerror.h>

namespace PGLite {

namespace {

/// @brief The first line of every key file; a change here invalidates every cached texture.
constexpr std::string_view keyHeader = "PGLite texture cache 1";

const wchar_t* hookName(PGRunObserver::HookKind kind)
{
    return kind == PGRunObserver::HookKind::ConvertToCM ? L"ParallaxToCM" : L"SSSFix";
}

std::filesystem::path outputOf(PGRunObserver::HookKind kind,
                               const std::filesystem::path& texPath)
{
    return kind == PGRunObserver::HookKind::ConvertToCM ? PatcherTextureHookConvertToCM::outputFilename(texPath)
                                                        : PatcherTextureHookFixSSS::outputFilename(texPath);
}

PGEnums::TextureType typeOf(PGRunObserver::HookKind kind)
{
    return kind == PGRunObserver::HookKind::ConvertToCM ? PGEnums::TextureType::ComplexMaterial
                                                        : PGEnums::TextureType::SubsurfaceColor;
}

std::uint32_t signatureOf(PGRunObserver::HookKind kind)
{
    return kind == PGRunObserver::HookKind::ConvertToCM ? PatcherTextureHookConvertToCM::generatorSignature()
                                                        : PatcherTextureHookFixSSS::generatorSignature();
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

/// @brief Writes a file through a temporary file next to it and a rename, so the file is complete or absent.
bool writeFile(const std::filesystem::path& file,
               const void* data,
               std::size_t size)
{
    auto temporary = file;
    temporary += L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
            return false;

        stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!stream)
            return false;
    }

    std::error_code ec;
    std::filesystem::rename(temporary, file, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }

    return true;
}

}

TextureGenerator::TextureGenerator(std::filesystem::path cacheFolder)
    : m_cacheFolder(std::move(cacheFolder))
    , m_noSource(std::make_shared<SourceStream>())
{
}

void TextureGenerator::generatePending()
{
    const std::scoped_lock lock(m_mutex);
    for (const auto& texPath : PatcherTextureHookConvertToCM::processList())
        process(HookKind::ConvertToCM, texPath);
    for (const auto& texPath : PatcherTextureHookFixSSS::processList())
        process(HookKind::FixSSS, texPath);
}

void TextureGenerator::onEnvironmentReplaced(PGDirectory& directory)
{
    const std::scoped_lock lock(m_mutex);
    for (const auto& [output, entry] : m_entries) {
        if (!entry.isServed)
            continue;

        directory.addGeneratedFile(output);
        directory.setTextureType(output, entry.type);
    }

    // The next patch registers its textures with the new directory, as the first patch did with the old one.
    PatcherTextureHookConvertToCM::reset();
    PatcherTextureHookFixSSS::reset();
}

TextureGenerator::Stats TextureGenerator::stats() const
{
    return { .generated = m_generated, .fromCache = m_fromCache, .failed = m_failed };
}

void TextureGenerator::process(HookKind kind,
                               const std::filesystem::path& texPath)
{
    const auto output = outputOf(kind, texPath);
    if (m_entries.contains(output))
        return;

    // Recorded first: a texture that fails is not tried again for every mesh that names it. Offline logs it once too.
    auto& entry = m_entries[output];
    entry.type = typeOf(kind);

    try {
        const auto started = std::chrono::steady_clock::now();

        // The source as the engine serves it: the loose file or archive entry the game itself loads for the path.
        std::vector<std::byte> source;
        if (!Resources::readFile(pathString(texPath), source)) {
            Logger::error(L"Unable to read {} to generate {}", texPath.wstring(), output.wstring());
            m_failed++;
            return;
        }

        const auto key = cacheKey(kind, texPath, source);
        const auto cacheFile = m_cacheFolder / output;
        if (isCached(cacheFile, key)) {
            m_fromCache++;
            Logger::debug(L"The cached {} is current", output.wstring());
        } else {
            if (!generate(kind, texPath, source, cacheFile, key)) {
                m_failed++;
                return;
            }

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
        PGGlobals::pgd()->setTextureType(output, entry.type);

        const auto outputStr = pathString(output);
        {
            const std::scoped_lock lock(m_cacheFilesMutex);
            m_cacheFiles[outputStr] = cacheFile;
        }

        // Registered like a mesh (DESIGN.md section 3.1): a file of the same name in the load order is shadowed, as the
        // offline output folder shadows it. The registry keeps the stream for the session.
        RE::BSTSmartPointer<RE::BSResource::Stream> stream(new PatchStream(outputStr, m_noSource, *this));
        Resources::registerStream(outputStr, stream);
        entry.isServed = true;
    } catch (const std::exception& e) {
        Logger::error(L"Unable to generate {} from {}: {}",
                      output.wstring(),
                      texPath.wstring(),
                      StringUtil::utf8toUTF16(e.what()));
        m_failed++;
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
        StringUtil::utf16toUTF8(hookName(kind)),
        signatureOf(kind),
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

    std::error_code ec;
    std::filesystem::create_directories(cacheFile.parent_path(), ec);
    if (!writeFile(cacheFile, blob.GetBufferPointer(), blob.GetBufferSize())) {
        Logger::error(L"Unable to write the generated texture {}", cacheFile.wstring());
        return false;
    }

    // The texture is served from this session either way; without its key the next session generates it again.
    const auto fullKey = key + std::format("outputSize={}\n", blob.GetBufferSize());
    if (!writeFile(keyFileOf(cacheFile), fullKey.data(), fullKey.size()))
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
