#include "pgutil/PGEnums.hpp"

#include "util/EnumStringHelper.hpp"

#include <string>
#include <vector>

namespace PGEnums {
auto getStrFromShader(const ShapeShader& shader) -> std::string
{
    return std::string(EnumStringHelper::stringFromEnum(shader, shapeShaderTable, "Unknown"));
}

auto getShaderFromStr(const std::string& shader) -> ShapeShader
{
    return EnumStringHelper::enumFromString(shader, shapeShaderTable, ShapeShader::Unknown);
}

auto getStrFromTexType(const TextureType& type) -> std::string
{
    return std::string(EnumStringHelper::stringFromEnum(type, textureTypeTable, "unknown"));
}

auto getTexTypeFromStr(const std::string& type) -> TextureType
{
    return EnumStringHelper::enumFromString(type, textureTypeTable, TextureType::Unknown);
}

auto getTexTypesStr() -> std::vector<std::string> { return EnumStringHelper::allEnumStrings(textureTypeTable); }
}
