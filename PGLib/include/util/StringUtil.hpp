#pragma once

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

/**
 * @brief Utility functions for string encoding conversion, case manipulation, and related helpers.
 */
namespace StringUtil {

/**
 * @brief Converts a UTF-8 encoded narrow string to a UTF-16 wide string.
 *
 * @param str Input UTF-8 string.
 * @return Equivalent UTF-16 wide string.
 */
auto utf8toUTF16(const std::string& str) -> std::wstring;

/**
 * @brief Converts a UTF-16 wide string to a UTF-8 encoded narrow string.
 *
 * @param str Input UTF-16 wide string.
 * @return Equivalent UTF-8 narrow string.
 */
auto utf16toUTF8(const std::wstring& str) -> std::string;

/**
 * @brief Converts a Windows-1252 encoded narrow string to a UTF-16 wide string.
 *
 * @param str Input Windows-1252 string.
 * @return Equivalent UTF-16 wide string.
 */
auto windows1252toUTF16(const std::string& str) -> std::wstring;

/**
 * @brief Converts a UTF-16 wide string to a Windows-1252 encoded narrow string.
 *
 * @param str Input UTF-16 wide string.
 * @return Equivalent Windows-1252 narrow string.
 */
auto utf16toWindows1252(const std::wstring& str) -> std::string;

/**
 * @brief Converts an ASCII encoded narrow string to a UTF-16 wide string.
 *
 * @param str Input ASCII string.
 * @return Equivalent UTF-16 wide string.
 */
auto asciitoUTF16(const std::string& str) -> std::wstring;

/**
 * @brief Converts a UTF-16 wide string to an ASCII encoded narrow string.
 *
 * @param str Input UTF-16 wide string.
 * @return Equivalent ASCII narrow string.
 */
auto utf16toASCII(const std::wstring& str) -> std::string;

/**
 * @brief Converts a vector of UTF-8 strings to a vector of UTF-16 wide strings.
 *
 * @param vec Input vector of UTF-8 strings.
 * @return Vector of equivalent UTF-16 wide strings.
 */
auto utf8VectorToUTF16(const std::vector<std::string>& vec) -> std::vector<std::wstring>;

/**
 * @brief Converts a vector of UTF-16 wide strings to a vector of UTF-8 strings.
 *
 * @param vec Input vector of UTF-16 wide strings.
 * @return Vector of equivalent UTF-8 narrow strings.
 */
auto utf16VectorToUTF8(const std::vector<std::wstring>& vec) -> std::vector<std::string>;

/**
 * @brief Converts a vector of Windows-1252 strings to a vector of UTF-16 wide strings.
 *
 * @param vec Input vector of Windows-1252 strings.
 * @return Vector of equivalent UTF-16 wide strings.
 */
auto windows1252VectorToUTF16(const std::vector<std::string>& vec) -> std::vector<std::wstring>;

/**
 * @brief Converts a vector of UTF-16 wide strings to a vector of Windows-1252 strings.
 *
 * @param vec Input vector of UTF-16 wide strings.
 * @return Vector of equivalent Windows-1252 narrow strings.
 */
auto utf16VectorToWindows1252(const std::vector<std::wstring>& vec) -> std::vector<std::string>;

/**
 * @brief Converts a vector of ASCII strings to a vector of UTF-16 wide strings.
 *
 * @param vec Input vector of ASCII strings.
 * @return Vector of equivalent UTF-16 wide strings.
 */
auto asciiVectorToUTF16(const std::vector<std::string>& vec) -> std::vector<std::wstring>;

/**
 * @brief Converts a vector of UTF-16 wide strings to a vector of ASCII strings.
 *
 * @param vec Input vector of UTF-16 wide strings.
 * @return Vector of equivalent ASCII narrow strings.
 */
auto utf16VectorToASCII(const std::vector<std::wstring>& vec) -> std::vector<std::string>;

/**
 * @brief Checks whether a narrow string contains only ASCII characters (code points <= 127).
 *
 * @param str Input narrow string to check.
 * @return true if every character is within the ASCII range, false otherwise.
 */
auto containsOnlyAscii(const std::string& str) -> bool;

/**
 * @brief Checks whether a wide string contains only ASCII characters (code points <= 127).
 *
 * @param str Input wide string to check.
 * @return true if every character is within the ASCII range, false otherwise.
 */
auto containsOnlyAscii(const std::wstring& str) -> bool;

/**
 * @brief Converts a wide string to lower-case using the classic C locale (ASCII-safe).
 *
 * @param str Input wide string.
 * @return Lower-case copy of the input string.
 */
auto toLowerASCII(const std::wstring& str) -> std::wstring;

/**
 * @brief Checks whether a string value exists inside a JSON array.
 *
 * @param json A nlohmann::json value expected to be an array.
 * @param str The string to search for.
 * @return true if the string is found as an element of the JSON array, false otherwise.
 */
auto checkIfStringInJSONArray(const nlohmann::json& json,
                              const std::string& str) -> bool;

/**
 * @brief Returns a lower-case copy of a narrow string using a fast ASCII-only algorithm.
 *
 * @param str Input narrow string.
 * @return Lower-case copy (only A–Z are affected; non-ASCII characters are unchanged).
 */
auto toLowerASCIIFast(const std::string& str) -> std::string;

/**
 * @brief Returns a lower-case copy of a wide string using a fast ASCII-only algorithm.
 *
 * @param str Input wide string.
 * @return Lower-case copy (only A–Z are affected; non-ASCII characters are unchanged).
 */
auto toLowerASCIIFast(const std::wstring& str) -> std::wstring;

/**
 * @brief Converts a narrow string to lower-case in-place using a fast ASCII-only algorithm.
 *
 * @param str String to convert in-place (only A–Z are affected).
 */
auto toLowerASCIIFastInPlace(std::string& str) -> void;

/**
 * @brief Converts a wide string to lower-case in-place using a fast ASCII-only algorithm.
 *
 * @param str Wide string to convert in-place (only A–Z are affected).
 */
auto toLowerASCIIFastInPlace(std::wstring& str) -> void;

/**
 * @brief Case-insensitive equality comparison for ASCII strings using a fast lower-case conversion.
 *
 * @tparam StringType1 Type of the first string (std::string or std::wstring).
 * @tparam StringType2 Type of the second string (std::string or std::wstring).
 * @param str1 First string to compare.
 * @param str2 Second string to compare.
 * @return true if both strings are equal when compared in lower-case ASCII, false otherwise.
 */
template <typename StringType1,
          typename StringType2>
auto asciiFastIEquals(const StringType1& str1,
                      const StringType2& str2) -> bool
{
    return boost::equals(toLowerASCIIFast(str1), toLowerASCIIFast(str2));
}

} // namespace StringUtil
