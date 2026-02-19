#include "pgutil/PGEnums.hpp"

#include "util/EnumStringHelper.hpp"

#include <string>
#include <vector>

using namespace std;

namespace PGEnums {
auto getStrFromShader(const ShapeShader& shader) -> string
{
    return std::string(EnumStringHelper::stringFromEnum(shader, SHAPESHADER_TABLE, "Unknown"));
}

auto getShaderFromStr(const string& shader) -> ShapeShader
{
    return EnumStringHelper::enumFromString(shader, SHAPESHADER_TABLE, ShapeShader::UNKNOWN);
}

auto getStrFromTexType(const TextureType& type) -> string
{
    return std::string(EnumStringHelper::stringFromEnum(type, TEXTURETYPE_TABLE, "unknown"));
}

auto getTexTypeFromStr(const string& type) -> TextureType
{
    return EnumStringHelper::enumFromString(type, TEXTURETYPE_TABLE, TextureType::UNKNOWN);
}

auto getTexTypesStr() -> std::vector<std::string> { return EnumStringHelper::allEnumStrings(TEXTURETYPE_TABLE); }
}
