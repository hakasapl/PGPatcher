#include "util/StringUtil.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <locale>
#include <string>
#include <vector>
#include <windows.h>

namespace StringUtil {

namespace {

constexpr unsigned asciiUpperBound = 127;
constexpr unsigned codePageUSASCII = 20127;
constexpr unsigned codePageWindows1252 = 1252;

/**
 * @brief Converts a narrow string of the given code page to UTF-16 with the Win32 conversion functions.
 */
std::wstring multiByteToWide(unsigned codePage,
                             const std::string& str)
{
    if (str.empty())
        return { };

    const int sizeNeeded = MultiByteToWideChar(codePage, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (sizeNeeded <= 0)
        return { };

    std::wstring wStr(static_cast<size_t>(sizeNeeded), 0);
    MultiByteToWideChar(codePage, 0, str.data(), static_cast<int>(str.size()), wStr.data(), sizeNeeded);
    return wStr;
}

/**
 * @brief Converts a UTF-16 string to a narrow string of the given code page with the Win32 conversion functions.
 * A character the code page cannot represent takes Windows' best fit character when there is one and is dropped
 * otherwise, as the boost.locale conversion this replaced did (its skip method asks for a substitute of 0 and erases
 * it).
 */
std::string wideToMultiByte(unsigned codePage,
                            const std::wstring& wStr)
{
    if (wStr.empty())
        return { };

    // UTF-8 represents every character, and the API refuses a default character for it.
    const bool isUTF8 = codePage == CP_UTF8;
    constexpr char substitute = '\0';
    const char* const defaultChar = isUTF8 ? nullptr : &substitute;
    const int sizeNeeded = WideCharToMultiByte(
        codePage, 0, wStr.data(), static_cast<int>(wStr.size()), nullptr, 0, defaultChar, nullptr);
    if (sizeNeeded <= 0)
        return { };

    std::string str(static_cast<size_t>(sizeNeeded), 0);
    BOOL usedDefault = FALSE;
    WideCharToMultiByte(codePage,
                        0,
                        wStr.data(),
                        static_cast<int>(wStr.size()),
                        str.data(),
                        sizeNeeded,
                        defaultChar,
                        isUTF8 ? nullptr : &usedDefault);
    if (usedDefault)
        std::erase(str, substitute);

    return str;
}

} // namespace

std::wstring windows1252toUTF16(const std::string& str) { return multiByteToWide(codePageWindows1252, str); }

std::string utf16toWindows1252(const std::wstring& str) { return wideToMultiByte(codePageWindows1252, str); }

std::wstring asciitoUTF16(const std::string& str) { return multiByteToWide(codePageUSASCII, str); }

std::string utf16toASCII(const std::wstring& str) { return wideToMultiByte(codePageUSASCII, str); }

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

std::wstring utf8toUTF16(const std::string& str) { return multiByteToWide(CP_UTF8, str); }

std::string utf16toUTF8(const std::wstring& wStr) { return wideToMultiByte(CP_UTF8, wStr); }

bool containsOnlyAscii(const std::string& str)
{
    // Compared as byte values, so every byte above 0x7F is rejected.
    return std::ranges::all_of(str, [](unsigned char ch) { return ch <= asciiUpperBound; });
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
