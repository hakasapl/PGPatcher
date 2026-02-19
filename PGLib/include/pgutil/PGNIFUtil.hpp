#pragma once

#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Object3d.hpp"
#include "Shaders.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace PGNIFUtil {

static constexpr float MIN_FLOAT_COMPARISON = 10e-05F;

auto getSlotFromTexType(const PGEnums::TextureType& type) -> PGEnums::TextureSlots;

/// @brief load a Nif from memory
/// @param[in] nifBytes memory containing the NIF
/// @return the nif
auto loadNIFFromBytes(const std::vector<std::byte>& nifBytes,
                      const bool& runChecks = true) -> nifly::NifFile;

/// @brief get a map containing the known texture suffixes
/// @return the map containing the suffixes and the slot/type pairs
auto getTexSuffixMap() -> std::map<std::wstring,
                                   std::tuple<PGEnums::TextureSlots,
                                              PGEnums::TextureType>>;

/// @brief Deduct the texture type and slot usually used from the suffix of a texture
/// @param[in] path texture to check
/// @return pair of texture slot and type of that texture
auto getDefaultsFromSuffix(const std::filesystem::path& path) -> std::tuple<PGEnums::TextureSlots,
                                                                            PGEnums::TextureType>;

/// @brief set the shader type of a given shader
/// @param nifShader the shader
/// @param[in] type type that is set
auto setShaderType(nifly::NiShader* nifShader,
                   const nifly::BSLightingShaderPropertyShaderType& type) -> bool;

/// @brief set a new value for a float value
/// @param value the value to change
/// @param[in] newValue the new value to set
auto setShaderFloat(float& value,
                    const float& newValue) -> bool;

/// @brief set a new value for a vector float value
/// @param value the value to change
/// @param[in] newValue the new value
/// @return
auto setShaderVec2(nifly::Vector2& value,
                   const nifly::Vector2& newValue) -> bool;

/// @brief check if a given flag is set for a shader
/// @param nifShaderBSLSP the shader to check
/// @param[in] flag the flag to check
/// @return if the flag is set
auto hasShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                   const nifly::SkyrimShaderPropertyFlags1& flag) -> bool;

/// @brief check if a given flag is set for a shader
/// @param nifShaderBSLSP the shader to check
/// @param[in] flag the flag to check
/// @return if the flag is set
auto hasShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                   const nifly::SkyrimShaderPropertyFlags2& flag) -> bool;

/// @brief set a given shader flag 1 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to set
auto setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                   const nifly::SkyrimShaderPropertyFlags1& flag) -> bool;

/// @brief set a given shader flag 2 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to set
auto setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                   const nifly::SkyrimShaderPropertyFlags2& flag) -> bool;

/// @brief clear all shader flags 1 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag1 to clear
auto clearShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                     const nifly::SkyrimShaderPropertyFlags1& flag) -> bool;

/// @brief clear all shader flags 2 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag2 to clear
auto clearShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                     const nifly::SkyrimShaderPropertyFlags2& flag) -> bool;

/// @brief set or unset a given shader flag 1 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to set
/// @param[in] enable enable or disable the flag
auto configureShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                         const nifly::SkyrimShaderPropertyFlags1& flag,
                         const bool& enable) -> bool;

/// @brief set or unset a given shader flag 2 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to set
/// @param[in] enable enable or disable the flag
auto configureShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                         const nifly::SkyrimShaderPropertyFlags2& flag,
                         const bool& enable) -> bool;

/// @brief set the path of a texture slot for a shape, unicode variant
/// @param nif nif
/// @param nifShape the shape
/// @param[in] slot texture slot
/// @param[in] texturePath the path to set
auto setTextureSlot(nifly::NifFile* nif,
                    nifly::NiShape* nifShape,
                    const PGEnums::TextureSlots& slot,
                    const std::wstring& texturePath) -> bool;

/// @brief set the path of a texture slot for a shape
/// @param nif nif
/// @param nifShape the shape
/// @param[in] slot texture slot
/// @param[in] texturePath the path to set
auto setTextureSlot(nifly::NifFile* nif,
                    nifly::NiShape* nifShape,
                    const PGEnums::TextureSlots& slot,
                    const std::string& texturePath) -> bool;

/// @brief set all paths of a texture slot for a shape
/// @param nif nif
/// @param nifShape the shape
/// @param[in] newSlots textures to set
auto setTextureSlots(nifly::NifFile* nif,
                     nifly::NiShape* nifShape,
                     const std::array<std::wstring,
                                      NUM_TEXTURE_SLOTS>& newSlots) -> bool;

/// @brief get all set texture slots from a shape
/// @param nif nif
/// @param nifShape shape
/// @return array of textures set in the slots
auto getTextureSlots(nifly::NifFile* nif,
                     nifly::NiShape* nifShape) -> PGTypes::TextureSet;

/// @brief get the texture name without suffix, i.e. without _n.dds
/// @param[in] texPath the path to get the base for
/// @return base path
auto getTexBase(const std::filesystem::path& texPath,
                const PGEnums::TextureSlots& slot = PGEnums::TextureSlots::UNKNOWN) -> std::wstring;

/// @brief get the matching textures for a given base path
/// @param[in] base base texture name
/// @param[in] desiredType the type to find
/// @param[in] searchMap base names without suffix mapped to a set of potential textures. strings and paths must all be
/// lowercase
/// @return vector of textures
auto getTexMatch(const std::wstring& base,
                 const PGEnums::TextureType& desiredType,
                 const std::map<std::wstring,
                                std::unordered_set<PGTypes::PGTexture,
                                                   PGTypes::PGTextureHasher>>& searchMap)
    -> std::vector<PGTypes::PGTexture>;

/// @brief Gets all the texture prefixes for a textureset from a nif shape, ie. _n.dds is removed etc. for each slot
/// @param[in] nif the nif
/// @param nifShape the shape
/// @return array of texture names without suffixes
auto getSearchPrefixes(nifly::NifFile const& nif,
                       nifly::NiShape* nifShape,
                       const bool& findBaseSlots = true) -> std::array<std::wstring,
                                                                       NUM_TEXTURE_SLOTS>;

/// @brief Gets all the texture prefixes for a texture set. ie. _n.dds is removed etc. for each slot
/// @param[in] oldSlots
/// @return array of texture names without suffixes
auto getSearchPrefixes(const std::array<std::wstring,
                                        NUM_TEXTURE_SLOTS>& oldSlots,
                       const bool& findBaseSlots = true) -> std::array<std::wstring,
                                                                       NUM_TEXTURE_SLOTS>;

auto getShapesWithBlockIDs(const nifly::NifFile* nif) -> std::unordered_map<nifly::NiShape*,
                                                                            int>;

auto isPatchableShape(nifly::NifFile& nif,
                      nifly::NiShape& nifShape) -> bool;

auto isShaderPatchableShape(nifly::NifFile& nif,
                            nifly::NiShape& nifShape) -> bool;

} // namespace PGNIFUtil
