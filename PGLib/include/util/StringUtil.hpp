#pragma once

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace StringUtil {

// narrow and wide string conversion functions
auto utf8toUTF16(const std::string& str) -> std::wstring;
auto utf16toUTF8(const std::wstring& str) -> std::string;
auto windows1252toUTF16(const std::string& str) -> std::wstring;
auto utf16toWindows1252(const std::wstring& str) -> std::string;
auto asciitoUTF16(const std::string& str) -> std::wstring;
auto utf16toASCII(const std::wstring& str) -> std::string;

auto utf8VectorToUTF16(const std::vector<std::string>& vec) -> std::vector<std::wstring>;
auto utf16VectorToUTF8(const std::vector<std::wstring>& vec) -> std::vector<std::string>;
auto windows1252VectorToUTF16(const std::vector<std::string>& vec) -> std::vector<std::wstring>;
auto utf16VectorToWindows1252(const std::vector<std::wstring>& vec) -> std::vector<std::string>;

auto containsOnlyAscii(const std::string& str) -> bool;
auto containsOnlyAscii(const std::wstring& str) -> bool;

auto toLowerASCII(const std::wstring& str) -> std::wstring;

auto checkIfStringInJSONArray(const nlohmann::json& json,
                              const std::string& str) -> bool;

auto toLowerASCIIFast(const std::string& str) -> std::string;
auto toLowerASCIIFast(const std::wstring& str) -> std::wstring;
auto toLowerASCIIFastInPlace(std::string& str) -> void;
auto toLowerASCIIFastInPlace(std::wstring& str) -> void;

template <typename StringType1,
          typename StringType2>
auto asciiFastIEquals(const StringType1& str1,
                      const StringType2& str2) -> bool
{
    return boost::equals(toLowerASCIIFast(str1), toLowerASCIIFast(str2));
}

} // namespace StringUtil
