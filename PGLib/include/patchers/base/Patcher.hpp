#pragma once

#include "ParallaxGenD3D.hpp"
#include "ParallaxGenDirectory.hpp"

#include <string>

/**
 * @class Patcher
 * @brief Base class for all patchers
 */
class Patcher {
private:
    // Instance vars
    std::string m_patcherName; /** Name of the patcher (used in log and UI elements) */
    bool m_triggerSave; /** If true, this patcher changing will trigger a wider save of the object */

    // Static Tools
    static ParallaxGenDirectory* s_pgd; /** All patchers have access to PGD */
    static ParallaxGenD3D* s_pgd3d; /** All patchers have access to PGD3D */

    // Each patcher needs to also implement these static methods:
    // static auto getFactory()

protected:
    /**
     * @brief Get the PGD object (used only within child patchers)
     *
     * @return ParallaxGenDirectory* PGD pointer
     */
    [[nodiscard]] static auto getPGD() -> ParallaxGenDirectory*;

    /**
     * @brief Get the PGD3D object (used only within child patchers)
     *
     * @return ParallaxGenD3D* PGD3D pointer
     */
    [[nodiscard]] static auto getPGD3D() -> ParallaxGenD3D*;

public:
    /**
     * @brief Load the statics for all patchers
     *
     * @param pgd initialized PGD object
     * @param pgd3d initialized PGD3D object
     */
    static void loadStatics(ParallaxGenDirectory& pgd, ParallaxGenD3D& pgd3d);

    /**
     * @brief Construct a new Patcher object
     *
     * @param nifPath Path to NIF being patched
     * @param nif NIF object
     * @param patcherName Name of patcher
     */
    Patcher(std::string patcherName, const bool& triggerSave = true);

    /**
     * @brief Get the Patcher Name object
     *
     * @return std::string Patcher name
     */
    [[nodiscard]] auto getPatcherName() const -> std::string;

    /**
     * @brief Get the Trigger Save object
     *
     * @return bool If true, this patcher changing will trigger a wider save of the object
     */
    [[nodiscard]] auto triggerSave() const -> bool;
};
