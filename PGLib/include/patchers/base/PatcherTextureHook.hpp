#pragma once

#include "PGRunObserver.hpp"
#include "patchers/base/PatcherTexture.hpp"
#include "pgutil/PGEnums.hpp"

#include <DirectXTex.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>

/**
 * @class PatcherTextureHook
 * @brief Base class for texture hooks: patchers that mesh patchers register a texture for, and that generate a new
 * texture from it during the texture phase
 */
class PatcherTextureHook : public PatcherTexture {
protected:
    static inline std::mutex s_generatedFileTrackerMutex;

    /**
     * @brief A CRC32 of what decides the output of a hook besides its input texture: the shader bytecode and the
     * values of the generation (formats, parameters), so a cache of generated textures can tell a build that makes
     * the same bytes from one that does not
     *
     * @param bytecode Compiled bytecode of the hook's shader
     * @param values The other inputs of the generation, as 32 bit words
     * @return The signature
     */
    static std::uint32_t signatureOf(std::span<const unsigned char> bytecode,
                                     std::span<const std::uint32_t> values);

public:
    // Type definitions.
    using PatcherGlobalFactory
        = std::function<std::unique_ptr<PatcherTextureHook>(std::filesystem::path, DirectX::ScratchImage*)>;
    using PatcherGlobalObject = std::unique_ptr<PatcherTextureHook>;

    // Constructors.
    PatcherTextureHook(std::filesystem::path texPath,
                       DirectX::ScratchImage* tex,
                       std::string patcherName);
    virtual ~PatcherTextureHook() = default;
    PatcherTextureHook(const PatcherTextureHook& other) = delete;
    PatcherTextureHook& operator=(const PatcherTextureHook& other) = delete;
    PatcherTextureHook(PatcherTextureHook&& other) noexcept = default;
    PatcherTextureHook& operator=(PatcherTextureHook&& other) noexcept = default;

    /**
     * @brief Generate the output texture from the hooked texture and write it to the output folder
     *
     * @return true Output was written
     * @return false Generation or writing failed
     */
    virtual bool applyPatch() = 0;

    /**
     * @brief Generate the output texture in memory: the compute shader, then the block compression the output file
     * receives. applyPatch() writes this; the runtime patcher caches it instead
     *
     * @param[out] compressed The generated texture
     * @return true The texture was generated
     * @return false The shader or the compression failed
     * @throws std::runtime_error if the patcher has no image
     */
    virtual bool generate(DirectX::ScratchImage& compressed) = 0;

    //
    // The hooks by kind (PGRunObserver::HookKind), for the code that tracks or caches their outputs.
    //

    /// @brief The path of the texture a hook generates from a source texture, see the hooks' outputFilename().
    static std::filesystem::path outputPathOf(PGRunObserver::HookKind kind,
                                              const std::filesystem::path& texPath);

    /// @brief The texture type of what a hook generates.
    static PGEnums::TextureType outputTypeOf(PGRunObserver::HookKind kind);

    /// @brief The generator signature of a hook, see the hooks' generatorSignature().
    static std::uint32_t generatorSignatureOf(PGRunObserver::HookKind kind);

    /// @brief The patcher name of a hook, as its log lines carry it.
    static const char* hookNameOf(PGRunObserver::HookKind kind);
};
