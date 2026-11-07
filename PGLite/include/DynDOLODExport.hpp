#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>

namespace PGLite {

class Plan;
class RecordSnapshot;
class TextureSetFactory;

/**
 * @brief The file that tells DynDOLOD how PGLite patches the full models of its base records. DynDOLOD reads the meshes
 * on disk, which PGLite never patches, so its LOD would miss what the game shows. The file describes the PBR materials
 * today; its name and its "shader_type" leave room for more.
 *
 * The file is Data\SKSE\Plugins\PGLite\dyndolod_pglite_patches.json (schema\dyndolod_pglite_patches.schema.json),
 * written whole from the current plan: every record of a type DynDOLOD reads full models from
 * (PGPlugin::dynDOLODRecTypeSet()) whose model PGLite patches is described with the member of its family it belongs
 * to, alternate textures included, and listed when the patch applies a PBR entry to a shape of its model (MODL). A
 * record whose model string another plugin replaced after PGLite planned loads another mesh, so it is not listed. The
 * format, compact JSON:
 *
 *   {"version": 1,
 *    "materials": [{"shader_type": "pbr", "multilayer": true, "slot1": "pbr\\a.dds", ...}, {"delete": true}, ...],
 *    "records": {"<plugin>": {"<local form ID, 6 hex digits>": {"<3D index>": <material index>, ...}, ...}, ...}}
 *
 * A material is described as PatcherMeshShaderTruePBR::material() describes a shape: the attributes of the PBR
 * entries applied to it as the configs give them, the rest is the mesh's own; a shape the patch deleted has the
 * material {"delete": true}. Every distinct material is listed once. A record lists every shape of its model a PBR
 * entry applies to and every deleted shape by the shape's 3D index in the original mesh, the index alternate textures
 * use. A shape it does not list, and every shape of a record the file does not list, is drawn as the mesh on disk
 * says, also one that is PBR there already. Records are keyed by the plugin that defines them and their form ID within
 * it, so the file holds for any load order of the same plugins.
 */
class DynDOLODExport {
public:
    /// @brief What a write did.
    struct Result {
        bool didSucceed = false;
        /// @brief The records the file lists.
        std::size_t records = 0;
        /// @brief The distinct materials the file lists, the deletion included.
        std::size_t materials = 0;
        /// @brief The variants of a mesh (a member's file) that could not be described (logged); their records are not
        /// listed.
        std::size_t failed = 0;
    };

    using StageCallback = std::function<void(const char*)>;
    using ProgressCallback = std::function<void(std::size_t, std::size_t)>;

    /// @brief Data\SKSE\Plugins\PGLite\dyndolod_pglite_patches.json.
    [[nodiscard]] static std::filesystem::path file();

    /**
     * @brief Describes the families of the plan on as many threads as the machine has, the way their streams patch
     * them, and writes the file whole or not at all. A family no open patched yet first gets the patch its first open
     * would give it (Family::settle()), retries after read failures included, so the file describes what the game
     * keeps for the session; the families stay patched afterwards, as if the game had opened them. Under the shared
     * pipeline lock, while no Apply replaces the plan. Never throws: a failure is logged.
     *
     * @param snapshot The record fields PGLite edits, which tell the records that still load the planned mesh.
     * @param textureSets Where the first patches get the texture set objects of patched alternate textures.
     * @param reportStage Receives the translation key of each step.
     * @param reportProgress Receives the families described so far and the families to describe.
     */
    [[nodiscard]] static Result write(const Plan& plan,
                                      const RecordSnapshot& snapshot,
                                      TextureSetFactory& textureSets,
                                      const StageCallback& reportStage,
                                      const ProgressCallback& reportProgress);
};

}
