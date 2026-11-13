#include "pgutil/PGShapeChanges.hpp"

#include "pgutil/PGMaterialState.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/StringUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <utility>

namespace {

/// @brief A texture path relative to the textures folder, as the slot attributes of the PBR configs take them.
std::wstring relativeTexturePath(const std::wstring& path)
{
    const std::wstring texturesPrefix = L"textures\\";
    const bool hasPrefix = path.size() > texturesPrefix.size()
        && StringUtil::asciiFastIEquals(path.substr(0, texturesPrefix.size()), texturesPrefix);
    return hasPrefix ? path.substr(texturesPrefix.size()) : path;
}

} // namespace

PGShapeChanges::State PGShapeChanges::stateOf(const nifly::NifFile& nif,
                                              nifly::NiShape& nifShape,
                                              const PGTypes::TextureSet& slots)
{
    State state;
    if (const auto* const shader = dynamic_cast<const nifly::BSLightingShaderProperty*>(nif.GetShader(&nifShape)))
        state.shader = *shader;

    state.slots = slots;
    return state;
}

nlohmann::json PGShapeChanges::changes(const State& base,
                                       const State& patched)
{
    auto out = nlohmann::json::object();

    if (base.shader && patched.shader) {
        auto shader = PGMaterialState::shaderChanges(*base.shader, *patched.shader);
        if (!shader.empty())
            out[nifly::BSLightingShaderProperty::BlockName] = std::move(shader);
    }

    // The slots by their index in the texture set; a path with and without the textures folder is the same texture.
    auto textures = nlohmann::json::object();
    for (std::size_t slot = 0; slot < numTextureSlots; slot++) {
        const auto basePath = relativeTexturePath(base.slots.at(slot));
        const auto patchedPath = relativeTexturePath(patched.slots.at(slot));
        if (!StringUtil::asciiFastIEquals(basePath, patchedPath))
            textures[std::to_string(slot)] = StringUtil::utf16toUTF8(patchedPath);
    }
    if (!textures.empty())
        out[nifly::BSShaderTextureSet::BlockName] = std::move(textures);

    return out;
}
