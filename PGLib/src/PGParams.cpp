#include "PGParams.hpp"

#include "PGModManager.hpp"
#include "PGPlugin.hpp"
#include "common/BethesdaGame.hpp"
#include "pgutil/PGEnums.hpp"
#include "util/StringUtil.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
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

void PGParams::readJSON(const nlohmann::json& paramJ)
{
    // "game".
    if (paramJ.contains("game") && paramJ["game"].contains("dir"))
        game.dir = StringUtil::utf8toUTF16(paramJ["game"]["dir"].get<std::string>());
    if (paramJ.contains("game") && paramJ["game"].contains("type"))
        paramJ["game"]["type"].get_to<BethesdaGame::GameType>(game.type);

    // "modmanager".
    if (paramJ.contains("modmanager") && paramJ["modmanager"].contains("type"))
        paramJ["modmanager"]["type"].get_to<PGModManager::ModManagerType>(modManager.type);
    if (paramJ.contains("modmanager") && paramJ["modmanager"].contains("mo2instancedir"))
        paramJ["modmanager"]["mo2instancedir"].get_to<std::filesystem::path>(modManager.mo2InstanceDir);
    if (paramJ.contains("modmanager") && paramJ["modmanager"].contains("mo2useloosefileorder"))
        paramJ["modmanager"]["mo2useloosefileorder"].get_to<bool>(modManager.shouldUseMO2LooseFileOrder);

    // "output".
    if (paramJ.contains("output") && paramJ["output"].contains("dir"))
        output.dir = StringUtil::utf8toUTF16(paramJ["output"]["dir"].get<std::string>());
    if (paramJ.contains("output") && paramJ["output"].contains("zip"))
        paramJ["output"]["zip"].get_to<bool>(output.zip);
    if (paramJ.contains("output") && paramJ["output"].contains("pluginlang"))
        output.pluginLang = PGPlugin::pluginLangFromString(paramJ["output"]["pluginlang"].get<std::string>());

    // "processing".
    if (paramJ.contains("processing") && paramJ["processing"].contains("multithread"))
        paramJ["processing"]["multithread"].get_to<bool>(processing.multithread);
    if (paramJ.contains("processing") && paramJ["processing"].contains("devmode"))
        paramJ["processing"]["devmode"].get_to<bool>(processing.enableModDevMode);
    if (paramJ.contains("processing") && paramJ["processing"].contains("enabledebuglogging"))
        paramJ["processing"]["enabledebuglogging"].get_to<bool>(processing.enableDebugLogging);
    if (paramJ.contains("processing") && paramJ["processing"].contains("enabletracelogging"))
        paramJ["processing"]["enabletracelogging"].get_to<bool>(processing.enableTraceLogging);
    if (paramJ.contains("processing") && paramJ["processing"].contains("allowlist"))
        for (const auto& item : paramJ["processing"]["allowlist"])
            processing.allowList.push_back(StringUtil::utf8toUTF16(item.get<std::string>()));
    if (paramJ.contains("processing") && paramJ["processing"].contains("blocklist"))
        for (const auto& item : paramJ["processing"]["blocklist"])
            processing.blockList.push_back(StringUtil::utf8toUTF16(item.get<std::string>()));
    if (paramJ.contains("processing") && paramJ["processing"].contains("texturemaps")) {
        for (const auto& item : paramJ["processing"]["texturemaps"].items()) {
            processing.textureMaps.emplace_back(StringUtil::utf8toUTF16(item.key()),
                                                PGEnums::texTypeFromStr(item.value().get<std::string>()));
        }
    }
    if (paramJ.contains("processing") && paramJ["processing"].contains("vanillabsalist"))
        for (const auto& item : paramJ["processing"]["vanillabsalist"])
            processing.vanillaBSAList.push_back(StringUtil::utf8toUTF16(item.get<std::string>()));
    if (paramJ.contains("processing") && paramJ["processing"].contains("allowedmodelrecordtypes")) {
        processing.allowedModelRecordTypes.clear();

        for (const auto& item : paramJ["processing"]["allowedmodelrecordtypes"])
            processing.allowedModelRecordTypes.insert(PGPlugin::recTypeFromString(item.get<std::string>()));
    }

    // "prepatcher".
    if (paramJ.contains("prepatcher") && paramJ["prepatcher"].contains("fixmeshlighting"))
        paramJ["prepatcher"]["fixmeshlighting"].get_to<bool>(prePatcher.isFixMeshLightingEnabled);

    // "shaderpatcher".
    if (paramJ.contains("shaderpatcher") && paramJ["shaderpatcher"].contains("parallax"))
        paramJ["shaderpatcher"]["parallax"].get_to<bool>(shaderPatcher.isParallaxEnabled);
    if (paramJ.contains("shaderpatcher") && paramJ["shaderpatcher"].contains("complexmaterial"))
        paramJ["shaderpatcher"]["complexmaterial"].get_to<bool>(shaderPatcher.isComplexMaterialEnabled);
    if (paramJ.contains("shaderpatcher") && paramJ["shaderpatcher"].contains("truepbr"))
        paramJ["shaderpatcher"]["truepbr"].get_to<bool>(shaderPatcher.isTruePBREnabled);

    // "shadertransforms".
    if (paramJ.contains("shadertransforms") && paramJ["shadertransforms"].contains("parallaxtocm"))
        paramJ["shadertransforms"]["parallaxtocm"].get_to<bool>(shaderTransforms.isParallaxToCMEnabled);

    // "postpatcher".
    if (paramJ.contains("postpatcher") && paramJ["postpatcher"].contains("disableprepatchedmaterials"))
        paramJ["postpatcher"]["disableprepatchedmaterials"].get_to<bool>(postPatcher.disablePrePatchedMaterials);
    if (paramJ.contains("postpatcher") && paramJ["postpatcher"].contains("fixsss"))
        paramJ["postpatcher"]["fixsss"].get_to<bool>(postPatcher.isFixSSSEnabled);
    if (paramJ.contains("postpatcher") && paramJ["postpatcher"].contains("hairflowmap"))
        paramJ["postpatcher"]["hairflowmap"].get_to<bool>(postPatcher.isHairFlowMapEnabled);

    // "globalpatcher".
}

void PGParams::writeJSON(nlohmann::json& j) const
{
    // "game".
    j["game"]["dir"] = StringUtil::utf16toUTF8(game.dir.wstring());
    j["game"]["type"] = game.type;

    // "modmanager".
    j["modmanager"]["type"] = modManager.type;
    j["modmanager"]["mo2instancedir"] = StringUtil::utf16toUTF8(modManager.mo2InstanceDir.wstring());
    j["modmanager"]["mo2useloosefileorder"] = modManager.shouldUseMO2LooseFileOrder;

    // "output".
    j["output"]["dir"] = StringUtil::utf16toUTF8(output.dir.wstring());
    j["output"]["zip"] = output.zip;
    j["output"]["pluginlang"] = PGPlugin::stringFromPluginLang(output.pluginLang);

    // "processing".
    j["processing"]["multithread"] = processing.multithread;
    j["processing"]["devmode"] = processing.enableModDevMode;
    j["processing"]["enabledebuglogging"] = processing.enableDebugLogging;
    j["processing"]["enabletracelogging"] = processing.enableTraceLogging;
    j["processing"]["allowlist"] = StringUtil::utf16VectorToUTF8(processing.allowList);
    j["processing"]["blocklist"] = StringUtil::utf16VectorToUTF8(processing.blockList);
    j["processing"]["texturemaps"] = nlohmann::json::object();
    for (const auto& [key, value] : processing.textureMaps)
        j["processing"]["texturemaps"][StringUtil::utf16toUTF8(key)] = PGEnums::strFromTexType(value);
    j["processing"]["vanillabsalist"] = StringUtil::utf16VectorToUTF8(processing.vanillaBSAList);
    j["processing"]["allowedmodelrecordtypes"] = nlohmann::json::array();
    for (const auto& item : processing.allowedModelRecordTypes)
        j["processing"]["allowedmodelrecordtypes"].push_back(PGPlugin::stringFromRecType(item));

    // "prepatcher".
    j["prepatcher"]["fixmeshlighting"] = prePatcher.isFixMeshLightingEnabled;

    // "shaderpatcher".
    j["shaderpatcher"]["parallax"] = shaderPatcher.isParallaxEnabled;
    j["shaderpatcher"]["complexmaterial"] = shaderPatcher.isComplexMaterialEnabled;
    j["shaderpatcher"]["truepbr"] = shaderPatcher.isTruePBREnabled;

    // "shadertransforms".
    j["shadertransforms"]["parallaxtocm"] = shaderTransforms.isParallaxToCMEnabled;

    // "postpatcher".
    j["postpatcher"]["disableprepatchedmaterials"] = postPatcher.disablePrePatchedMaterials;
    j["postpatcher"]["fixsss"] = postPatcher.isFixSSSEnabled;
    j["postpatcher"]["hairflowmap"] = postPatcher.isHairFlowMapEnabled;

    // "globalpatcher".
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
