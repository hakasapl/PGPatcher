#pragma once

#include <unordered_set>
#include <vector>

namespace ContainerUtil {
// Template Functions
template <typename T>
auto isInVector(const std::vector<T>& vec,
                const T& test) -> bool
{
    return std::find(vec.begin(), vec.end(), test) != vec.end();
}

// concatenates two vectors without duplicates
template <typename T>
void concatenateVectorsWithoutDuplicates(std::vector<T>& vec1,
                                         const std::vector<T>& vec2)
{
    std::unordered_set<T> set(vec1.begin(), vec1.end());

    for (const auto& element : vec2) {
        if (set.find(element) == set.end()) {
            vec1.push_back(element);
            set.insert(element);
        }
    }
};

// adds an element to a vector if it is not already present
template <typename T>
void addUniqueElement(std::vector<T>& vec,
                      const T& element)
{
    if (std::find(vec.begin(), vec.end(), element) == vec.end()) {
        vec.push_back(element);
    }
}
}
