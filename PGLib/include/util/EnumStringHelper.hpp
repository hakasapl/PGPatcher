#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Utilities for bidirectional conversion between enum values and their string representations.
 */
namespace EnumStringHelper {

/**
 * @brief Maps a single enum value to its string name.
 *
 * @tparam Enum The enum type.
 */
template <typename Enum> struct EnumStringEntry {
    Enum value;
    std::string_view name;
};

/**
 * @brief Looks up an enum value by its string name in a lookup table.
 *
 * @tparam Enum The enum type.
 * @tparam N Number of entries in the lookup table.
 * @param str The string name to look up.
 * @param table Array of EnumStringEntry mappings to search.
 * @param defaultValue Value returned when no match is found.
 * @return The matching enum value, or defaultValue if the string is not found.
 */
template <typename Enum,
          size_t N>
constexpr auto enumFromString(std::string_view str,
                              const std::array<EnumStringEntry<Enum>,
                                               N>& table,
                              Enum defaultValue) -> Enum
{
    auto it = std::ranges::find(table, str, &EnumStringEntry<Enum>::name);
    return (it != table.end()) ? it->value : defaultValue;
}

/**
 * @brief Looks up the string name for an enum value in a lookup table.
 *
 * @tparam Enum The enum type.
 * @tparam N Number of entries in the lookup table.
 * @param value The enum value to look up.
 * @param table Array of EnumStringEntry mappings to search.
 * @param defaultValue String returned when no match is found.
 * @return The string name corresponding to value, or defaultValue if not found.
 */
template <typename Enum,
          size_t N>
constexpr auto stringFromEnum(Enum value,
                              const std::array<EnumStringEntry<Enum>,
                                               N>& table,
                              std::string_view defaultValue) -> std::string_view
{
    auto it = std::ranges::find(table, value, &EnumStringEntry<Enum>::value);
    return (it != table.end()) ? it->name : defaultValue;
}

/**
 * @brief Collects all string names from a lookup table into a vector.
 *
 * @tparam Enum The enum type.
 * @tparam N Number of entries in the lookup table.
 * @param table Array of EnumStringEntry mappings.
 * @return Vector containing every name string from the table, in order.
 */
template <typename Enum,
          size_t N>
auto allEnumStrings(const std::array<EnumStringEntry<Enum>,
                                     N>& table) -> std::vector<std::string>
{
    std::vector<std::string> result;
    result.reserve(N);

    for (const auto& e : table) {
        result.emplace_back(e.name);
    }

    return result;
}

}
