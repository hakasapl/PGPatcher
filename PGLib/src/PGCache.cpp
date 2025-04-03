#include "PGCache.hpp"
#include "NIFUtil.hpp"
#include "ParallaxGenUtil.hpp"
#include <filesystem>
#include <utility>

#include <boost/crc.hpp>
#include <boost/iostreams/stream.hpp>

using namespace std;

// statics
ParallaxGenDirectory* PGCache::s_pgd = nullptr;
bool PGCache::s_cacheEnabled = false;
nlohmann::json PGCache::s_nifCache = nlohmann::json::object();
std::mutex PGCache::s_nifCacheMutex = std::mutex();

void PGCache::loadStatics(ParallaxGenDirectory* pgd, const bool& cacheEnabled)
{
    s_pgd = pgd;
    s_cacheEnabled = cacheEnabled;
}

void PGCache::loadCache(const nlohmann::json& cache) { s_nifCache = cache; }
void PGCache::clearCache() { s_nifCache.clear(); }
auto PGCache::getCache() -> nlohmann::json { return s_nifCache; }

auto PGCache::getNIFMeta(const filesystem::path& nifPath, nifly::NifFile* nif) -> NIFMeta
{
    bool cacheValid = true;
    if (!s_cacheEnabled) {
        cacheValid = false;
    }

    // convert nifPath to lookup string
    const auto nifKey = ParallaxGenUtil::utf16toUTF8(nifPath.wstring());

    // check if key exists in cache
    nlohmann::json curCache;
    if (cacheValid) {
        {
            const lock_guard<mutex> lock(s_nifCacheMutex);
            if (s_nifCache.contains(nifKey)) {
                curCache = s_nifCache[nifKey];
            } else {
                cacheValid = false;
            }
        }

        // check that invalidation fields exist
        cacheValid &= curCache.contains("mtime");
        cacheValid &= curCache.contains("size");

        // Check if cache should be invalidated
        if (cacheValid) {
            const auto cacheMTime = curCache["mtime"].get<size_t>();
            const auto nifMTime = s_pgd->getFileMTime(nifPath);
            cacheValid &= cacheMTime == nifMTime;
        }

        if (cacheValid) {
            const auto cacheSize = curCache["size"].get<uintmax_t>();
            const auto nifSize = s_pgd->getFileSize(nifPath);
            cacheValid &= cacheSize == nifSize;
        }

        // check if NIF exists in output
        cacheValid &= filesystem::exists(s_pgd->getGeneratedPath() / nifPath);

        // verify all fields to complete NIFMeta exist
        if (cacheValid) {
            cacheValid &= curCache.contains("oldcrc32") && curCache["oldcrc32"].is_number_unsigned();
            cacheValid &= curCache.contains("newcrc32") && curCache["oldcrc32"].is_number_unsigned();
            cacheValid &= curCache.contains("shapes") && curCache["shapes"].is_array();
        }
    }

    // If cache is valid, build NIFMeta from cache
    if (cacheValid) {
        NIFMeta meta;
        meta.fromCache = cacheValid;
        meta.oldCRC32 = curCache["oldcrc32"].get<unsigned int>();
        meta.newCRC32 = curCache["newcrc32"].get<unsigned int>();
        for (const auto& shape : curCache["shapes"]) {
            NIFShapeMeta shapeMeta;
            for (const auto& [key, value] : shape["canApply"].items()) {
                shapeMeta.canApply[NIFUtil::getShaderFromStr(key)] = value.get<bool>();
            }

            shapeMeta.winningMatch = PatcherUtil::ShaderPatcherMatch::fromJSON(shape["winningMatch"]);

            for (int i = 0; cmp_less(i, NUM_TEXTURE_SLOTS); i++) {
                shapeMeta.textures.at(i) = ParallaxGenUtil::utf8toUTF16(shape["textures"].at(i).get<string>());
            }

            meta.shapes.push_back(shapeMeta);
        }

        return meta;
    }

    // We need to load the NIF and get this info from there
    NIFMeta meta;
    meta.fromCache = false;

    // Load NIF
    loadNIF(nifPath, nif, &meta.oldCRC32);

    // Fill in shape info
    for (const auto& nifShape : nif->GetShapes()) {
        NIFShapeMeta shapeMeta;
        shapeMeta.textures = NIFUtil::getTextureSlots(nif, nifShape);
        meta.shapes.push_back(shapeMeta);
    }

    return meta;
}

void PGCache::setNIFMeta(const std::filesystem::path& nifPath, const NIFMeta& meta)
{
    if (!s_cacheEnabled) {
        return;
    }

    // convert nifPath to lookup string
    const auto nifKey = ParallaxGenUtil::utf16toUTF8(nifPath.wstring());

    // build cache object
    nlohmann::json cacheObj;
    cacheObj["mtime"] = s_pgd->getFileMTime(nifPath);
    cacheObj["size"] = s_pgd->getFileSize(nifPath);
    cacheObj["oldcrc32"] = meta.oldCRC32;
    cacheObj["newcrc32"] = meta.newCRC32;

    // build shapes array
    nlohmann::json shapesArray = nlohmann::json::array();
    for (const auto& shape : meta.shapes) {
        nlohmann::json shapeObj;
        for (const auto& [key, value] : shape.canApply) {
            shapeObj[NIFUtil::getStrFromShader(key)] = value;
        }

        shapeObj["winningMatch"] = shape.winningMatch.getJSON();

        nlohmann::json texturesArray = nlohmann::json::array();
        for (int i = 0; cmp_less(i, NUM_TEXTURE_SLOTS); i++) {
            texturesArray.push_back(ParallaxGenUtil::utf16toUTF8(shape.textures.at(i)));
        }
        shapeObj["textures"] = texturesArray;

        shapesArray.push_back(shapeObj);
    }
    cacheObj["shapes"] = shapesArray;

    // save to cache
    const lock_guard<mutex> lock(s_nifCacheMutex);
    s_nifCache[nifKey] = cacheObj;
}

auto PGCache::loadNIF(const filesystem::path& nifPath, nifly::NifFile* nif, unsigned int* oldCRC32) -> bool
{
    if (nif == nullptr) {
        return false;
    }

    // Load NIF file
    vector<std::byte> nifFileData;
    try {
        nifFileData = s_pgd->getFile(nifPath);
        if (nifFileData.empty()) {
            throw runtime_error("File is empty");
        }

        // Convert Byte Vector to Stream
        // Using reinterpret_cast to convert from std::byte to char is more efficient due to less copies
        boost::iostreams::array_source nifArraySource(
            reinterpret_cast<const char*>(nifFileData.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            nifFileData.size());
        boost::iostreams::stream<boost::iostreams::array_source> nifStream(nifArraySource);

        nif->Load(nifStream);
        if (!nif->IsValid()) {
            throw runtime_error("Invalid NIF");
        }
    } catch (const exception& e) {
        return false;
    }

    // Calculate CRC32 hash before
    if (oldCRC32 != nullptr) {
        boost::crc_32_type crcBeforeResult {};
        crcBeforeResult.process_bytes(nifFileData.data(), nifFileData.size());
        *oldCRC32 = crcBeforeResult.checksum();
    }

    return true;
}

auto PGCache::saveNIF(const std::filesystem::path& nifPath, nifly::NifFile* nif, unsigned int* newCRC32) -> bool
{
    if (nif == nullptr) {
        return false;
    }

    const auto outputPath = s_pgd->getGeneratedPath() / nifPath;

    // create directories if required
    filesystem::create_directories(outputPath.parent_path());

    if (nif->Save(outputPath, { .optimize = false, .sortBlocks = false }) != 0) {
        return false;
    }

    if (newCRC32 != nullptr) {
        // Calculate CRC32 hash after
        // TODO I need to find a way to do this while saving, I shouldn't have to reread after saving
        const auto outputFileBytes = ParallaxGenUtil::getFileBytes(outputPath);
        boost::crc_32_type crcResultAfter {};
        crcResultAfter.process_bytes(outputFileBytes.data(), outputFileBytes.size());
        *newCRC32 = crcResultAfter.checksum();
    }

    return true;
}
