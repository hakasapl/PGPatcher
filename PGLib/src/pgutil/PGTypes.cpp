#include "pgutil/PGTypes.hpp"

#include "util/StringUtil.hpp"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace PGTypes {
auto getTextureSlotsFromStr(const std::string& slots) -> TextureSet
{
    TextureSet textureSlots;
    std::vector<std::string> splitSlots;
    boost::split(splitSlots, slots, boost::is_any_of(","));
    for (size_t i = 0; i < splitSlots.size(); ++i)
        if (i < numTextureSlots)
            textureSlots.at(i) = StringUtil::utf8toUTF16(splitSlots[i]);
    return textureSlots;
}

auto getStrFromTextureSlots(const TextureSet& slots) -> std::string
{
    std::string strSlots;
    for (const auto& slot : slots) {
        if (!strSlots.empty())
            strSlots += ',';
        strSlots += StringUtil::utf16toUTF8(slot);
    }
    return strSlots;
}
}
