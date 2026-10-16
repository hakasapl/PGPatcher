#pragma once

#include <cstddef>
#include <filesystem>
#include <unordered_set>

namespace PGLite {

/**
 * @brief The offline program's output in the load order (DESIGN.md section 4.14). PGLite accepts one generated in
 * PGLite mode, whose stock records file (PGPlugin::stockRecordsFile) says what its plugins changed, and puts those
 * records back to their stock model path and alternate textures before it reads the uses; the output's meshes carry
 * their stock state in PG_STOCK blocks, which the stock marker reverts when they are patched. Any other offline output
 * disables PGLite.
 */
class OfflineOutput {
public:
    /// @brief What restore() did.
    struct Result {
        /// @brief Record models put back to their stock model path and alternate textures.
        std::size_t restored = 0;
        /// @brief Record models of the file that hold neither the output's values nor the stock ones: another plugin
        /// changed them after the output was generated, so they are kept.
        std::size_t kept = 0;
        /// @brief Record models the output never changed that carried its duplicate paths or texture sets, copies of
        /// its records, given the stock values of the record they were copied from, or the original path and the
        /// stock texture sets.
        std::size_t mapped = 0;
        /// @brief Record models of the file the loaded game does not have, or whose stock texture sets it lacks.
        std::size_t unresolved = 0;
        /// @brief The textures the output generated, lower case data relative paths: inputs of no patch, so the
        /// file map leaves them out and the runtime generates its own.
        std::unordered_set<std::filesystem::path> generatedTextures;
    };

    /// @brief Whether an offline output is in the load order: its diff file resolves or its plugin is active.
    [[nodiscard]] static bool isPresent();

    /// @brief Whether the stock records file of an output generated in PGLite mode resolves.
    [[nodiscard]] static bool hasStockRecords();

    /**
     * @brief The textures the output generated (Result::generatedTextures), read from the stock records file alone,
     * so the file map can leave them out before the data is loaded; empty without the file.
     *
     * @throws std::runtime_error as restore() does.
     */
    [[nodiscard]] static std::unordered_set<std::filesystem::path> generatedTextures();

    /**
     * @brief Reads the stock records file and restores the records. At data load, before the uses are read, on the
     * thread that owns the records.
     *
     * @throws std::runtime_error if the file cannot be read or parsed, or is of a newer version.
     */
    static Result restore();
};

}
