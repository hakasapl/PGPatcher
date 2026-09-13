#include "pgutil/PGEnums.hpp"

#include "util/EnumStringHelper.hpp"

#include <string>
#include <vector>

namespace PGEnums {
std::string strFromShader(const ShapeShader& shader)
{
    return std::string(EnumStringHelper::stringFromEnum(shader, shapeShaderTable, "Unknown"));
}

ShapeShader shaderFromStr(const std::string& shader)
{
    return EnumStringHelper::enumFromString(shader, shapeShaderTable, ShapeShader::Unknown);
}

std::string strFromTexType(const TextureType& type)
{
    return std::string(EnumStringHelper::stringFromEnum(type, textureTypeTable, "unknown"));
}

TextureType texTypeFromStr(const std::string& type)
{
    return EnumStringHelper::enumFromString(type, textureTypeTable, TextureType::Unknown);
}

std::vector<std::string> texTypesStr() { return EnumStringHelper::allEnumStrings(textureTypeTable); }
}
