#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace EnumStringHelper {
template <typename Enum> struct EnumStringEntry {
    Enum value;
    std::string_view name;
};

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
