#pragma once

#include "Plan.hpp"
#include "RecordEdits.hpp"
#include "TextureGenerator.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

class PGDirectory;

namespace PGLite {

/**
 * @brief The output for the tools that read patched meshes from disk, TexGen, DynDOLOD and BodySlide (DESIGN.md
 * section 4.14): a mod folder with the patched models of the record types DynDOLOD scans (activators, containers,
 * doors, furniture, movable statics, statics, trees and grass), the BodySlide ShapeData meshes, the textures the
 * patches generated, ParallaxGen_Diff.json as the offline program writes it, and a marker file by which PGLite
 * recognizes its own output in the load order and disables itself. The whole plan is written, not what the game has
 * loaded: every family is patched here if its first open did not.
 */
class ToolsOutput {
public:
    struct Result {
        bool didSucceed = false;
        std::size_t written = 0;
        std::size_t unchanged = 0;
        std::size_t failed = 0;
        std::size_t textures = 0;
        /// @brief The translation key of the reason the generation did not run, empty when it ran.
        std::string refusalKey;
    };

    struct Inputs {
        const Plan& plan;
        TextureSetFactory& textureSets;
        TextureGenerator& textures;
        PGDirectory& directory;
        /// @brief The engine's stream for a data-relative path, for the ShapeData meshes the plan has no family for.
        std::function<std::shared_ptr<SourceStream>(const std::filesystem::path&)> resolveSource;
        std::function<void(const char*)> reportStage;
        std::function<void(std::size_t, std::size_t)> reportProgress;
    };

    /// @brief The marker at the root of the output; PGLite disables itself while it resolves in the load order.
    static constexpr const char* markerFile = "pglite_toolsoutput.json";
    /// @brief The diff the offline program writes, which TexGen and DynDOLOD read for the original checksums.
    static constexpr const char* diffFile = "ParallaxGen_Diff.json";

    /**
     * @brief Writes the output into a folder: an empty one, a new one, or an earlier output of PGLite (its marker),
     * which is replaced. Any other folder with contents is refused.
     *
     * @param folder The mod folder.
     * @param inputs The plan and what patching needs.
     * @return What was written; refusalKey names the reason when nothing was.
     */
    [[nodiscard]] static Result generate(const std::filesystem::path& folder,
                                         const Inputs& inputs);

    /// @brief Whether DynDOLOD scans the record type for LOD (its reference: ACTI, CONT, DOOR, FURN, MSTT, STAT, TREE
    /// and GRAS).
    [[nodiscard]] static bool isScannedRecordType(PGPlugin::ModelRecordType type);
};

}
