#include "PCH.h"

#include "Uses.hpp"

#include "PGPlugin.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PGLite {

namespace {

/// @brief A record type the offline program reads, with the type it reports for it.
struct RecordType {
    RE::FormType formType;
    PGPlugin::ModelRecordType recType;
};

/// @brief The 36 record types with models. ADDN has no xEdit type offline, so its uses carry Unknown, which the record
/// type filter always excludes.
constexpr std::array<RecordType, 36> recordTypes {
    {
        { .formType = RE::FormType::Activator, .recType = PGPlugin::ModelRecordType::Activator },
        { .formType = RE::FormType::AddonNode, .recType = PGPlugin::ModelRecordType::Unknown },
        { .formType = RE::FormType::Ammo, .recType = PGPlugin::ModelRecordType::Ammunition },
        { .formType = RE::FormType::AnimatedObject, .recType = PGPlugin::ModelRecordType::AnimatedObject },
        { .formType = RE::FormType::Armor, .recType = PGPlugin::ModelRecordType::Armor },
        { .formType = RE::FormType::Armature, .recType = PGPlugin::ModelRecordType::ArmorAddon },
        { .formType = RE::FormType::ArtObject, .recType = PGPlugin::ModelRecordType::ArtObject },
        { .formType = RE::FormType::BodyPartData, .recType = PGPlugin::ModelRecordType::BodyPartData },
        { .formType = RE::FormType::Book, .recType = PGPlugin::ModelRecordType::Book },
        { .formType = RE::FormType::CameraShot, .recType = PGPlugin::ModelRecordType::CameraShot },
        { .formType = RE::FormType::Climate, .recType = PGPlugin::ModelRecordType::Climate },
        { .formType = RE::FormType::Container, .recType = PGPlugin::ModelRecordType::Container },
        { .formType = RE::FormType::Door, .recType = PGPlugin::ModelRecordType::Door },
        { .formType = RE::FormType::Explosion, .recType = PGPlugin::ModelRecordType::Explosion },
        { .formType = RE::FormType::Flora, .recType = PGPlugin::ModelRecordType::Flora },
        { .formType = RE::FormType::Furniture, .recType = PGPlugin::ModelRecordType::Furniture },
        { .formType = RE::FormType::Grass, .recType = PGPlugin::ModelRecordType::Grass },
        { .formType = RE::FormType::Hazard, .recType = PGPlugin::ModelRecordType::Hazard },
        { .formType = RE::FormType::HeadPart, .recType = PGPlugin::ModelRecordType::HeadPart },
        { .formType = RE::FormType::IdleMarker, .recType = PGPlugin::ModelRecordType::IdleMarker },
        { .formType = RE::FormType::Impact, .recType = PGPlugin::ModelRecordType::Impact },
        { .formType = RE::FormType::AlchemyItem, .recType = PGPlugin::ModelRecordType::Ingestible },
        { .formType = RE::FormType::Ingredient, .recType = PGPlugin::ModelRecordType::Ingredient },
        { .formType = RE::FormType::KeyMaster, .recType = PGPlugin::ModelRecordType::Key },
        { .formType = RE::FormType::LeveledNPC, .recType = PGPlugin::ModelRecordType::LeveledNPC },
        { .formType = RE::FormType::Light, .recType = PGPlugin::ModelRecordType::Light },
        { .formType = RE::FormType::MaterialObject, .recType = PGPlugin::ModelRecordType::MaterialObject },
        { .formType = RE::FormType::Misc, .recType = PGPlugin::ModelRecordType::MiscItem },
        { .formType = RE::FormType::MovableStatic, .recType = PGPlugin::ModelRecordType::MoveableStatic },
        { .formType = RE::FormType::Projectile, .recType = PGPlugin::ModelRecordType::Projectile },
        { .formType = RE::FormType::Scroll, .recType = PGPlugin::ModelRecordType::Scroll },
        { .formType = RE::FormType::SoulGem, .recType = PGPlugin::ModelRecordType::SoulGem },
        { .formType = RE::FormType::Static, .recType = PGPlugin::ModelRecordType::StaticObject },
        { .formType = RE::FormType::TalkingActivator, .recType = PGPlugin::ModelRecordType::TalkingActivator },
        { .formType = RE::FormType::Tree, .recType = PGPlugin::ModelRecordType::Tree },
        { .formType = RE::FormType::Weapon, .recType = PGPlugin::ModelRecordType::Weapon },
    },
};

/// @brief The record index (TX00 to TX07) of every NIF texture slot: the engine stores a texture set in record order,
/// the NIF and the offline program in slot order.
constexpr std::array<std::size_t, 8> recordIndexOfSlot { 0, 1, 3, 4, 5, 2, 6, 7 };

/// @brief The "Enabled" bit of the weight slider bytes of an ARMA record (DNAM).
constexpr std::uint8_t weightSliderEnabledFlag = 0x02;

/// @brief The record header flag the offline program treats as "ignored".
constexpr std::uint32_t ignoredRecordFlag = 1U << 24U;

/// @brief The single pass bit of a material object (MATO DATA), as Mutagen reads it.
constexpr std::int32_t singlePassFlag = 1;

/// @brief The night sky model TESClimate::InitializeData sets on every climate; a plugin value equal to it is not a use.
constexpr std::wstring_view defaultNightSky = L"meshes\\sky\\stars.nif";

constexpr std::wstring_view meshesPrefix = L"meshes\\";
constexpr std::wstring_view texturesPrefix = L"textures\\";

void addModel(RE::TESModel& model,
              std::string subModel,
              std::vector<ModelElement>& elements)
{
    elements.push_back({ .model = &model, .swap = nullptr, .subModel = std::move(subModel) });
}

void addSwap(RE::TESModelTextureSwap& swap,
             std::string subModel,
             std::vector<ModelElement>& elements)
{
    elements.push_back({ .model = &swap, .swap = &swap, .subModel = std::move(subModel) });
}

void addDestructibleStages(RE::BGSDestructibleObjectForm& destructible,
                           std::vector<ModelElement>& elements)
{
    const auto* data = destructible.data;
    if (!data || !data->stages)
        return;

    for (std::int8_t stage = 0; stage < data->numStages; stage++) {
        auto* const stageData = data->stages[stage];
        if (!stageData || !stageData->replacementModel)
            continue;

        addSwap(*stageData->replacementModel, std::format("DMDL{}", stage), elements);
    }
}

void addActivator(RE::TESObjectACTI& activator,
                  std::vector<ModelElement>& elements)
{
    addSwap(activator, "MODL", elements);
    addDestructibleStages(activator, elements);
}

template<typename T>
void addSwapWithStages(RE::TESForm& form,
                       std::vector<ModelElement>& elements)
{
    auto& record = static_cast<T&>(form);
    addSwap(record, "MODL", elements);
    addDestructibleStages(record, elements);
}

template<typename T>
void addSwapOnly(RE::TESForm& form,
                 std::vector<ModelElement>& elements)
{
    addSwap(static_cast<T&>(form), "MODL", elements);
}

template<typename T>
void addModelOnly(RE::TESForm& form,
                  std::vector<ModelElement>& elements)
{
    addModel(static_cast<T&>(form), "MODL", elements);
}

/// @brief Whether an armor addon model is a weighted (_0/_1) model: the weight slider of its sex is enabled.
bool isWeightedArmorAddonModel(const RE::TESObjectARMA& armorAddon,
                               const std::string& subModel)
{
    const bool isMale = subModel == "MALE" || subModel == "1STMALE";
    const bool isFemale = subModel == "FEMALE" || subModel == "1STFEMALE";
    if (isMale)
        return armorAddon.data.modelRange[RE::SEXES::kMale] & weightSliderEnabledFlag;
    if (isFemale)
        return armorAddon.data.modelRange[RE::SEXES::kFemale] & weightSliderEnabledFlag;

    return false;
}

/// @brief Whether a static's material object is single pass.
bool isSinglePassStatic(const RE::TESObjectSTAT& stat)
{
    const auto* const material = stat.data.materialObj;
    return material && (material->directionalData.singlePass & singlePassFlag);
}

/// @brief The order the offline program patches the uses of a mesh in, see PGPlugin::modelUses().
bool compareUses(const RecordUse& a,
                 const RecordUse& b)
{
    const bool aHasAltTex = !a.attributes.alternateTextures.empty();
    const bool bHasAltTex = !b.attributes.alternateTextures.empty();
    if (aHasAltTex != bHasAltTex)
        return !aHasAltTex; // no alternate textures first
    if (a.attributes.isWeighted != b.attributes.isWeighted)
        return a.attributes.isWeighted; // weighted first
    if (a.formKey.modKey != b.formKey.modKey)
        return a.formKey.modKey < b.formKey.modKey;
    if (a.formKey.formID != b.formKey.formID)
        return a.formKey.formID < b.formKey.formID;
    return a.formKey.subMODL < b.formKey.subMODL;
}

}

void Uses::build()
{
    m_byPath.clear();

    auto* const dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler)
        throw std::runtime_error("The data handler is not available");

    std::size_t numUses = 0;
    for (const auto& [formType, recType] : recordTypes) {
        for (auto* const form : dataHandler->GetFormArray(formType)) {
            if (!form)
                continue;

            // A form without a file has no form key offline.
            const auto modKey = originFileName(*form);
            if (modKey.empty())
                continue;

            const auto elements = modelElements(*form);
            for (const auto& element : elements) {
                const auto path = normalizedModelPath(element.model->GetModel());
                if (path.empty())
                    continue;

                if (formType == RE::FormType::Climate && path.wstring() == defaultNightSky) {
                    // The engine's own default, which the plugin files never carry.
                    continue;
                }

                RecordUse use;
                use.formKey = { .modKey = modKey, .formID = form->GetLocalFormID(), .subMODL = element.subModel };
                use.form = form;
                use.element = element;
                use.attributes.recType = recType;
                use.attributes.isIgnored = form->GetFormFlags() & ignoredRecordFlag;
                if (formType == RE::FormType::Static)
                    use.attributes.isSinglepassMATO = isSinglePassStatic(static_cast<const RE::TESObjectSTAT&>(*form));
                if (formType == RE::FormType::Armature) {
                    use.attributes.isWeighted
                        = isWeightedArmorAddonModel(static_cast<const RE::TESObjectARMA&>(*form), element.subModel);
                }

                if (element.swap && element.swap->alternateTextures) {
                    for (std::uint32_t i = 0; i < element.swap->numAlternateTextures; i++) {
                        const auto& alternateTexture = element.swap->alternateTextures[i];
                        use.attributes.alternateTextures[alternateTexture.index3D] = textureSet(alternateTexture.textureSet);
                    }
                }

                if (use.attributes.isWeighted) {
                    // A weighted model is used under both files of its pair.
                    const auto counterpart = weightCounterpart(path);
                    if (counterpart.empty()) {
                        Logger::warn(L"Weight slider enabled but the model does not end in _0.nif or _1.nif: {} ({} / "
                                     L"{:06X})",
                                     path.wstring(),
                                     modKey,
                                     use.formKey.formID);
                        continue;
                    }

                    m_byPath[counterpart].push_back(use);
                    numUses++;
                }

                m_byPath[path].push_back(use);
                numUses++;
            }
        }
    }

    for (auto& [path, uses] : m_byPath)
        std::ranges::sort(uses, compareUses);

    Logger::info("Found {} record model uses of {} meshes", numUses, m_byPath.size());
}

const std::unordered_map<std::filesystem::path, std::vector<RecordUse>>& Uses::byPath() const { return m_byPath; }

Uses::UseList Uses::usesOf(const std::filesystem::path& path) const
{
    UseList uses;

    const auto it = m_byPath.find(path);
    if (it == m_byPath.end())
        return uses;

    uses.reserve(it->second.size());
    for (const auto& use : it->second)
        uses.emplace_back(use.formKey, use.attributes);

    return uses;
}

std::filesystem::path Uses::normalizedModelPath(const char* model)
{
    if (!model || !*model)
        return { };

    auto path = StringUtil::windows1252toUTF16(model);
    std::ranges::replace(path, L'/', L'\\');
    StringUtil::toLowerASCIIFastInPlace(path);

    // Leading separators and the current folder are not part of the path the engine hashes.
    while (path.starts_with(L"\\") || path.starts_with(L".\\"))
        path.erase(0, path.starts_with(L"\\") ? 1 : 2);

    if (path.empty())
        return { };

    if (!path.starts_with(meshesPrefix))
        path = std::wstring(meshesPrefix) + path;

    return path;
}

std::wstring Uses::originFileName(const RE::TESForm& form)
{
    const auto* const file = form.GetFile(0);
    if (!file)
        return { };

    return StringUtil::windows1252toUTF16(std::string(file->GetFilename()));
}

PGTypes::TextureSet Uses::textureSet(const RE::BGSTextureSet* set)
{
    // The ninth slot of a NIF never reaches a plugin and stays empty.
    PGTypes::TextureSet textures;
    if (!set)
        return textures;

    for (std::size_t slot = 0; slot < recordIndexOfSlot.size(); slot++) {
        const char* const name = set->textures[recordIndexOfSlot.at(slot)].textureName.c_str();
        if (!name || !*name)
            continue;

        auto texture = StringUtil::windows1252toUTF16(name);
        StringUtil::toLowerASCIIFastInPlace(texture);
        if (!texture.starts_with(texturesPrefix))
            texture = std::wstring(texturesPrefix) + texture;

        textures.at(slot) = texture;
    }

    return textures;
}

std::vector<ModelElement> Uses::modelElements(RE::TESForm& form)
{
    std::vector<ModelElement> elements;

    switch (form.GetFormType()) {
    case RE::FormType::Activator:
        addActivator(static_cast<RE::TESObjectACTI&>(form), elements);
        break;
    case RE::FormType::Flora:
        addActivator(static_cast<RE::TESFlora&>(form), elements);
        break;
    case RE::FormType::Furniture:
        addActivator(static_cast<RE::TESFurniture&>(form), elements);
        break;
    case RE::FormType::TalkingActivator:
        addActivator(static_cast<RE::BGSTalkingActivator&>(form), elements);
        break;
    case RE::FormType::AddonNode:
        addSwapOnly<RE::BGSAddonNode>(form, elements);
        break;
    case RE::FormType::AlchemyItem:
        addSwapWithStages<RE::AlchemyItem>(form, elements);
        break;
    case RE::FormType::Ammo:
        addSwapWithStages<RE::TESAmmo>(form, elements);
        break;
    case RE::FormType::AnimatedObject:
        addSwapOnly<RE::TESObjectANIO>(form, elements);
        break;
    case RE::FormType::Armor: {
        auto& armor = static_cast<RE::TESObjectARMO&>(form);
        addSwap(armor.worldModels[RE::SEXES::kMale], "MALE", elements);
        addSwap(armor.worldModels[RE::SEXES::kFemale], "FEMALE", elements);
        addDestructibleStages(armor, elements);
        break;
    }
    case RE::FormType::Armature: {
        auto& armorAddon = static_cast<RE::TESObjectARMA&>(form);
        addSwap(armorAddon.bipedModels[RE::SEXES::kMale], "MALE", elements);
        addSwap(armorAddon.bipedModels[RE::SEXES::kFemale], "FEMALE", elements);
        addSwap(armorAddon.bipedModel1stPersons[RE::SEXES::kMale], "1STMALE", elements);
        addSwap(armorAddon.bipedModel1stPersons[RE::SEXES::kFemale], "1STFEMALE", elements);
        break;
    }
    case RE::FormType::ArtObject:
        addSwapOnly<RE::BGSArtObject>(form, elements);
        break;
    case RE::FormType::BodyPartData:
        addModelOnly<RE::BGSBodyPartData>(form, elements);
        break;
    case RE::FormType::Book:
        addSwapWithStages<RE::TESObjectBOOK>(form, elements);
        break;
    case RE::FormType::CameraShot:
        addModelOnly<RE::BGSCameraShot>(form, elements);
        break;
    case RE::FormType::Climate:
        addModel(static_cast<RE::TESClimate&>(form).nightSky, "MODL", elements);
        break;
    case RE::FormType::Container:
        addSwapWithStages<RE::TESObjectCONT>(form, elements);
        break;
    case RE::FormType::Door:
        addSwapWithStages<RE::TESObjectDOOR>(form, elements);
        break;
    case RE::FormType::Explosion:
        addModelOnly<RE::BGSExplosion>(form, elements);
        break;
    case RE::FormType::Grass:
        addModelOnly<RE::TESGrass>(form, elements);
        break;
    case RE::FormType::Hazard:
        addModelOnly<RE::BGSHazard>(form, elements);
        break;
    case RE::FormType::HeadPart:
        addSwapOnly<RE::BGSHeadPart>(form, elements);
        break;
    case RE::FormType::IdleMarker:
        addModelOnly<RE::BGSIdleMarker>(form, elements);
        break;
    case RE::FormType::Impact:
        addModelOnly<RE::BGSImpactData>(form, elements);
        break;
    case RE::FormType::Ingredient:
        addSwapWithStages<RE::IngredientItem>(form, elements);
        break;
    case RE::FormType::KeyMaster:
        addSwapWithStages<RE::TESKey>(form, elements);
        break;
    case RE::FormType::LeveledNPC:
        addSwapOnly<RE::TESLevCharacter>(form, elements);
        break;
    case RE::FormType::Light:
        addSwapWithStages<RE::TESObjectLIGH>(form, elements);
        break;
    case RE::FormType::MaterialObject:
        addModelOnly<RE::BGSMaterialObject>(form, elements);
        break;
    case RE::FormType::Misc:
        addSwapWithStages<RE::TESObjectMISC>(form, elements);
        break;
    case RE::FormType::MovableStatic:
        addSwapWithStages<RE::BGSMovableStatic>(form, elements);
        break;
    case RE::FormType::Projectile: {
        auto& projectile = static_cast<RE::BGSProjectile&>(form);
        addModel(projectile, "MODL", elements);
        addDestructibleStages(projectile, elements);
        break;
    }
    case RE::FormType::Scroll:
        addSwapWithStages<RE::ScrollItem>(form, elements);
        break;
    case RE::FormType::SoulGem:
        addSwapWithStages<RE::TESSoulGem>(form, elements);
        break;
    case RE::FormType::Static:
        addSwapOnly<RE::TESObjectSTAT>(form, elements);
        break;
    case RE::FormType::Tree:
        addModelOnly<RE::TESObjectTREE>(form, elements);
        break;
    case RE::FormType::Weapon:
        addSwapWithStages<RE::TESObjectWEAP>(form, elements);
        break;
    default:
        break;
    }

    return elements;
}

PGPlugin::ModelRecordType Uses::recordType(RE::FormType type)
{
    for (const auto& [formType, recType] : recordTypes)
        if (formType == type)
            return recType;

    return PGPlugin::ModelRecordType::Unknown;
}

std::filesystem::path Uses::weightCounterpart(const std::filesystem::path& path)
{
    const auto str = path.wstring();
    static constexpr std::wstring_view suffix0 = L"_0.nif";
    static constexpr std::wstring_view suffix1 = L"_1.nif";
    if (str.ends_with(suffix0))
        return str.substr(0, str.size() - suffix0.size()) + std::wstring(suffix1);
    if (str.ends_with(suffix1))
        return str.substr(0, str.size() - suffix1.size()) + std::wstring(suffix0);

    return { };
}

}
