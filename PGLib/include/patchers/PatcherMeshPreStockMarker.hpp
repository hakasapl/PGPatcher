#pragma once

#include "patchers/base/PatcherMeshPre.hpp"
#include "pgutil/PGTypes.hpp"

#include "ExtraData.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

/**
 * @class PatcherMeshPreStockMarker
 * @brief Keeps the stock (unpatched) material of a shape in a PG_STOCK extra data block and reverts to it.
 *
 * A shape that carries a NiStringExtraData block named PG_STOCK is reverted to the texture set and shader state the
 * block describes before any other patcher runs. A mesh that was patched before, by PGPatcher or by a mod author, is
 * patched again from its stock state this way instead of on top of the previous result. The result is the same no
 * matter how often a mesh passes through PGPatcher, and a plugin use can resolve to a different shader than the one
 * the mesh ships with.
 *
 * The block holds only what differs from stock: the texture slots and shader fields a patch changed, each with its
 * stock value. Fields that are not listed are at their stock value already. Geometry (vertex colors, normals) is not
 * recorded on purpose: BodySlide carries it over from the ShapeData as it is, so there is nothing to revert.
 *
 * The block is only created on BodySlide ShapeData meshes (plugin uses flagged isBodySlideShapeData). BodySlide clones
 * the extra data of a ShapeData shape into every mesh it builds from it, so PGPatcher can patch the ShapeData first and
 * still patch the built meshes correctly in a later run. Any other mesh that has the block is reverted all the same,
 * which lets mods ship pre-patched meshes. pgtools can create the blocks for such a mod from the original and the
 * patched meshes.
 */
class PatcherMeshPreStockMarker : public PatcherMeshPre {
public:
    /// @brief Name of the NiStringExtraData block that holds the stock state of a shape.
    static constexpr const char* extraDataName = "PG_STOCK";

    /// @brief Version written into the block, so the format can change later without breaking old blocks.
    static constexpr unsigned formatVersion = 1;

    /**
     * @brief Get the Factory object
     *
     * @return PatcherMeshPre::PatcherMeshPreFactory
     */
    static PatcherMeshPre::PatcherMeshPreFactory factory();

    /**
     * @brief Construct a new PrePatcher Stock Marker patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshPreStockMarker(std::filesystem::path nifPath,
                              nifly::NifFile* nif);

    /**
     * @brief Reverts the shape to its stock state if it carries a PG_STOCK block, otherwise creates the block on
     * BodySlide ShapeData meshes
     *
     * After this ran the block holds the complete stock state of the shape; finalizeMarker() reduces it to the
     * differences once every patcher is done with the shape.
     *
     * @param[in,out] slots Texture slots of the shape as seen by the patchers. They are replaced by the stock slots
     * when they mirror the texture set of the NIF; an alternate texture from a plugin record is left alone.
     * @param nifShape Shape to patch
     * @return true Shape was reverted to its stock state
     * @return false Shape was not changed
     */
    bool applyPatch(PGTypes::TextureSet& slots,
                    nifly::NiShape& nifShape) override;

    /**
     * @brief Reduces the PG_STOCK block of a shape to the stock values that differ from its current state
     *
     * Called once every patcher ran on the shape. A block that ends up with no differences is emptied and removed by
     * removeEmptyMarkers().
     *
     * @param nif NIF the shape belongs to
     * @param nifShape Patched shape
     */
    static void finalizeMarker(nifly::NifFile& nif,
                               nifly::NiShape& nifShape);

    /**
     * @brief Removes the emptied PG_STOCK blocks of a NIF
     *
     * Called once per NIF after every shape went through finalizeMarker(). Blocks are only deleted here, after all
     * shapes are done, because deleting a block renumbers the blocks behind it.
     *
     * @param nif NIF to clean up
     */
    static void removeEmptyMarkers(nifly::NifFile& nif);

    /**
     * @brief Serializes the current material of a shape as the content of a PG_STOCK block
     *
     * @param nif NIF the shape belongs to
     * @param nifShape Shape to capture
     * @return Complete stock state of the shape
     */
    [[nodiscard]] static nlohmann::json captureStock(nifly::NifFile& nif,
                                                     nifly::NiShape& nifShape);

    /**
     * @brief Reduces a stock state to the values that differ from the current state of a shape
     *
     * @param nif NIF the shape belongs to
     * @param nifShape Shape to compare against
     * @param stock Stock state, as returned by captureStock()
     * @return Stock values that differ from the shape (only the version if nothing differs)
     */
    [[nodiscard]] static nlohmann::json diffStock(nifly::NifFile& nif,
                                                  nifly::NiShape& nifShape,
                                                  const nlohmann::json& stock);

    /**
     * @brief Checks whether a stock state holds no differences
     *
     * @param stock Stock state, as returned by diffStock()
     * @return true Nothing differs from stock
     * @return false At least one value differs from stock
     */
    [[nodiscard]] static bool isEmptyStock(const nlohmann::json& stock);

    /**
     * @brief Finds the PG_STOCK block of a shape
     *
     * @param nif NIF the shape belongs to
     * @param nifShape Shape to search
     * @return The block, or nullptr if the shape has none
     */
    [[nodiscard]] static nifly::NiStringExtraData* findMarker(nifly::NifFile& nif,
                                                              nifly::NiShape& nifShape);

    /**
     * @brief Creates or replaces the PG_STOCK block of a shape
     *
     * @param nif NIF the shape belongs to
     * @param nifShape Shape that gets the block
     * @param stock Content of the block
     */
    static void writeMarker(nifly::NifFile& nif,
                            nifly::NiShape& nifShape,
                            const nlohmann::json& stock);

    /**
     * @brief Creates PG_STOCK blocks on the shapes of a patched mesh from its unpatched counterpart
     *
     * Shapes are matched by name. A shape that only exists on one side is reported as a warning: a shape the patch
     * deleted cannot be restored. Shapes without differences get no block, and a stale block on such a shape is
     * removed.
     *
     * @param stockNif Unpatched mesh
     * @param patchedNif Patched mesh, which receives the blocks
     * @param meshLabel Name of the mesh for log messages
     * @return Number of shapes that received a block
     */
    static size_t markPatchedShapes(nifly::NifFile& stockNif,
                                    nifly::NifFile& patchedNif,
                                    const std::wstring& meshLabel);

private:
    /**
     * @brief Parses and validates the content of a PG_STOCK block
     *
     * @param marker Block to read
     * @param[out] stock Parsed content
     * @param[out] error Reason the block is unusable (only set when false is returned)
     * @return true The block is usable
     * @return false The block is empty, malformed or of a newer version
     */
    static bool parseMarker(const nifly::NiStringExtraData& marker,
                            nlohmann::json& stock,
                            std::string& error);

    /**
     * @brief Applies a complete stock state to a shape
     *
     * @param nif NIF the shape belongs to
     * @param nifShape Shape to revert
     * @param shader Lighting shader of the shape
     * @param stock Complete stock state
     * @param[in,out] slots Texture slots of the shape as seen by the patchers (see applyPatch())
     * @return true Something about the shape changed
     * @return false The shape already was in its stock state
     */
    static bool restoreStock(nifly::NifFile& nif,
                             nifly::NiShape& nifShape,
                             nifly::BSLightingShaderProperty& shader,
                             const nlohmann::json& stock,
                             PGTypes::TextureSet& slots);
};
