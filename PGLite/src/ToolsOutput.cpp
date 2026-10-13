#include "PCH.h"

#include "ToolsOutput.hpp"

#include "PGDirectory.hpp"
#include "Plan.hpp"
#include "Resources.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "util/FileUtil.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <boost/crc.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace PGLite {

namespace {

/// @brief One mesh of the output: the member of a family served at one of its original paths.
struct Item {
    Family* family = nullptr;
    std::size_t member = 0;
    std::size_t pathIndex = 0;
    std::filesystem::path path;
};

std::uint32_t crc32Of(const std::vector<std::byte>& bytes)
{
    boost::crc_32_type crc;
    crc.process_bytes(bytes.data(), bytes.size());
    return crc.checksum();
}

/// @brief The original mesh of an item, read through the engine's stream as Family::original reads it.
std::shared_ptr<const std::vector<std::byte>> originalOf(const Item& item)
{
    if (item.pathIndex >= item.family->sources.size() || !item.family->sources.at(item.pathIndex))
        return nullptr;

    auto& source = *item.family->sources.at(item.pathIndex);
    auto bytes = std::make_shared<std::vector<std::byte>>();
    const std::scoped_lock lock(source.mutex);
    if (!source.stream || !Resources::readStream(*source.stream, *bytes))
        return nullptr;

    return bytes;
}

bool writeFile(const std::filesystem::path& file,
               const std::vector<std::byte>& bytes)
{
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
        return false;

    stream.write(reinterpret_cast<const char*>(bytes.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                 static_cast<std::streamsize>(bytes.size()));
    stream.close();
    return stream.good();
}

/**
 * @brief Makes the folder ready: a new or empty folder is used as it is, an earlier output of PGLite is emptied of
 * what PGLite writes, anything else is refused.
 *
 * @return The refusal key, empty when the folder is ready.
 */
std::string prepareFolder(const std::filesystem::path& folder)
{
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec)) {
        std::filesystem::create_directories(folder, ec);
        return ec ? "pglite.toolsOutput.folderNotWritable" : "";
    }

    if (!std::filesystem::is_directory(folder, ec))
        return "pglite.toolsOutput.folderNotEmpty";

    const bool isEmpty = std::filesystem::directory_iterator(folder, ec) == std::filesystem::directory_iterator();
    if (isEmpty)
        return "";

    if (!std::filesystem::exists(folder / ToolsOutput::markerFile, ec))
        return "pglite.toolsOutput.folderNotEmpty";

    // An earlier output: what PGLite writes goes, whatever else the user put there stays.
    for (const auto* const entry : { L"meshes", L"calientetools", L"textures" })
        std::filesystem::remove_all(folder / entry, ec);
    for (const auto* const entry : { ToolsOutput::markerFile, ToolsOutput::diffFile })
        std::filesystem::remove(folder / entry, ec);
    return "";
}

}

bool ToolsOutput::isScannedRecordType(PGPlugin::ModelRecordType type)
{
    using ModelRecordType = PGPlugin::ModelRecordType;
    return type == ModelRecordType::Activator || type == ModelRecordType::Container || type == ModelRecordType::Door
        || type == ModelRecordType::Furniture || type == ModelRecordType::MoveableStatic
        || type == ModelRecordType::StaticObject || type == ModelRecordType::Tree || type == ModelRecordType::Grass;
}

ToolsOutput::Result ToolsOutput::generate(const std::filesystem::path& folder,
                                          const Inputs& inputs)
{
    Result result;
    result.refusalKey = prepareFolder(folder);
    if (!result.refusalKey.empty()) {
        Logger::warn(L"The 3rd party tools output was not written to {}: {}",
                     folder.wstring(),
                     StringUtil::utf8toUTF16(result.refusalKey));
        return result;
    }

    Logger::info(L"Writing the 3rd party tools output to {}", folder.wstring());

    // The families of the record types DynDOLOD scans, at their original paths, with the member the original path
    // serves (variant 0). A family whose original path serves the unpatched mesh has nothing to write.
    std::vector<Item> items;
    for (const auto& family : inputs.plan.families()) {
        if (family->isFacegen || family->isBaseUnpatched || family->sources.empty())
            continue;

        const bool isScanned = std::ranges::any_of(family->members, [](const Family::Member& member) {
            return isScannedRecordType(member.attributes.recType);
        });
        if (!isScanned)
            continue;

        const auto member = std::ranges::find_if(
            family->members, [](const Family::Member& candidate) { return !candidate.variantNumber; });
        if (member == family->members.end())
            continue;

        const auto memberIndex = static_cast<std::size_t>(member - family->members.begin());
        for (std::size_t pathIndex = 0; pathIndex < family->basePaths.size(); pathIndex++)
            if (pathIndex < family->sources.size() && family->sources.at(pathIndex))
                items.push_back({ family.get(), memberIndex, pathIndex, family->basePaths.at(pathIndex) });
    }

    // The BodySlide ShapeData meshes have no record use and no family: a plan of their own, patched for the dummy use
    // the offline program patches them for.
    Plan bodySlidePlan(inputs.plan.generation());
    for (const auto& [path, cache] : inputs.directory.meshes()) {
        if (!PGNIFUtil::isBodySlideShapeDataMesh(path))
            continue;

        auto source = inputs.resolveSource(path);
        if (!source) {
            Logger::warn(L"The engine cannot resolve the ShapeData mesh {}, it is not written", path.wstring());
            result.failed++;
            continue;
        }

        auto& family = bodySlidePlan.addBodySlideFamily(path);
        family.sources.push_back(std::move(source));
        items.push_back({ &family, 0, 0, path });
    }

    // Every item is patched here if no open did, then written when the patch changed it.
    inputs.reportStage("pglite.stages.writingMeshes");
    nlohmann::json diff = nlohmann::json::object();
    for (std::size_t i = 0; i < items.size(); i++) {
        inputs.reportProgress(i, items.size());
        const auto& item = items.at(i);
        const Logger::Prefix prefix(item.path.wstring());
        try {
            const auto original = originalOf(item);
            const auto patched
                = item.family->bytes(item.member, item.pathIndex, inputs.textureSets, inputs.plan.generation());
            inputs.textures.generatePending();
            if (!original || !patched) {
                Logger::error("The mesh could not be read or patched, it is not written");
                result.failed++;
                continue;
            }

            const auto crcOriginal = crc32Of(*original);
            const auto crcPatched = crc32Of(*patched);
            if (crcOriginal == crcPatched) {
                result.unchanged++;
                continue;
            }

            if (!writeFile(folder / item.path, *patched)) {
                Logger::error("The patched mesh could not be written");
                result.failed++;
                continue;
            }

            auto& entry = diff[StringUtil::utf16toUTF8(item.path.wstring())];
            entry["crc32original"] = crcOriginal;
            entry["crc32patched"] = crcPatched;
            result.written++;
        } catch (const std::exception& e) {
            Logger::error("The mesh is not written: {}", e.what());
            result.failed++;
        }
    }

    // The textures the patches generated, from PGLite's cache, at the paths the meshes name.
    inputs.reportStage("pglite.stages.copyingTextures");
    for (const auto& [output, cacheFile] : inputs.textures.cachedOutputs()) {
        std::error_code ec;
        const auto target = folder / StringUtil::utf8toUTF16(output);
        std::filesystem::create_directories(target.parent_path(), ec);
        if (std::filesystem::copy_file(cacheFile, target, std::filesystem::copy_options::overwrite_existing, ec))
            result.textures++;
        else
            Logger::warn(L"The generated texture {} could not be copied: {}",
                         cacheFile.wstring(),
                         StringUtil::utf8toUTF16(ec.message()));
    }

    // The diff the tools read, and the marker PGLite reads.
    try {
        FileUtil::saveJSON(folder / diffFile, diff, true);
        nlohmann::json marker = nlohmann::json::object();
        marker["generator"] = "PGLite";
        marker["version"] = PG_FULL_VERSION;
        marker["meshes"] = result.written;
        marker["textures"] = result.textures;
        FileUtil::saveJSON(folder / markerFile, marker, true);
    } catch (const std::exception& e) {
        Logger::error("The output files could not be written: {}", e.what());
        return result;
    }

    Logger::info("3rd party tools output written: {} meshes ({} unchanged, {} failed), {} textures",
                 result.written,
                 result.unchanged,
                 result.failed,
                 result.textures);
    result.didSucceed = true;
    return result;
}

}
