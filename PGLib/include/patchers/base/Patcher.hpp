#pragma once

#include <string>

/**
 * @class Patcher
 * @brief Base class for all patchers
 */
class Patcher {
private:
    // Instance vars
    std::string m_patcherName; /** Name of the patcher (used in log and UI elements) */

    // Each patcher needs to also implement these static methods:
    // static auto getFactory()

public:
    /**
     * @brief Construct a new Patcher object
     *
     * @param nifPath Path to NIF being patched
     * @param nif NIF object
     * @param patcherName Name of patcher
     */
    Patcher(std::string patcherName);

    /**
     * @brief Get the Patcher Name object
     *
     * @return std::string Patcher name
     */
    [[nodiscard]] auto getPatcherName() const -> std::string;
};
