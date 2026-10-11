#pragma once

#include "pgutil/PGTypes.hpp"

#include "ExtraData.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Nodes.hpp"
#include "Particles.hpp"
#include "Shaders.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace TestUtil {

/**
 * @brief Builds small Skyrim SE meshes for tests: a scene root with the shapes, nodes and properties a test asks for.
 *
 * Every shape is a unit quad. Blocks are added in call order and saved without nifly's block sorting, so block indices
 * and tree order are exactly what the test built.
 */
class NIFBuilder {
private:
    nifly::NifFile m_nif;

    nifly::BSTriShape* addTriShape(const std::string& name,
                                   nifly::NiNode* parent);

public:
    /// @brief Creates an empty SSE mesh with a "Scene Root" node.
    NIFBuilder();

    /// @brief The mesh being built.
    [[nodiscard]] nifly::NifFile& nif();

    /// @brief The scene root.
    [[nodiscard]] nifly::NiNode* root();

    /**
     * @brief Adds a BSTriShape with a BSLightingShaderProperty and a texture set.
     *
     * @param name Shape name
     * @param textures Texture paths by slot (ASCII), empty slots stay empty
     * @param shaderType Shader type of the lighting shader
     * @param parent Node to attach the shape to, the root by default
     */
    nifly::BSTriShape* addShape(const std::string& name,
                                const PGTypes::TextureSet& textures = PGTypes::TextureSet(),
                                nifly::BSLightingShaderPropertyShaderType shaderType = nifly::BSLSP_DEFAULT,
                                nifly::NiNode* parent = nullptr);

    /// @brief Adds an old style NiTriShape with a BSLightingShaderProperty and a texture set.
    nifly::NiTriShape* addNiTriShape(const std::string& name,
                                     const PGTypes::TextureSet& textures = PGTypes::TextureSet(),
                                     nifly::NiNode* parent = nullptr);

    /// @brief Adds a BSTriShape with a BSEffectShaderProperty (no texture set).
    nifly::BSTriShape* addEffectShape(const std::string& name,
                                      const std::string& sourceTexture = "",
                                      nifly::NiNode* parent = nullptr);

    /// @brief Adds a BSTriShape that has no shader property at all.
    nifly::BSTriShape* addShaderlessShape(const std::string& name,
                                          nifly::NiNode* parent = nullptr);

    /// @brief Adds a particle system, which counts towards the 3D indices plugins use.
    nifly::NiParticleSystem* addParticleSystem(const std::string& name,
                                               nifly::NiNode* parent = nullptr);

    /// @brief Adds a billboard node, the kind of node that carries particle lights.
    nifly::NiBillboardNode* addBillboardNode(const std::string& name,
                                             nifly::NiNode* parent = nullptr);

    /// @brief Adds a child node.
    nifly::NiNode* addNode(const std::string& name,
                           nifly::NiNode* parent = nullptr);

    /// @brief Attaches an alpha property to a shape.
    nifly::NiAlphaProperty* addAlphaProperty(nifly::NiShape* shape,
                                             uint16_t flags = 4844,
                                             uint8_t threshold = 128);

    /// @brief Attaches a NiStringExtraData block to a shape, node or shader.
    nifly::NiStringExtraData* addStringExtraData(nifly::NiObjectNET* target,
                                                 const std::string& name,
                                                 const std::string& value);

    /// @brief Attaches a NiBooleanExtraData block to a shape, node or shader.
    nifly::NiBooleanExtraData* addBooleanExtraData(nifly::NiObjectNET* target,
                                                   const std::string& name,
                                                   bool value);

    /// @brief Sets every texture slot of a shape.
    void setTextures(nifly::NiShape* shape,
                     const PGTypes::TextureSet& textures);

    /// @brief The lighting shader of a shape, nullptr if it has none.
    [[nodiscard]] nifly::BSLightingShaderProperty* lightingShader(nifly::NiShape* shape);

    /// @brief The texture set of a shape, nullptr if it has none.
    [[nodiscard]] nifly::BSShaderTextureSet* textureSet(nifly::NiShape* shape);

    /// @brief Serializes the mesh as PGPatcher saves meshes (no optimization, no block sorting).
    [[nodiscard]] std::vector<std::byte> bytes();

    /// @brief Writes the mesh to a file, creating its folders.
    void save(const std::filesystem::path& path);

    /// @brief Serializes a mesh as PGPatcher saves meshes.
    static std::vector<std::byte> bytesOf(nifly::NifFile& nif);

    /// @brief Writes a mesh to a file, creating its folders.
    static void saveTo(nifly::NifFile& nif,
                       const std::filesystem::path& path);
};

}
