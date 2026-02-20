#pragma once

#include <unordered_set>
#include <vector>

/**
 * @brief Utility functions for common container operations such as search and deduplication.
 */
namespace ContainerUtil {

/**
 * @brief Checks whether a value exists in a vector.
 *
 * @tparam T Element type of the vector.
 * @param vec The vector to search.
 * @param test The value to look for.
 * @return true if the value is found in the vector, false otherwise.
 */
template <typename T>
auto isInVector(const std::vector<T>& vec,
                const T& test) -> bool
{
    return std::find(vec.begin(), vec.end(), test) != vec.end();
}

/**
 * @brief Appends elements from a second vector into a first vector, skipping any duplicates.
 *
 * @tparam T Element type of the vectors.
 * @param vec1 Destination vector that receives unique elements from vec2.
 * @param vec2 Source vector whose elements are merged into vec1.
 */
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

/**
 * @brief Inserts an element into a vector only if it is not already present.
 *
 * @tparam T Element type of the vector.
 * @param vec The vector to insert into.
 * @param element The element to add if not already present.
 */
template <typename T>
void addUniqueElement(std::vector<T>& vec,
                      const T& element)
{
    if (std::find(vec.begin(), vec.end(), element) == vec.end()) {
        vec.push_back(element);
    }
}

}
