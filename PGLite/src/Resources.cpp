#include "PCH.h"

#include "Resources.hpp"

#include "Settings.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <tuple>
#include <vector>

namespace PGLite {

namespace {

// The register-path-with-stream function and the path-to-stream resolver, Address Library IDs of the SE and the AE
// runtimes. AE: 1.7.99 0x140ECEE30 and 0x140ECE860, verified against the installed 1.7.104 and the 1.6.1170 library.
// SE: 1.5.97 0x140C45280 and 0x140C44CB0, found in the 1.5.97 database through the same bound
// BSResourceNiBinaryStream constructor, with the insert semantics checked (an existing loose record gets the stream,
// an archive record is converted), DESIGN.md section 3.1.
constexpr std::uint64_t registerPathSEID = 68491;
constexpr std::uint64_t registerPathAEID = 69845;
constexpr std::uint64_t resolvePathSEID = 68484;
constexpr std::uint64_t resolvePathAEID = 69839;

using RegisterPathFn = bool (*)(const char* path,
                                RE::BSTSmartPointer<RE::BSResource::Stream>& stream);
using ResolvePathFn = std::uint32_t (*)(const char* path,
                                        RE::BSTSmartPointer<RE::BSResource::Stream>& stream,
                                        bool isWritable,
                                        RE::BSResource::Location* location);

}

std::filesystem::path Resources::addressLibraryFile()
{
    const auto version = REL::Module::get().version().string();
    const auto name
        = REL::Module::IsAE() ? std::format("versionlib-{}.bin", version) : std::format("version-{}.bin", version);
    return Settings::pluginsFolder() / name;
}

void Resources::registerStream(const std::string& path,
                               RE::BSTSmartPointer<RE::BSResource::Stream>& stream)
{
    // The return value is not used: on AE it says whether the file type has an entry database, on SE it is whatever
    // the registry unlock left in the register.
    static const REL::Relocation<RegisterPathFn> registerPath { REL::RelocationID(registerPathSEID, registerPathAEID) };
    std::ignore = registerPath(path.c_str(), stream);
}

bool Resources::resolveStream(const std::string& path,
                              RE::BSTSmartPointer<RE::BSResource::Stream>& stream)
{
    static const REL::Relocation<ResolvePathFn> resolvePath { REL::RelocationID(resolvePathSEID, resolvePathAEID) };
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
