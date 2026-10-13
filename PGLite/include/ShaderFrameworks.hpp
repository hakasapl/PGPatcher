#pragma once

#include "PGParams.hpp"

namespace PGLite {

/**
 * @brief The shader frameworks that render what PGLite patches (DESIGN.md section 4.9): Community Shaders, found
 * through its SKSE plugin module, and ENB, found through the ENBGetSDKVersion export of the loaded modules, the way
 * TerrainHelper finds it. Without either PGLite disables itself; with one of them, the patchers only the other one
 * renders stay off (TruePBR and the hair flow map need Community Shaders, Fix Mesh Lighting needs ENB).
 */
class ShaderFrameworks {
public:
    struct Detected {
        bool hasCommunityShaders = false;
        bool hasENB = false;

        [[nodiscard]] bool hasAny() const { return hasCommunityShaders || hasENB; }
    };

    /// @brief Looks at the loaded modules; meaningful once the SKSE plugins are loaded, so at data load or later.
    [[nodiscard]] static Detected detect();

    /// @brief Turns off the patchers the detected frameworks cannot render, with a warning for each.
    static void limitPatchers(PGParams& params,
                              const Detected& detected);

    /**
     * @brief The parameters of a first start, when there is no settings.json yet: the offline defaults with parallax,
     * complex material and the pre-patched material reset on, TruePBR on with Community Shaders and Fix Mesh
     * Lighting on with ENB.
     */
    [[nodiscard]] static PGParams firstStartParams(const Detected& detected);
};

}
