#pragma once

#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Object3d.hpp"
#include "Shaders.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

/**
 * @brief Helpers for reading and writing the shader and texture data of NIF shapes.
 *
 * Texture paths returned by these helpers are lower case. Setters return whether they changed anything so patchers can
 * tell a no-op patch from a real one.
 */
namespace PGNIFUtil {

/// @brief Tolerance of setShaderFloat(): smaller differences are not written
static constexpr float minFloatComparison = 10e-05;

/// @brief Get the texture slot a texture type is normally assigned to
/// @param[in] type texture type
/// @return the slot, TextureSlots::Unknown for TextureType::Unknown
PGEnums::TextureSlots slotFromTexType(const PGEnums::TextureType& type);

/// @brief Load a NIF from memory
/// @param[in] nifBytes memory containing the NIF
/// @param[in] shouldRunChecks also reject NIFs with a null shape, a dangling texture set reference or a non-ASCII
/// texture path
/// @return the NIF
/// @throws std::runtime_error if the bytes are empty, nifly cannot load them (access violations are caught) or a check
/// fails
nifly::NifFile loadNIFFromBytes(const std::vector<std::byte>& nifBytes,
                                const bool& shouldRunChecks = true);

/// @brief get a map containing the known texture suffixes
/// @return the map containing the suffixes and the slot/type pairs
const std::map<std::wstring,
               std::tuple<PGEnums::TextureSlots,
                          PGEnums::TextureType>>&
texSuffixMap();

/// @brief Deduce the texture slot and type a texture is usually used for from its file name suffix
/// @param[in] path texture to check (ASCII)
/// @return slot and type, both Unknown if no known suffix matches. A "_p" texture under textures\pbr is HeightPBR
std::tuple<PGEnums::TextureSlots,
           PGEnums::TextureType>
defaultsFromSuffix(const std::filesystem::path& path);

/// @brief Set the shader type of a shader
/// @param nifShader the shader
/// @param[in] type type to set
/// @return true if the type changed
bool setShaderType(nifly::NiShader* nifShader,
                   const nifly::BSLightingShaderPropertyShaderType& type);

/// @brief Set a float shader field unless it is already within minFloatComparison of the new value
/// @param value the field to change
/// @param[in] newValue the new value to set
/// @return true if the field changed
bool setShaderFloat(float& value,
                    const float& newValue);

/// @brief Set a vector shader field unless it already has the new value
/// @param value the field to change
/// @param[in] newValue the new value
/// @return true if the field changed
bool setShaderVec2(nifly::Vector2& value,
                   const nifly::Vector2& newValue);

/// @brief check if a given flag is set for a shader
/// @param nifShaderBSLSP the shader to check
/// @param[in] flag the flag to check
/// @return if the flag is set
bool hasShaderFlag(const nifly::BSShaderProperty* nifShaderBSLSP,
                   const nifly::SkyrimShaderPropertyFlags1& flag);

/// @brief check if a given flag is set for a shader
/// @param nifShaderBSLSP the shader to check
/// @param[in] flag the flag to check
/// @return if the flag is set
bool hasShaderFlag(const nifly::BSShaderProperty* nifShaderBSLSP,
                   const nifly::SkyrimShaderPropertyFlags2& flag);

/// @brief Set a shader flag 1 on a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to set
/// @return true if the flag was not set before
bool setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                   const nifly::SkyrimShaderPropertyFlags1& flag);

/// @brief Set a shader flag 2 on a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to set
/// @return true if the flag was not set before
bool setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                   const nifly::SkyrimShaderPropertyFlags2& flag);

/// @brief Clear a shader flag 1 on a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to clear
/// @return true if the flag was set before
bool clearShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                     const nifly::SkyrimShaderPropertyFlags1& flag);

/// @brief Clear a shader flag 2 on a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to clear
/// @return true if the flag was set before
bool clearShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                     const nifly::SkyrimShaderPropertyFlags2& flag);

/// @brief Set or clear a shader flag 1 on a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to change
/// @param[in] enable true to set the flag, false to clear it
/// @return true if the flag changed
bool configureShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                         const nifly::SkyrimShaderPropertyFlags1& flag,
                         const bool& enable);

/// @brief Set or clear a shader flag 2 on a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to change
/// @param[in] enable true to set the flag, false to clear it
/// @return true if the flag changed
bool configureShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                         const nifly::SkyrimShaderPropertyFlags2& flag,
                         const bool& enable);

// Shared blocks. Vanilla meshes share alpha properties and texture sets between shapes, and a mesh can share a shader
// between shapes as well. setAlphaProperty(), setTextureSlot() and setTextureSlots() change a block for the given
// shape alone: a block that other blocks also reference is copied for the shape first, and a block only the shape uses
// is changed in place. The original keeps its other users either way, so no block is left unreferenced. A shader is
// written by many patchers through its pointer, so unshareShader() copies a shared one once, before the shape is
// patched; the copy keeps the texture set until a write copies that too. PGNIFUtil.cpp implements this once for every
// kind of block. A block with a controller is the exception: the controller animates that one block, so the block
// stays shared and changes for every shape that uses it; shapes on a shader that stays shared also share its texture
// set and every change to it. The number of texture slots is also changed in place for every sharer.
// deleteAlphaProperty() follows the same rule: it only deletes blocks the shape alone uses, and the links of an
// animation sequence to a deleted controller go with it, together with what only those links used.

/// @brief Give a shape its own lighting shader if other shapes share it
///
/// The patchers write the shader of a shape through its pointer, so a shared shader is copied once before the shape
/// is patched instead of at each write. The copy references the same texture set, which is copied on its first write.
/// The shape patchers write lighting shaders only, so no other shader type is copied, and a shader with a controller
/// stays shared together with its texture set (see the note on shared blocks above).
///
/// @param nif the NIF
/// @param nifShape the shape
/// @return true if the shader was copied
bool unshareShader(nifly::NifFile& nif,
                   nifly::NiShape& nifShape);

/// @brief Set the flags and the alpha test threshold of the NiAlphaProperty of a shape
///
/// A shape without an NiAlphaProperty only gets one when shouldCreate is set, with the default values for whatever
/// flags and threshold leave out.
///
/// @param nif the NIF
/// @param nifShape the shape
/// @param[in] flags new flags, or nothing to keep the current ones
/// @param[in] threshold new alpha test threshold, or nothing to keep the current one
/// @param[in] shouldCreate whether a shape without an alpha property gets one
/// @return true if the alpha property of the shape changed or was created
bool setAlphaProperty(nifly::NifFile& nif,
                      nifly::NiShape& nifShape,
                      const std::optional<uint16_t>& flags,
                      const std::optional<uint8_t>& threshold,
                      bool shouldCreate);

/// @brief Remove the NiAlphaProperty from a shape
///
/// Other blocks that share the alpha property keep it. Otherwise it is deleted, together with every block only it uses,
/// such as an alpha test controller; the links of an animation sequence to that controller go with it, together with
/// the interpolators only those links used.
///
/// @param nif the NIF
/// @param nifShape the shape
/// @return true if the shape had an alpha property
bool deleteAlphaProperty(nifly::NifFile& nif,
                         nifly::NiShape& nifShape);

/// @brief Set the texture of one slot of a shape, wide string variant (the path must be ASCII)
///
/// A texture set other shapes share is copied for the shape first, see the note on shared blocks above.
///
/// @param nif the NIF
/// @param nifShape the shape
/// @param[in] slot texture slot
/// @param[in] texturePath the path to set
/// @return true if the slot changed (compared without regard to case)
bool setTextureSlot(nifly::NifFile* nif,
                    nifly::NiShape* nifShape,
                    const PGEnums::TextureSlots& slot,
                    const std::wstring& texturePath);

/// @brief Set the texture of one slot of a shape
///
/// A texture set other shapes share is copied for the shape first, see the note on shared blocks above.
///
/// @param nif the NIF
/// @param nifShape the shape
/// @param[in] slot texture slot
/// @param[in] texturePath the path to set
/// @return true if the slot changed (compared without regard to case)
bool setTextureSlot(nifly::NifFile* nif,
                    nifly::NiShape* nifShape,
                    const PGEnums::TextureSlots& slot,
                    const std::string& texturePath);

/// @brief Set every texture slot of a shape
///
/// A texture set other shapes share is copied for the shape first, see the note on shared blocks above.
///
/// @param nif the NIF
/// @param nifShape the shape
/// @param[in] newSlots textures to set
/// @return true if any slot changed (compared without regard to case)
bool setTextureSlots(nifly::NifFile* nif,
                     nifly::NiShape* nifShape,
                     const PGTypes::TextureSet& newSlots);

/// @brief Get the texture of one slot of a shape
/// @param nif the NIF
/// @param nifShape the shape
/// @param[in] slot the slot
/// @return the lower case texture path, empty if the slot is empty
std::string textureSlot(const nifly::NifFile* nif,
                        nifly::NiShape* nifShape,
                        const PGEnums::TextureSlots& slot);

/// @brief Get every texture slot of a shape
/// @param nif the NIF
/// @param nifShape the shape
/// @return lower case texture paths by slot, empty for empty or missing slots
PGTypes::TextureSet textureSlots(const nifly::NifFile* nif,
                                 nifly::NiShape* nifShape);

/// @brief Convert a texture set to UTF-8 strings
PGTypes::TextureSetStr textureSetToStr(const PGTypes::TextureSet& set);

/// @brief Get the lower case path of a texture without extension and, for a known slot, without the slot's suffix
/// (e.g. "_n" for the normal slot)
/// @param[in] texPath the texture path
/// @param[in] slot the slot the texture is used in, Unknown to only remove the extension
/// @return the base path
std::wstring texBase(const std::filesystem::path& texPath,
                     const PGEnums::TextureSlots& slot = PGEnums::TextureSlots::Unknown);

/// @brief Find the textures of a type that share a base path. The lookup is recorded for the update cache
/// @param[in] base base texture path (any case)
/// @param[in] desiredType the type to find
/// @param[in] searchMap lower case base paths mapped to the textures that share them, see PGDirectory::textureMap()
/// @return the matching textures, in no particular order
std::vector<PGTypes::PGTexture> texMatch(const std::wstring& base,
                                         const PGEnums::TextureType& desiredType,
                                         const std::map<std::wstring,
                                                        std::unordered_set<PGTypes::PGTexture,
                                                                           PGTypes::PGTextureHasher>>& searchMap);

/// @brief Get the base path (see texBase()) of every texture slot of a shape
/// @param[in] nif the NIF
/// @param nifShape the shape
/// @param[in] shouldFindBaseSlots also remove the slot's suffix, not only the extension
/// @return base paths by slot, empty for empty slots
PGTypes::TextureSet searchPrefixes(nifly::NifFile const& nif,
                                   nifly::NiShape* nifShape,
                                   const bool& shouldFindBaseSlots = true);

/// @brief Get the base path (see texBase()) of every slot of a texture set
/// @param[in] oldSlots texture set
/// @param[in] shouldFindBaseSlots also remove the slot's suffix, not only the extension
/// @return base paths by slot, empty for empty slots
PGTypes::TextureSet searchPrefixes(const PGTypes::TextureSet& oldSlots,
                                   const bool& shouldFindBaseSlots = true);

/// @brief Get the shapes of a NIF with their 3D index, the index plugins use for alternate textures: shapes and
/// particle systems counted in block tree order
/// @param[in] nif the NIF
/// @return shapes with their 3D index, in tree order
/// @throws std::runtime_error if nif is null
std::vector<std::pair<nifly::NiShape*,
                      int>>
shapesWith3DIdx(const nifly::NifFile* nif);

/// @brief Check whether a shape is a tri shape type PGPatcher handles and has a shader
bool isPatchableShape(const nifly::NifFile& nif,
                      nifly::NiShape& nifShape);

/// @brief Check whether a shape stores vertex positions. A shape without the vertex flag holds zero vectors instead,
/// and so does a BSDynamicTriShape, whose positions live in its dynamic data, so none of them can give bounds.
bool hasVertexPositions(nifly::NiShape* nifShape);

/// @brief Check whether a shape can get a different shader: it is patchable, its shader is a BSLightingShaderProperty
/// with a texture set, and neither the shape nor the shader carries a PG_IGNORE boolean extra data set to true
bool isShaderPatchableShape(nifly::NifFile& nif,
                            nifly::NiShape& nifShape);

/// @brief The folder of the facegen head meshes, relative to the data folder, lower case.
constexpr const wchar_t* facegenFolder = L"meshes\\actors\\character\\facegendata\\facegeom";

/// @brief Checks whether a NIF is a facegen mesh (under facegenFolder)
/// @param[in] path relative path of the NIF (any case)
bool isFacegenMesh(std::filesystem::path const& path);

/// @brief Checks whether a NIF is a BodySlide ShapeData mesh, which BodySlide builds its output meshes from
/// @param[in] path relative path of the NIF
/// @return true if the NIF is under CalienteTools\BodySlide\ShapeData
bool isBodySlideShapeDataMesh(std::filesystem::path const& path);

} // namespace PGNIFUtil
