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

using namespace std;
namespace StringUtil {

constexpr unsigned ASCII_UPPER_BOUND = 127;

auto windows1252toUTF16(const std::string& str) -> std::wstring
{
    return boost::locale::conv::to_utf<wchar_t>(str, "windows-1252");
}

auto utf16toWindows1252(const std::wstring& str) -> std::string
{
    return boost::locale::conv::from_utf<wchar_t>(str, "windows-1252");
}

auto asciitoUTF16(const std::string& str) -> std::wstring
{
    return boost::locale::conv::to_utf<wchar_t>(str, "US-ASCII");
}

auto utf16toASCII(const std::wstring& str) -> std::string
{
    return boost::locale::conv::from_utf<wchar_t>(str, "US-ASCII");
}

auto utf8VectorToUTF16(const vector<string>& vec) -> vector<wstring>
{
    vector<wstring> out;
    out.reserve(vec.size());
    for (const auto& item : vec) {
        out.push_back(utf8toUTF16(item));
    }

    return out;
}

auto utf16VectorToUTF8(const vector<wstring>& vec) -> vector<string>
{
    vector<string> out;
    out.reserve(vec.size());
    for (const auto& item : vec) {
        out.push_back(utf16toUTF8(item));
    }

    return out;
}

auto windows1252VectorToUTF16(const vector<string>& vec) -> vector<wstring>
{
    vector<wstring> out;
    out.reserve(vec.size());
    for (const auto& item : vec) {
        out.push_back(windows1252toUTF16(item));
    }

    return out;
}

auto utf16VectorToWindows1252(const vector<wstring>& vec) -> vector<string>
{
    vector<string> out;
    out.reserve(vec.size());
    for (const auto& item : vec) {
        out.push_back(utf16toWindows1252(item));
    }

    return out;
}

auto toLowerASCII(const std::wstring& str) -> std::wstring { return boost::to_lower_copy(str, std::locale::classic()); }

auto utf8toUTF16(const string& str) -> wstring
{
    // Just return empty string if empty
    if (str.empty()) {
        return {};
    }

    // Convert string > wstring
    const int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), nullptr, 0);
    std::wstring wStr(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.length(), wStr.data(), sizeNeeded);

    return wStr;
}

auto utf16toUTF8(const wstring& wStr) -> string
{
    // Just return empty string if empty
    if (wStr.empty()) {
        return {};
    }

    // Convert wstring > string
    const int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wStr.data(), (int)wStr.size(), nullptr, 0, nullptr, nullptr);
    string str(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wStr.data(), (int)wStr.size(), str.data(), sizeNeeded, nullptr, nullptr);

    return str;
}

auto containsOnlyAscii(const std::string& str) -> bool
{
    return std::ranges::all_of(str, [](char wc) { return wc <= ASCII_UPPER_BOUND; });
}

auto containsOnlyAscii(const std::wstring& str) -> bool
{
    return std::ranges::all_of(str, [](wchar_t wc) { return wc <= ASCII_UPPER_BOUND; });
}

auto checkIfStringInJSONArray(const nlohmann::json& json,
                              const string& str) -> bool
{
    if (json.is_array()) {
        for (const auto& item : json) {
            if (item.is_string() && item.get<string>() == str) {
                return true;
            }
        }
    }
    return false;
}

auto toLowerASCIIFast(const std::string& str) -> std::string
{
    std::string lowerStr = str;
    for (char& ch : lowerStr) {
        if (ch >= 'A' && ch <= 'Z') {
            ch += ('a' - 'A');
        }
    }
    return lowerStr;
}

auto toLowerASCIIFast(const std::wstring& str) -> std::wstring
{
    std::wstring lowerStr = str;
    for (wchar_t& ch : lowerStr) {
        if (ch >= L'A' && ch <= L'Z') {
            ch += (L'a' - L'A');
        }
    }
    return lowerStr;
}

auto toLowerASCIIFastInPlace(std::string& str) -> void
{
    for (char& ch : str) {
        if (ch >= 'A' && ch <= 'Z') {
            ch += ('a' - 'A');
        }
    }
}

auto toLowerASCIIFastInPlace(std::wstring& str) -> void
{
    for (wchar_t& ch : str) {
        if (ch >= L'A' && ch <= L'Z') {
            ch += (L'a' - L'A');
        }
    }
}

} // namespace StringUtil
