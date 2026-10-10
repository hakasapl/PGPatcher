#pragma once

#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Object3d.hpp"
#include "Shaders.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
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

/// @brief Set the texture of one slot of a shape, wide string variant (the path must be ASCII)
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
/// @param nif the NIF
/// @param nifShape the shape
/// @param[in] newSlots textures to set
/// @return true if any slot changed
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

/// @brief Check whether a shape can get a different shader: it is patchable, its shader is a BSLightingShaderProperty
/// with a texture set, and neither the shape nor the shader carries a PG_IGNORE boolean extra data set to true
bool isShaderPatchableShape(nifly::NifFile& nif,
                            nifly::NiShape& nifShape);

/// @brief Checks whether a NIF is a facegen mesh (under meshes\actors\character\facegendata\facegeom)
/// @param[in] path lower case relative path of the NIF
bool isFacegenMesh(std::filesystem::path const& path);

/// @brief Checks whether a NIF is a BodySlide ShapeData mesh, which BodySlide builds its output meshes from
/// @param[in] path relative path of the NIF
/// @return true if the NIF is under CalienteTools\BodySlide\ShapeData
bool isBodySlideShapeDataMesh(std::filesystem::path const& path);

} // namespace PGNIFUtil
