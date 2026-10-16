// The record type and language tables of PGPlugin and the path helper, which need no plugin library and are shared
// with the PGLite runtime; the Mutagen-backed functions live in PGPlugin.cpp.
#include "PGPlugin.hpp"

#include "util/EnumStringHelper.hpp"

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

auto PGPlugin::pluginLangFromString(const std::string& lang) -> PluginLang
{
    return EnumStringHelper::enumFromString(lang, pluginLangTable, PluginLang::English);
}

std::string PGPlugin::stringFromPluginLang(const PluginLang& lang)
{
    return std::string(EnumStringHelper::stringFromEnum(lang, pluginLangTable, "English"));
}

std::vector<std::string> PGPlugin::availablePluginLangStrs()
{
    return EnumStringHelper::allEnumStrings(pluginLangTable);
}

auto PGPlugin::recTypeFromString(const std::string& recTypeStr) -> ModelRecordType
{
    return EnumStringHelper::enumFromString(recTypeStr, modelRecordTypeTable, ModelRecordType::Unknown);
}

std::string PGPlugin::stringFromRecType(const ModelRecordType& recType)
{
    return std::string(EnumStringHelper::stringFromEnum(recType, modelRecordTypeTable, ""));
}

std::vector<std::string> PGPlugin::availableRecTypeStrs()
{
    return EnumStringHelper::allEnumStrings(modelRecordTypeTable);
}

auto PGPlugin::defaultRecTypeSet() -> std::unordered_set<ModelRecordType>
{
    // These are enabled by default in the initial config.
    static const std::unordered_set<ModelRecordType> defaultSet = {
        ModelRecordType::Activator,
        ModelRecordType::Ammunition,
        ModelRecordType::AnimatedObject,
        ModelRecordType::Armor,
        ModelRecordType::ArmorAddon,
        ModelRecordType::ArtObject,
        ModelRecordType::BodyPartData,
        ModelRecordType::Book,
        ModelRecordType::CameraShot,
        ModelRecordType::Climate,
        ModelRecordType::Container,
        ModelRecordType::Door,
        ModelRecordType::Explosion,
        ModelRecordType::Flora,
        ModelRecordType::Furniture,
        ModelRecordType::Grass,
        ModelRecordType::Hazard,
        ModelRecordType::HeadPart,
        ModelRecordType::IdleMarker,
        ModelRecordType::Impact,
        ModelRecordType::Ingestible,
        ModelRecordType::Ingredient,
        ModelRecordType::Key,
        ModelRecordType::LeveledNPC,
        ModelRecordType::Light,
        ModelRecordType::MaterialObject,
        ModelRecordType::MiscItem,
        ModelRecordType::MoveableStatic,
        ModelRecordType::Projectile,
        ModelRecordType::Scroll,
        ModelRecordType::SoulGem,
        ModelRecordType::StaticObject,
        ModelRecordType::TalkingActivator,
        ModelRecordType::Tree,
        ModelRecordType::Weapon,
    };

    return defaultSet;
}

auto PGPlugin::dynDOLODRecTypeSet() -> std::unordered_set<ModelRecordType>
{
    // The record types DynDOLOD reads full models from, as its reference lists them.
    static const std::unordered_set<ModelRecordType> dynDOLODSet = {
        ModelRecordType::Activator,    ModelRecordType::Container, ModelRecordType::Door,
        ModelRecordType::Furniture,    ModelRecordType::Grass,     ModelRecordType::MoveableStatic,
        ModelRecordType::StaticObject, ModelRecordType::Tree,
    };

    return dynDOLODSet;
}

std::filesystem::path PGPlugin::pluginPathFromDataPath(const std::filesystem::path& dataPath)
{
    static const std::filesystem::path meshesPrefix = "meshes";
    static const std::filesystem::path texturesPrefix = "textures";

    auto relativePath = dataPath;

    // Check if the first component is "meshes" or "textures".
    if (!dataPath.empty()) {
        auto iter = dataPath.begin();
        if (*iter == meshesPrefix) {
            // Erase the first component.
            relativePath = std::filesystem::path { };
            for (++iter; iter != dataPath.end(); ++iter)
                relativePath /= *iter;
        } else if (*iter == texturesPrefix) {
            relativePath = std::filesystem::path { };
            for (++iter; iter != dataPath.end(); ++iter)
                relativePath /= *iter;
        } else {
            return dataPath;
        }
    }

    return relativePath;
}

bool PGPlugin::isUseOrderedBefore(const UseOrder& a,
                                  const UseOrder& b)
{
    if (a.hasAlternateTextures != b.hasAlternateTextures)
        return !a.hasAlternateTextures; // no alternate textures first
    if (a.isWeighted != b.isWeighted)
        return a.isWeighted; // weighted first
    if (a.modName != b.modName)
        return a.modName < b.modName; // alphabetical mod name
    if (a.formID != b.formID)
        return a.formID < b.formID; // ascending form ID
    return a.subModel < b.subModel; // alphabetical sub model
}
