#include "PCH.h"

#include "Resources.hpp"

#include "Settings.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace PGLite {

namespace {

// The register-path-with-stream function (1.7.99 0x140ECEE30) and the path-to-stream resolver (1.7.99 0x140ECE860),
// Address Library IDs of the AE runtimes. Verified against the installed 1.7.104 and the 1.6.1170 library.
constexpr std::uint64_t registerPathID = 69845;
constexpr std::uint64_t resolvePathID = 69839;

using RegisterPathFn = bool (*)(const char* path,
                                RE::BSTSmartPointer<RE::BSResource::Stream>& stream);
using ResolvePathFn = std::uint32_t (*)(const char* path,
                                        RE::BSTSmartPointer<RE::BSResource::Stream>& stream,
                                        bool isWritable,
                                        RE::BSResource::Location* location);

}

bool Resources::isRuntimeSupported() { return REL::Module::IsAE(); }

std::filesystem::path Resources::addressLibraryFile()
{
    const auto version = REL::Module::get().version().string();
    const auto name
        = REL::Module::IsAE() ? std::format("versionlib-{}.bin", version) : std::format("version-{}.bin", version);
    return Settings::pluginsFolder() / name;
}

bool Resources::registerStream(const std::string& path,
                               RE::BSTSmartPointer<RE::BSResource::Stream>& stream)
{
    if (!isRuntimeSupported())
        return false;

    static const REL::Relocation<RegisterPathFn> registerPath { REL::RelocationID(0, registerPathID) };
    return registerPath(path.c_str(), stream);
}

bool Resources::resolveStream(const std::string& path,
                              RE::BSTSmartPointer<RE::BSResource::Stream>& stream)
{
    if (!isRuntimeSupported())
        return false;

    static const REL::Relocation<ResolvePathFn> resolvePath { REL::RelocationID(0, resolvePathID) };
    const auto error = static_cast<RE::BSResource::ErrorCode>(resolvePath(path.c_str(), stream, false, nullptr));
    return error == RE::BSResource::ErrorCode::kNone && stream;
}

bool Resources::exists(const std::string& path)
{
    RE::BSTSmartPointer<RE::BSResource::Stream> stream;
    return resolveStream(path, stream);
}

bool Resources::readStream(RE::BSResource::Stream& stream,
                           std::vector<std::byte>& bytes)
{
    bytes.clear();
    if (stream.DoOpen() != RE::BSResource::ErrorCode::kNone)
        return false;

    // DoOpen sets the size of the contents.
    const std::uint64_t size = stream.totalSize;
    bytes.resize(size);

    std::uint64_t total = 0;
    while (total < size) {
        std::uint64_t read = 0;
        const auto error = stream.DoRead(bytes.data() + total, size - total, read);
        if (error != RE::BSResource::ErrorCode::kNone || !read)
            break;

        total += read;
    }

    stream.DoClose();

    if (total != size) {
        bytes.clear();
        return false;
    }

    return true;
}

bool Resources::readFile(const std::string& path,
                         std::vector<std::byte>& bytes)
{
    RE::BSTSmartPointer<RE::BSResource::Stream> stream;
    if (!resolveStream(path, stream))
        return false;

    return readStream(*stream, bytes);
}

}
