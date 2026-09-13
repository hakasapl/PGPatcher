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

namespace PGNIFUtil {

static constexpr float minFloatComparison = 10e-05;

PGEnums::TextureSlots slotFromTexType(const PGEnums::TextureType& type);

/// @brief load a Nif from memory
/// @param[in] nifBytes memory containing the NIF
/// @return the nif
nifly::NifFile loadNIFFromBytes(const std::vector<std::byte>& nifBytes,
                                const bool& runChecks = true);

/// @brief get a map containing the known texture suffixes
/// @return the map containing the suffixes and the slot/type pairs
std::map<std::wstring,
         std::tuple<PGEnums::TextureSlots,
                    PGEnums::TextureType>>
texSuffixMap();

/// @brief Deduct the texture type and slot usually used from the suffix of a texture
/// @param[in] path texture to check
/// @return pair of texture slot and type of that texture
std::tuple<PGEnums::TextureSlots,
           PGEnums::TextureType>
defaultsFromSuffix(const std::filesystem::path& path);

/// @brief set the shader type of a given shader
/// @param nifShader the shader
/// @param[in] type type that is set
bool setShaderType(nifly::NiShader* nifShader,
                   const nifly::BSLightingShaderPropertyShaderType& type);

/// @brief set a new value for a float value
/// @param value the value to change
/// @param[in] newValue the new value to set
bool setShaderFloat(float& value,
                    const float& newValue);

/// @brief set a new value for a vector float value
/// @param value the value to change
/// @param[in] newValue the new value
/// @return
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

/// @brief set a given shader flag 1 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to set
bool setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                   const nifly::SkyrimShaderPropertyFlags1& flag);

/// @brief set a given shader flag 2 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to set
bool setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                   const nifly::SkyrimShaderPropertyFlags2& flag);

/// @brief clear all shader flags 1 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag1 to clear
bool clearShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                     const nifly::SkyrimShaderPropertyFlags1& flag);

/// @brief clear all shader flags 2 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag2 to clear
bool clearShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                     const nifly::SkyrimShaderPropertyFlags2& flag);

/// @brief set or unset a given shader flag 1 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to set
/// @param[in] enable enable or disable the flag
bool configureShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                         const nifly::SkyrimShaderPropertyFlags1& flag,
                         const bool& enable);

/// @brief set or unset a given shader flag 2 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to set
/// @param[in] enable enable or disable the flag
bool configureShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                         const nifly::SkyrimShaderPropertyFlags2& flag,
                         const bool& enable);

/// @brief set the path of a texture slot for a shape, unicode variant
/// @param nif nif
/// @param nifShape the shape
/// @param[in] slot texture slot
/// @param[in] texturePath the path to set
bool setTextureSlot(nifly::NifFile* nif,
                    nifly::NiShape* nifShape,
                    const PGEnums::TextureSlots& slot,
                    const std::wstring& texturePath);

/// @brief set the path of a texture slot for a shape
/// @param nif nif
/// @param nifShape the shape
/// @param[in] slot texture slot
/// @param[in] texturePath the path to set
bool setTextureSlot(nifly::NifFile* nif,
                    nifly::NiShape* nifShape,
                    const PGEnums::TextureSlots& slot,
                    const std::string& texturePath);

/// @brief set all paths of a texture slot for a shape
/// @param nif nif
/// @param nifShape the shape
/// @param[in] newSlots textures to set
bool setTextureSlots(nifly::NifFile* nif,
                     nifly::NiShape* nifShape,
                     const PGTypes::TextureSet& newSlots);

/// @brief get the set texture for a slot
/// @param nif nif
/// @param nifShape the shape
/// @param[in] slot the slot
/// @return texture set in the slot
std::string textureSlot(const nifly::NifFile* nif,
                        nifly::NiShape* nifShape,
                        const PGEnums::TextureSlots& slot);

/// @brief get all set texture slots from a shape
/// @param nif nif
/// @param nifShape shape
/// @return array of textures set in the slots
PGTypes::TextureSet textureSlots(const nifly::NifFile* nif,
                                 nifly::NiShape* nifShape);

PGTypes::TextureSetStr textureSetToStr(const PGTypes::TextureSet& set);

/// @brief get the texture name without suffix, i.e. without _n.dds
/// @param[in] texPath the path to get the base for
/// @return base path
std::wstring texBase(const std::filesystem::path& texPath,
                     const PGEnums::TextureSlots& slot = PGEnums::TextureSlots::Unknown);

/// @brief get the matching textures for a given base path
/// @param[in] base base texture name
/// @param[in] desiredType the type to find
/// @param[in] searchMap base names without suffix mapped to a set of potential textures. strings and paths must all be
/// lowercase
/// @return vector of textures
std::vector<PGTypes::PGTexture> texMatch(const std::wstring& base,
                                         const PGEnums::TextureType& desiredType,
                                         const std::map<std::wstring,
                                                        std::unordered_set<PGTypes::PGTexture,
                                                                           PGTypes::PGTextureHasher>>& searchMap);

/// @brief Gets all the texture prefixes for a textureset from a nif shape, ie. _n.dds is removed etc. for each slot
/// @param[in] nif the nif
/// @param nifShape the shape
/// @return array of texture names without suffixes
PGTypes::TextureSet searchPrefixes(nifly::NifFile const& nif,
                                   nifly::NiShape* nifShape,
                                   const bool& findBaseSlots = true);

/// @brief Gets all the texture prefixes for a texture set. ie. _n.dds is removed etc. for each slot
/// @param[in] oldSlots
/// @return array of texture names without suffixes
PGTypes::TextureSet searchPrefixes(const PGTypes::TextureSet& oldSlots,
                                   const bool& findBaseSlots = true);

std::vector<std::pair<nifly::NiShape*,
                      int>>
shapesWith3DIdx(const nifly::NifFile* nif);

bool isPatchableShape(const nifly::NifFile& nif,
                      nifly::NiShape& nifShape);

bool isShaderPatchableShape(nifly::NifFile& nif,
                            nifly::NiShape& nifShape);

bool isFacegenMesh(std::filesystem::path const& path);

} // namespace PGNIFUtil
