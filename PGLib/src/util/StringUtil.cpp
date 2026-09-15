#include "util/StringUtil.hpp"

#include <DirectXTex.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/locale.hpp>
#include <boost/locale/encoding.hpp>
#include <nlohmann/detail/output/serializer.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <locale>
#include <string>
#include <stringapiset.h>
#include <vector>
#include <wingdi.h>
#include <winnls.h>
#include <winnt.h>

namespace StringUtil {

constexpr unsigned asciiUpperBound = 127;

std::wstring windows1252toUTF16(const std::string& str)
{
    return boost::locale::conv::to_utf<wchar_t>(str, "windows-1252");
}

std::string utf16toWindows1252(const std::wstring& str)
{
    return boost::locale::conv::from_utf<wchar_t>(str, "windows-1252");
}

std::wstring asciitoUTF16(const std::string& str) { return boost::locale::conv::to_utf<wchar_t>(str, "US-ASCII"); }

std::string utf16toASCII(const std::wstring& str) { return boost::locale::conv::from_utf<wchar_t>(str, "US-ASCII"); }

std::vector<std::wstring> utf8VectorToUTF16(const std::vector<std::string>& vec)
{
    std::vector<std::wstring> out;
    out.reserve(vec.size());
    for (const auto& item : vec)
        out.push_back(utf8toUTF16(item));

    return out;
}

std::vector<std::string> utf16VectorToUTF8(const std::vector<std::wstring>& vec)
{
    std::vector<std::string> out;
    out.reserve(vec.size());
    for (const auto& item : vec)
        out.push_back(utf16toUTF8(item));

    return out;
}

std::vector<std::wstring> windows1252VectorToUTF16(const std::vector<std::string>& vec)
{
    std::vector<std::wstring> out;
    out.reserve(vec.size());
    for (const auto& item : vec)
        out.push_back(windows1252toUTF16(item));

    return out;
}

std::vector<std::string> utf16VectorToWindows1252(const std::vector<std::wstring>& vec)
{
    std::vector<std::string> out;
    out.reserve(vec.size());
    for (const auto& item : vec)
        out.push_back(utf16toWindows1252(item));

    return out;
}

std::vector<std::wstring> asciiVectorToUTF16(const std::vector<std::string>& vec)
{
    std::vector<std::wstring> out;
    out.reserve(vec.size());
    for (const auto& item : vec)
        out.push_back(asciitoUTF16(item));

    return out;
}

std::vector<std::string> utf16VectorToASCII(const std::vector<std::wstring>& vec)
{
    std::vector<std::string> out;
    out.reserve(vec.size());
    for (const auto& item : vec)
        out.push_back(utf16toASCII(item));

    return out;
}

std::wstring toLowerASCII(const std::wstring& str) { return boost::to_lower_copy(str, std::locale::classic()); }

std::wstring utf8toUTF16(const std::string& str)
{
    // Just return empty string if empty.
    if (str.empty())
        return { };

    // Convert string > wstring.
    const int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
    std::wstring wStr(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.length()), wStr.data(), sizeNeeded);

    return wStr;
}

std::string utf16toUTF8(const std::wstring& wStr)
{
    // Just return empty string if empty.
    if (wStr.empty())
        return { };

    // Convert wstring > string.
    const int sizeNeeded
        = WideCharToMultiByte(CP_UTF8, 0, wStr.data(), static_cast<int>(wStr.size()), nullptr, 0, nullptr, nullptr);
    std::string str(sizeNeeded, 0);
    WideCharToMultiByte(
        CP_UTF8, 0, wStr.data(), static_cast<int>(wStr.size()), str.data(), sizeNeeded, nullptr, nullptr);

    return str;
}

bool containsOnlyAscii(const std::string& str)
{
    return std::ranges::all_of(str, [](char wc) { return wc <= asciiUpperBound; });
}

bool containsOnlyAscii(const std::wstring& str)
{
    return std::ranges::all_of(str, [](wchar_t wc) { return wc <= asciiUpperBound; });
}

bool checkIfStringInJSONArray(const nlohmann::json& json,
                              const std::string& str)
{
    if (json.is_array()) {
        for (const auto& item : json)
            if (item.is_string() && item.get<std::string>() == str)
                return true;
    }
    return false;
}

std::string toLowerASCIIFast(const std::string& str)
{
    std::string lowerStr = str;
    for (char& ch : lowerStr)
        if (ch >= 'A' && ch <= 'Z')
            ch += ('a' - 'A');
    return lowerStr;
}

std::wstring toLowerASCIIFast(const std::wstring& str)
{
    std::wstring lowerStr = str;
    for (wchar_t& ch : lowerStr)
        if (ch >= L'A' && ch <= L'Z')
            ch += (L'a' - L'A');
    return lowerStr;
}

void toLowerASCIIFastInPlace(std::string& str)
{
    for (char& ch : str)
        if (ch >= 'A' && ch <= 'Z')
            ch += ('a' - 'A');
}

void toLowerASCIIFastInPlace(std::wstring& str)
{
    for (wchar_t& ch : str)
        if (ch >= L'A' && ch <= L'Z')
            ch += (L'a' - L'A');
}

} // namespace StringUtil
