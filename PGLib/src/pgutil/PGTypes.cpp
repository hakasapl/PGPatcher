#include "pgutil/PGTypes.hpp"

#include "util/StringUtil.hpp"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace PGTypes {
TextureSet textureSlotsFromStr(const std::string& slots)
{
    TextureSet textureSlots;
    std::vector<std::string> splitSlots;
    boost::split(splitSlots, slots, boost::is_any_of(","));
    for (size_t i = 0; i < splitSlots.size(); ++i)
        if (i < numTextureSlots)
            textureSlots.at(i) = StringUtil::utf8toUTF16(splitSlots[i]);
    return textureSlots;
}

std::string strFromTextureSlots(const TextureSet& slots)
{
    std::string strSlots;
    for (size_t i = 0; i < slots.size(); i++) {
        // Every slot gets its separator, so empty leading slots keep later slots in place.
        if (i)
            strSlots += ',';
        strSlots += StringUtil::utf16toUTF8(slots.at(i));
    }
    return strSlots;
}
}
