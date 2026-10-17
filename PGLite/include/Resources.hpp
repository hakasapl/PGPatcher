#pragma once

#include "PGD3D.hpp"
#include "common/BethesdaDirectory.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace PGLite {

/**
 * @brief Thrown by a read that goes through the engine's resource layer, or of a texture file, when an input that is
 * there could not be read this time: a stream the engine resolves but cannot read, a header that cannot be read. A
 * patch that throws it is not kept, since a later open may read the input (Family::bytes). A path the engine cannot
 * resolve is not this: it is not there for the engine, whatever the file map says.
 */
class EngineReadError final : public BethesdaDirectory::ReadFailure {
public:
    using ReadFailure::ReadFailure;
};

/**
 * @brief Thrown in place of a new attempt while the retry interval of an earlier read failure runs (Family::serve):
 * the original is served, and no attempt is spent. No reader throws it, so it is no ReadFailure.
 */
class RememberedReadFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief How long after a read failure the runtime serves what it has without a new attempt: a family keeps serving
 * the original mesh (Family::bytes) and a failed generated texture is not made again (TextureGenerator). PGD3D
 * remembers a failed header read for the same time, so the attempt after the interval is a new read, unless another
 * family's lookup of the same header failed meanwhile and armed PGD3D again.
 */
constexpr std::chrono::seconds readFailureRetryInterval = PGD3D::defaultDDSMetadataRetryInterval;

/**
 * @brief The engine's resource layer (DESIGN.md section 3.1): the registry that maps a path to the stream the engine
 * reads it from, the resolver that gives the stream of any path the engine can load, and the grass loader's preload
 * of the grass meshes.
 *
 * The engine functions are called through the Address Library; none is hooked. Their IDs are known for the SE
 * runtime (1.5.97) and the AE runtimes (1.6.x and 1.7.x); the VR runtime (1.4.15), whose Address Library has no entry
 * for them, uses built-in offsets.
 */
class Resources {
public:
    /// @brief The Address Library file CommonLibSSE-NG loads for the running game version.
    [[nodiscard]] static std::filesystem::path addressLibraryFile();

    /**
     * @brief Registers a stream for a path in the engine's resource registry: a new path gets a record, an existing
     * record gets this stream instead of its own (the rule that makes loose files override archives). Takes the
     * registry lock itself.
     *
     * @param path Path relative to the data folder, as plugins write it.
     * @param stream The stream to register. The registry keeps a reference for the session.
     */
    static void registerStream(const std::string& path,
                               RE::BSTSmartPointer<RE::BSResource::Stream>& stream);

    /**
     * @brief Resolves a path to the stream the engine would read it from: the registry first (a loose record's stream
     * is cloned, an archive record gets an archive stream), then the file locations.
     *
     * @param path Path relative to the data folder.
     * @param[out] stream The stream, unopened.
     * @return true if the path resolved.
     */
    [[nodiscard]] static bool resolveStream(const std::string& path,
                                            RE::BSTSmartPointer<RE::BSResource::Stream>& stream);

    /**
     * @brief Whether a path resolves, see resolveStream().
     */
    [[nodiscard]] static bool exists(const std::string& path);

    /**
     * @brief Has the grass loader take its streams from the registry again. The engine copies the registry's stream
     * of every meshes\landscape\grass\*.nif into the grass model database when the grass manager is constructed
     * and once the plugins are loaded, and reads grass meshes from those copies without consulting the registry
     * again, so a grass stream registered after that is read only once this ran. Takes the registry locks itself.
     */
    static void refreshGrassStreams();

    /**
     * @brief Opens a stream, reads it to the end and closes it.
     *
     * @param stream The stream, which must not be open.
     * @param[out] bytes The contents.
     * @return true if the whole stream was read.
     */
    [[nodiscard]] static bool readStream(RE::BSResource::Stream& stream,
                                         std::vector<std::byte>& bytes);

    /**
     * @brief Reads a file through the resource layer: what the game itself would load for the path, with the engine's
     * archive precedence and the virtual file system of the mod manager.
     *
     * @param path Path relative to the data folder.
     * @param[out] bytes The contents.
     * @return true if the path resolved and was read.
     */
    [[nodiscard]] static bool readFile(const std::string& path,
                                       std::vector<std::byte>& bytes);
};

}
