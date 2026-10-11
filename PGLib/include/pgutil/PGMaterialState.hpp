#pragma once

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

/**
 * @brief The material of a shape as the patchers see it: the fields of its BSLightingShaderProperty, its texture set
 * and its NiAlphaProperty.
 *
 * One table lists every field the SSE mesh format stores for a BSLightingShaderProperty. The permutation tracker
 * compares materials with it to tell patched meshes apart, and the stock marker records and restores materials with
 * it, so a field a patcher writes cannot be covered by one and forgotten by the other.
 */
namespace PGMaterialState {

/**
 * @brief Serializes every shader field
 *
 * The keys follow the field names NifSkope shows for a BSLightingShaderProperty. Floats are rounded to nine
 * significant digits, which reads back as the same float.
 *
 * @param shader Shader to serialize
 * @return JSON object with one entry per field
 */
nlohmann::json shaderToJSON(const nifly::BSLightingShaderProperty& shader);

/**
 * @brief Applies every field present in the JSON to the shader
 *
 * A value that does not have the type of its field is ignored.
 *
 * @param shader Shader to change
 * @param fields JSON object as returned by shaderToJSON(), possibly with fields left out
 * @return true The shader changed
 * @return false Every field already had its value
 */
bool shaderFromJSON(nifly::BSLightingShaderProperty& shader,
                    const nlohmann::json& fields);

/**
 * @brief Compares every shader field exactly
 *
 * @param shaderA First shader
 * @param shaderB Second shader
 * @return true Every field is equal
 * @return false At least one field differs
 */
bool isShaderEqual(const nifly::BSLightingShaderProperty& shaderA,
                   const nifly::BSLightingShaderProperty& shaderB);

/**
 * @brief Serializes the texture slots of a shape
 *
 * @param nif NIF the shape belongs to
 * @param nifShape Shape to serialize
 * @return JSON object with the lower case path of every slot, keyed by slot index
 */
nlohmann::json textureSlotsToJSON(const nifly::NifFile& nif,
                                  nifly::NiShape& nifShape);

/**
 * @brief Compares two texture sets slot by slot without regard to case
 *
 * A slot one set has and the other does not counts as empty.
 *
 * @param texSetA First texture set
 * @param texSetB Second texture set
 * @return true Every slot is equal
 * @return false At least one slot differs
 */
bool isTextureSetEqual(const nifly::BSShaderTextureSet& texSetA,
                       const nifly::BSShaderTextureSet& texSetB);

/**
 * @brief Serializes the NiAlphaProperty of a shape
 *
 * The keys follow the field names NifSkope shows for an NiAlphaProperty.
 *
 * @param nif NIF the shape belongs to
 * @param nifShape Shape to serialize
 * @return JSON object with the flags and the threshold, or null if the shape has no alpha property
 */
nlohmann::json alphaPropertyToJSON(const nifly::NifFile& nif,
                                   nifly::NiShape& nifShape);

/**
 * @brief Applies an alpha property state to a shape
 *
 * Null removes the alpha property of the shape (see PGNIFUtil::deleteAlphaProperty()). An object sets every field it
 * has, through PGNIFUtil::setAlphaProperty(), and gives a shape without an alpha property one with the default values
 * for the fields it leaves out. A value that does not have the type of its field is ignored.
 *
 * @param nif NIF the shape belongs to
 * @param nifShape Shape to change
 * @param state JSON as returned by alphaPropertyToJSON(), possibly with fields left out
 * @return true The alpha property of the shape changed
 * @return false The shape already had this state
 */
bool alphaPropertyFromJSON(nifly::NifFile& nif,
                           nifly::NiShape& nifShape,
                           const nlohmann::json& state);

/**
 * @brief Compares two alpha properties exactly
 *
 * @param alphaA First alpha property, nullptr for a shape without one
 * @param alphaB Second alpha property, nullptr for a shape without one
 * @return true Neither exists, or every field is equal
 * @return false Only one exists, or at least one field differs
 */
bool isAlphaPropertyEqual(const nifly::NiAlphaProperty* alphaA,
                          const nifly::NiAlphaProperty* alphaB);

/**
 * @brief Reads a JSON number as one of the unsigned integer types the mesh format stores
 *
 * nlohmann stores a non-negative integer as unsigned when it was parsed, but as signed when it was built from an int
 * in code, so both integer kinds are accepted. A float without a fraction, such as 128.0, counts as that integer, as
 * it does for an "integer" of a JSON schema. A negative number, a fraction and a number the type cannot hold are
 * malformed. PBR JSON entries and PG_STOCK blocks read the fields of the mesh format with it, so they accept the same
 * values.
 *
 * @param value JSON value to read
 * @param[out] out Receives the number, and is left unchanged if the value is malformed
 * @return true The value is a number of the type
 * @return false The value is malformed
 */
template<typename T>
bool unsignedFromJSON(const nlohmann::json& value,
                      T& out)
{
    if (value.is_number_integer()) {
        const auto asInteger = value.get<int64_t>();
        if (asInteger < 0 || std::cmp_greater(asInteger, std::numeric_limits<T>::max()))
            return false;

        out = static_cast<T>(asInteger);
        return true;
    }

    if (!value.is_number_float())
        return false;

    // The bound is one past the largest value, which a double holds exactly for every unsigned type, while the largest
    // 64 bit value itself would round up to it.
    const auto number = value.get<double>();
    if (number < 0 || number >= static_cast<double>(std::numeric_limits<T>::max()) + 1 || std::trunc(number) != number)
        return false;

    out = static_cast<T>(number);
    return true;
}

} // namespace PGMaterialState
