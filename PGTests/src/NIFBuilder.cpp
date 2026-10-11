#include "NIFBuilder.hpp"

#include "pgutil/PGTypes.hpp"
#include "util/StringUtil.hpp"

#include "BasicTypes.hpp"
#include "ExtraData.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Nodes.hpp"
#include "Object3d.hpp"
#include "Particles.hpp"
#include "Shaders.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace TestUtil {

namespace {
const std::vector<nifly::Vector3> s_quadVerts { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
const std::vector<nifly::Triangle> s_quadTris { { 0, 1, 2 }, { 0, 2, 3 } };
const std::vector<nifly::Vector2> s_quadUVs { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
const std::vector<nifly::Vector3> s_quadNormals { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
}

NIFBuilder::NIFBuilder() { m_nif.Create(nifly::NiVersion::getSSE()); }

nifly::NifFile& NIFBuilder::nif() { return m_nif; }

nifly::NiNode* NIFBuilder::root() { return m_nif.GetRootNode(); }

nifly::BSTriShape* NIFBuilder::addTriShape(const std::string& name,
                                           nifly::NiNode* parent)
{
    auto& header = m_nif.GetHeader();

    auto triShape = std::make_unique<nifly::BSTriShape>();
    triShape->Create(header.GetVersion(), &s_quadVerts, &s_quadTris, &s_quadUVs, &s_quadNormals);
    triShape->SetSkinned(false);
    triShape->name.get() = name;

    auto* result = triShape.get();
    const auto shapeID = header.AddBlock(std::move(triShape));
    (parent ? parent : root())->childRefs.AddBlockRef(shapeID);
    return result;
}

nifly::BSTriShape* NIFBuilder::addShape(const std::string& name,
                                        const PGTypes::TextureSet& textures,
                                        nifly::BSLightingShaderPropertyShaderType shaderType,
                                        nifly::NiNode* parent)
{
    auto& header = m_nif.GetHeader();

    auto textureSet = std::make_unique<nifly::BSShaderTextureSet>(header.GetVersion());
    auto shader = std::make_unique<nifly::BSLightingShaderProperty>(header.GetVersion());
    shader->TextureSetRef()->index = header.AddBlock(std::move(textureSet));
    shader->SetSkinned(false);
    shader->SetShaderType(shaderType);
    const auto shaderID = header.AddBlock(std::move(shader));

    auto* shape = addTriShape(name, parent);
    shape->ShaderPropertyRef()->index = shaderID;
    setTextures(shape, textures);
    return shape;
}

nifly::NiTriShape* NIFBuilder::addNiTriShape(const std::string& name,
                                             const PGTypes::TextureSet& textures,
                                             nifly::NiNode* parent)
{
    auto& header = m_nif.GetHeader();

    auto textureSet = std::make_unique<nifly::BSShaderTextureSet>(header.GetVersion());
    auto shader = std::make_unique<nifly::BSLightingShaderProperty>(header.GetVersion());
    shader->TextureSetRef()->index = header.AddBlock(std::move(textureSet));
    shader->SetSkinned(false);
    const auto shaderID = header.AddBlock(std::move(shader));

    auto triShape = std::make_unique<nifly::NiTriShape>();
    triShape->ShaderPropertyRef()->index = shaderID;
    triShape->name.get() = name;

    auto shapeData = std::make_unique<nifly::NiTriShapeData>();
    shapeData->Create(header.GetVersion(), &s_quadVerts, &s_quadTris, &s_quadUVs, &s_quadNormals);
    triShape->SetGeomData(shapeData.get());
    triShape->DataRef()->index = header.AddBlock(std::move(shapeData));
    triShape->SetSkinned(false);

    auto* result = triShape.get();
    const auto shapeID = header.AddBlock(std::move(triShape));
    (parent ? parent : root())->childRefs.AddBlockRef(shapeID);
    setTextures(result, textures);
    return result;
}

nifly::BSTriShape* NIFBuilder::addEffectShape(const std::string& name,
                                              const std::string& sourceTexture,
                                              nifly::NiNode* parent)
{
    auto& header = m_nif.GetHeader();

    auto shader = std::make_unique<nifly::BSEffectShaderProperty>();
    shader->sourceTexture.get() = sourceTexture;
    const auto shaderID = header.AddBlock(std::move(shader));

    auto* shape = addTriShape(name, parent);
    shape->ShaderPropertyRef()->index = shaderID;
    return shape;
}

nifly::BSTriShape* NIFBuilder::addShaderlessShape(const std::string& name,
                                                  nifly::NiNode* parent)
{
    return addTriShape(name, parent);
}

nifly::NiParticleSystem* NIFBuilder::addParticleSystem(const std::string& name,
                                                       nifly::NiNode* parent)
{
    auto particles = std::make_unique<nifly::NiParticleSystem>();
    particles->name.get() = name;
    auto* result = particles.get();
    const auto blockID = m_nif.GetHeader().AddBlock(std::move(particles));
    (parent ? parent : root())->childRefs.AddBlockRef(blockID);
    return result;
}

nifly::NiNode* NIFBuilder::addNode(const std::string& name,
                                   nifly::NiNode* parent)
{
    return m_nif.AddNode(name, nifly::MatTransform(), parent);
}

nifly::NiBillboardNode* NIFBuilder::addBillboardNode(const std::string& name,
                                                     nifly::NiNode* parent)
{
    auto node = std::make_unique<nifly::NiBillboardNode>();
    node->name.get() = name;
    auto* result = node.get();
    const auto blockID = m_nif.GetHeader().AddBlock(std::move(node));
    (parent ? parent : root())->childRefs.AddBlockRef(blockID);
    return result;
}

nifly::NiAlphaProperty* NIFBuilder::addAlphaProperty(nifly::NiShape* shape,
                                                     uint16_t flags,
                                                     uint8_t threshold)
{
    if (!shape)
        throw std::invalid_argument("shape is null");

    auto alpha = std::make_unique<nifly::NiAlphaProperty>();
    alpha->flags = flags;
    alpha->threshold = threshold;
    auto* result = alpha.get();
    shape->AlphaPropertyRef()->index = m_nif.GetHeader().AddBlock(std::move(alpha));
    return result;
}

nifly::NiStringExtraData* NIFBuilder::addStringExtraData(nifly::NiObjectNET* target,
                                                         const std::string& name,
                                                         const std::string& value)
{
    if (!target)
        throw std::invalid_argument("target is null");

    auto extraData = std::make_unique<nifly::NiStringExtraData>();
    extraData->name.get() = name;
    extraData->stringData.get() = value;
    auto* result = extraData.get();
    target->extraDataRefs.AddBlockRef(m_nif.GetHeader().AddBlock(std::move(extraData)));
    return result;
}

nifly::NiBooleanExtraData* NIFBuilder::addBooleanExtraData(nifly::NiObjectNET* target,
                                                           const std::string& name,
                                                           bool value)
{
    if (!target)
        throw std::invalid_argument("target is null");

    auto extraData = std::make_unique<nifly::NiBooleanExtraData>();
    extraData->name.get() = name;
    extraData->booleanData = value;
    auto* result = extraData.get();
    target->extraDataRefs.AddBlockRef(m_nif.GetHeader().AddBlock(std::move(extraData)));
    return result;
}

void NIFBuilder::setTextures(nifly::NiShape* shape,
                             const PGTypes::TextureSet& textures)
{
    for (uint32_t slot = 0; slot < numTextureSlots; slot++) {
        std::string texture = StringUtil::utf16toASCII(textures.at(slot));
        m_nif.SetTextureSlot(shape, texture, slot);
    }
}

nifly::BSLightingShaderProperty* NIFBuilder::lightingShader(nifly::NiShape* shape)
{
    if (!shape)
        return nullptr;

    return dynamic_cast<nifly::BSLightingShaderProperty*>(m_nif.GetShader(shape));
}

nifly::BSShaderTextureSet* NIFBuilder::textureSet(nifly::NiShape* shape)
{
    auto* shader = shape ? m_nif.GetShader(shape) : nullptr;
    if (!shader || !shader->HasTextureSet())
        return nullptr;

    return m_nif.GetHeader().GetBlock(shader->TextureSetRef());
}

std::vector<std::byte> NIFBuilder::bytes() { return bytesOf(m_nif); }

void NIFBuilder::save(const std::filesystem::path& path) { saveTo(m_nif, path); }

std::vector<std::byte> NIFBuilder::bytesOf(nifly::NifFile& nif)
{
    std::ostringstream stream(std::ios::binary);
    if (nif.Save(stream, { .optimize = false, .sortBlocks = false }))
        throw std::runtime_error("failed to serialize NIF");

    const auto text = stream.str();
    std::vector<std::byte> out(text.size());
    for (size_t i = 0; i < text.size(); i++)
        out[i] = static_cast<std::byte>(text[i]);
    return out;
}

void NIFBuilder::saveTo(nifly::NifFile& nif,
                        const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open() || nif.Save(file, { .optimize = false, .sortBlocks = false }))
        throw std::runtime_error("failed to write NIF");
}

}
