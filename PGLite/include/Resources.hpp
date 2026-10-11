#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace PGLite {

/**
 * @brief The engine's resource layer (DESIGN.md section 3.1): the registry that maps a path to the stream the engine
 * reads it from, and the resolver that gives the stream of any path the engine can load.
 *
 * Both engine functions are called through the Address Library; neither is hooked. Their IDs are only known for the
 * AE runtimes (1.6.x and 1.7.x), so on Skyrim SE 1.5.97 the runtime is unsupported and PGLite stays disabled.
 */
class Resources {
public:
    /// @brief Whether the running game has the two functions PGLite needs.
    [[nodiscard]] static bool isRuntimeSupported();

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
