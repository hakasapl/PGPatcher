#include "PGParams.hpp"

#include "PGModManager.hpp"
#include "PGPlugin.hpp"
#include "common/BethesdaGame.hpp"
#include "pgutil/PGEnums.hpp"
#include "util/StringUtil.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

PGParams PGParams::defaults()
{
    PGParams outParams;

    // Game.
    outParams.game.dir = BethesdaGame::findGamePathFromSteam(BethesdaGame::GameType::SkyrimSE);

    // Mesh Rules.
    static const std::vector<std::wstring> defaultMeshBlocklist
        = { L"*\\cameras\\*", L"*\\dyndolod\\*", L"*\\lod\\*", L"*_lod_*", L"*_lod.*", L"*\\markers\\*" };
    outParams.processing.blockList = defaultMeshBlocklist;

    // Texture Rules.
    static const std::vector<std::wstring> defaultVanillaBSAList = {
        L"Skyrim - Textures0.bsa",
        L"Skyrim - Textures1.bsa",
        L"Skyrim - Textures2.bsa",
        L"Skyrim - Textures3.bsa",
        L"Skyrim - Textures4.bsa",
        L"Skyrim - Textures5.bsa",
        L"Skyrim - Textures6.bsa",
        L"Skyrim - Textures7.bsa",
        L"Skyrim - Textures8.bsa",
        L"Project Clarity AIO Half Res Packed.bsa",
        L"Project Clarity AIO Half Res Packed - Textures.bsa",
        L"Project Clarity AIO Half Res Packed0 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed1 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed2 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed3 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed4 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed5 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed6 - Textures.bsa",
    };
    outParams.processing.vanillaBSAList = defaultVanillaBSAList;

    return outParams;
}

void PGParams::readJSON(const nlohmann::json& paramsJSON)
{
    // "game".
    if (paramsJSON.contains("game") && paramsJSON["game"].contains("dir"))
        game.dir = StringUtil::utf8toUTF16(paramsJSON["game"]["dir"].get<std::string>());
    if (paramsJSON.contains("game") && paramsJSON["game"].contains("type"))
        paramsJSON["game"]["type"].get_to<BethesdaGame::GameType>(game.type);

    // "modmanager".
    if (paramsJSON.contains("modmanager") && paramsJSON["modmanager"].contains("type"))
        paramsJSON["modmanager"]["type"].get_to<PGModManager::ModManagerType>(modManager.type);
    if (paramsJSON.contains("modmanager") && paramsJSON["modmanager"].contains("mo2instancedir"))
        paramsJSON["modmanager"]["mo2instancedir"].get_to<std::filesystem::path>(modManager.mo2InstanceDir);
    if (paramsJSON.contains("modmanager") && paramsJSON["modmanager"].contains("mo2useloosefileorder"))
        paramsJSON["modmanager"]["mo2useloosefileorder"].get_to<bool>(modManager.shouldUseMO2LooseFileOrder);

    // "output".
    if (paramsJSON.contains("output") && paramsJSON["output"].contains("dir"))
        output.dir = StringUtil::utf8toUTF16(paramsJSON["output"]["dir"].get<std::string>());
    if (paramsJSON.contains("output") && paramsJSON["output"].contains("zip"))
        paramsJSON["output"]["zip"].get_to<bool>(output.zip);
    if (paramsJSON.contains("output") && paramsJSON["output"].contains("pluginlang"))
        output.pluginLang = PGPlugin::pluginLangFromString(paramsJSON["output"]["pluginlang"].get<std::string>());

    // "processing".
    if (paramsJSON.contains("processing") && paramsJSON["processing"].contains("multithread"))
        paramsJSON["processing"]["multithread"].get_to<bool>(processing.multithread);
    if (paramsJSON.contains("processing") && paramsJSON["processing"].contains("devmode"))
        paramsJSON["processing"]["devmode"].get_to<bool>(processing.enableModDevMode);
    if (paramsJSON.contains("processing") && paramsJSON["processing"].contains("enabledebuglogging"))
        paramsJSON["processing"]["enabledebuglogging"].get_to<bool>(processing.enableDebugLogging);
    if (paramsJSON.contains("processing") && paramsJSON["processing"].contains("enabletracelogging"))
        paramsJSON["processing"]["enabletracelogging"].get_to<bool>(processing.enableTraceLogging);
    if (paramsJSON.contains("processing") && paramsJSON["processing"].contains("pglitemode"))
        paramsJSON["processing"]["pglitemode"].get_to<bool>(processing.isPGLiteMode);
    if (paramsJSON.contains("processing") && paramsJSON["processing"].contains("allowlist"))
        for (const auto& item : paramsJSON["processing"]["allowlist"])
            processing.allowList.push_back(StringUtil::utf8toUTF16(item.get<std::string>()));
    if (paramsJSON.contains("processing") && paramsJSON["processing"].contains("blocklist"))
        for (const auto& item : paramsJSON["processing"]["blocklist"])
            processing.blockList.push_back(StringUtil::utf8toUTF16(item.get<std::string>()));
    if (paramsJSON.contains("processing") && paramsJSON["processing"].contains("texturemaps")) {
        for (const auto& item : paramsJSON["processing"]["texturemaps"].items()) {
            processing.textureMaps.emplace_back(StringUtil::utf8toUTF16(item.key()),
                                                PGEnums::texTypeFromStr(item.value().get<std::string>()));
        }
    }

    // The rules in the file's spelling, whatever form the file has them in (the keys of a JSON object are not touched
    // by replaceForwardSlashes()).
    normalizeRules();
    if (paramsJSON.contains("processing") && paramsJSON["processing"].contains("vanillabsalist"))
        for (const auto& item : paramsJSON["processing"]["vanillabsalist"])
            processing.vanillaBSAList.push_back(StringUtil::utf8toUTF16(item.get<std::string>()));
    if (paramsJSON.contains("processing") && paramsJSON["processing"].contains("allowedmodelrecordtypes")) {
        processing.allowedModelRecordTypes.clear();

        for (const auto& item : paramsJSON["processing"]["allowedmodelrecordtypes"])
            processing.allowedModelRecordTypes.insert(PGPlugin::recTypeFromString(item.get<std::string>()));
    }

    // "prepatcher".
    if (paramsJSON.contains("prepatcher") && paramsJSON["prepatcher"].contains("fixmeshlighting"))
        paramsJSON["prepatcher"]["fixmeshlighting"].get_to<bool>(prePatcher.isFixMeshLightingEnabled);

    // "shaderpatcher".
    if (paramsJSON.contains("shaderpatcher") && paramsJSON["shaderpatcher"].contains("parallax"))
        paramsJSON["shaderpatcher"]["parallax"].get_to<bool>(shaderPatcher.isParallaxEnabled);
    if (paramsJSON.contains("shaderpatcher") && paramsJSON["shaderpatcher"].contains("complexmaterial"))
        paramsJSON["shaderpatcher"]["complexmaterial"].get_to<bool>(shaderPatcher.isComplexMaterialEnabled);
    if (paramsJSON.contains("shaderpatcher") && paramsJSON["shaderpatcher"].contains("truepbr"))
        paramsJSON["shaderpatcher"]["truepbr"].get_to<bool>(shaderPatcher.isTruePBREnabled);

    // "shadertransforms".
    if (paramsJSON.contains("shadertransforms") && paramsJSON["shadertransforms"].contains("parallaxtocm"))
        paramsJSON["shadertransforms"]["parallaxtocm"].get_to<bool>(shaderTransforms.isParallaxToCMEnabled);

    // "postpatcher".
    if (paramsJSON.contains("postpatcher") && paramsJSON["postpatcher"].contains("disableprepatchedmaterials"))
        paramsJSON["postpatcher"]["disableprepatchedmaterials"].get_to<bool>(postPatcher.disablePrePatchedMaterials);
    if (paramsJSON.contains("postpatcher") && paramsJSON["postpatcher"].contains("fixsss"))
        paramsJSON["postpatcher"]["fixsss"].get_to<bool>(postPatcher.isFixSSSEnabled);
    if (paramsJSON.contains("postpatcher") && paramsJSON["postpatcher"].contains("hairflowmap"))
        paramsJSON["postpatcher"]["hairflowmap"].get_to<bool>(postPatcher.isHairFlowMapEnabled);

    // "globalpatcher".
    if (paramsJSON.contains("globalpatcher") && paramsJSON["globalpatcher"].contains("recalculatebounds"))
        paramsJSON["globalpatcher"]["recalculatebounds"].get_to<bool>(globalPatcher.isRecalculateBoundsEnabled);
}

void PGParams::writeJSON(nlohmann::json& paramsJSON) const
{
    // "game".
    paramsJSON["game"]["dir"] = StringUtil::utf16toUTF8(game.dir.wstring());
    paramsJSON["game"]["type"] = game.type;

    // "modmanager".
    paramsJSON["modmanager"]["type"] = modManager.type;
    paramsJSON["modmanager"]["mo2instancedir"] = StringUtil::utf16toUTF8(modManager.mo2InstanceDir.wstring());
    paramsJSON["modmanager"]["mo2useloosefileorder"] = modManager.shouldUseMO2LooseFileOrder;

    // "output".
    paramsJSON["output"]["dir"] = StringUtil::utf16toUTF8(output.dir.wstring());
    paramsJSON["output"]["zip"] = output.zip;
    paramsJSON["output"]["pluginlang"] = PGPlugin::stringFromPluginLang(output.pluginLang);

    // "processing".
    paramsJSON["processing"]["multithread"] = processing.multithread;
    paramsJSON["processing"]["devmode"] = processing.enableModDevMode;
    paramsJSON["processing"]["enabledebuglogging"] = processing.enableDebugLogging;
    paramsJSON["processing"]["enabletracelogging"] = processing.enableTraceLogging;
    paramsJSON["processing"]["pglitemode"] = processing.isPGLiteMode;
    paramsJSON["processing"]["allowlist"] = StringUtil::utf16VectorToUTF8(processing.allowList);
    paramsJSON["processing"]["blocklist"] = StringUtil::utf16VectorToUTF8(processing.blockList);
    paramsJSON["processing"]["texturemaps"] = nlohmann::json::object();
    for (const auto& [key, value] : processing.textureMaps)
        paramsJSON["processing"]["texturemaps"][StringUtil::utf16toUTF8(key)] = PGEnums::strFromTexType(value);
    paramsJSON["processing"]["vanillabsalist"] = StringUtil::utf16VectorToUTF8(processing.vanillaBSAList);
    paramsJSON["processing"]["allowedmodelrecordtypes"] = nlohmann::json::array();
    for (const auto& item : processing.allowedModelRecordTypes)
        paramsJSON["processing"]["allowedmodelrecordtypes"].push_back(PGPlugin::stringFromRecType(item));

    // "prepatcher".
    paramsJSON["prepatcher"]["fixmeshlighting"] = prePatcher.isFixMeshLightingEnabled;

    // "shaderpatcher".
    paramsJSON["shaderpatcher"]["parallax"] = shaderPatcher.isParallaxEnabled;
    paramsJSON["shaderpatcher"]["complexmaterial"] = shaderPatcher.isComplexMaterialEnabled;
    paramsJSON["shaderpatcher"]["truepbr"] = shaderPatcher.isTruePBREnabled;

    // "shadertransforms".
    paramsJSON["shadertransforms"]["parallaxtocm"] = shaderTransforms.isParallaxToCMEnabled;

    // "postpatcher".
    paramsJSON["postpatcher"]["disableprepatchedmaterials"] = postPatcher.disablePrePatchedMaterials;
    paramsJSON["postpatcher"]["fixsss"] = postPatcher.isFixSSSEnabled;
    paramsJSON["postpatcher"]["hairflowmap"] = postPatcher.isHairFlowMapEnabled;

    // "globalpatcher".
    paramsJSON["globalpatcher"]["recalculatebounds"] = globalPatcher.isRecalculateBoundsEnabled;
}

void PGParams::replaceForwardSlashes(nlohmann::json& json)
{
    if (json.is_string()) {
        auto& str = json.get_ref<std::string&>();
        for (auto& ch : str)
            if (ch == '/')
                ch = '\\';
    } else if (json.is_object()) {
        for (const auto& item : json.items())
            replaceForwardSlashes(item.value());
    } else if (json.is_array()) {
        for (auto& element : json)
            replaceForwardSlashes(element);
    }
}

void PGParams::normalizeRules()
{
    for (auto* const list : { &processing.allowList, &processing.blockList })
        for (auto& rule : *list)
            std::ranges::replace(rule, L'/', L'\\');

    // The texture rules are compared with the mapped paths, which are lower case; the mesh rules are globs, which
    // PathMatchSpecW matches without case.
    for (auto& [path, type] : processing.textureMaps) {
        std::ranges::replace(path, L'/', L'\\');
        StringUtil::toLowerASCIIFastInPlace(path);
    }
}

std::vector<PGParams::ValidationError> PGParams::validate() const
{
    std::vector<ValidationError> errors;
    const auto addError = [&errors](const char* key) { errors.push_back({ .key = key }); };
    const auto addErrorWithItem = [&errors](const char* key, const std::wstring& item) {
        errors.push_back({ .key = key, .hasItem = true, .item = item });
    };
    const auto checkList = [&addError, &addErrorWithItem](
                               const std::vector<std::wstring>& items, const char* emptyKey, const char* duplicateKey) {
        std::unordered_set<std::wstring> seen;
        for (const auto& item : items) {
            if (item.empty())
                addError(emptyKey);

            if (!seen.insert(item).second)
                addErrorWithItem(duplicateKey, item);
        }
    };

    // Shader transforms: the transform needs both of its patchers.
    if (shaderTransforms.isParallaxToCMEnabled
        && (!shaderPatcher.isParallaxEnabled || !shaderPatcher.isComplexMaterialEnabled)) {
        addError("launcher.validation.parallaxToCMRequiresPatchers");
    }

    // Mesh rules.
    checkList(processing.allowList,
              "launcher.validation.meshAllowListEmptyEntry",
              "launcher.validation.meshAllowListDuplicate");
    checkList(processing.blockList,
              "launcher.validation.meshBlockListEmptyEntry",
              "launcher.validation.meshBlockListDuplicate");

    // Texture rules.
    std::vector<std::wstring> textureRuleKeys;
    textureRuleKeys.reserve(processing.textureMaps.size());
    for (const auto& [key, type] : processing.textureMaps)
        textureRuleKeys.push_back(key);
    checkList(textureRuleKeys, "launcher.validation.textureRulesEmptyKey", "launcher.validation.textureRulesDuplicate");

    checkList(processing.vanillaBSAList,
              "launcher.validation.vanillaBSAListEmptyEntry",
              "launcher.validation.vanillaBSAListDuplicate");

    return errors;
}
