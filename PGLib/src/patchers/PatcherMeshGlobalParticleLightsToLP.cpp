#include "patchers/PatcherMeshGlobalParticleLightsToLP.hpp"

#include "patchers/base/PatcherMeshGlobal.hpp"
#include "util/Logger.hpp"
#include "util/ParallaxGenUtil.hpp"

#include "Animation.hpp"
#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "Keys.hpp"
#include "NifFile.hpp"
#include "Nodes.hpp"
#include "Object3d.hpp"
#include "Particles.hpp"
#include "Shaders.hpp"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

// statics
nlohmann::json PatcherMeshGlobalParticleLightsToLP::s_lpJsonData;
mutex PatcherMeshGlobalParticleLightsToLP::s_lpJsonDataMutex;

PatcherMeshGlobalParticleLightsToLP::PatcherMeshGlobalParticleLightsToLP(
    std::filesystem::path nifPath, nifly::NifFile* nif)
    : PatcherMeshGlobal(std::move(nifPath), nif, "ParticleLightsToLP")
{
}

auto PatcherMeshGlobalParticleLightsToLP::getFactory() -> PatcherMeshGlobal::PatcherMeshGlobalFactory
{
    return [](const filesystem::path& nifPath, nifly::NifFile* nif) -> unique_ptr<PatcherMeshGlobal> {
        return make_unique<PatcherMeshGlobalParticleLightsToLP>(nifPath, nif);
    };
}

auto PatcherMeshGlobalParticleLightsToLP::applyPatch() -> bool
{
    // Loop through all blocks to find alpha properties
    // Determine if NIF has attached havok animations
    vector<NiObject*> nifBlockTree;
    getNIF()->GetTree(nifBlockTree);

    bool appliedPatch = false;

    for (NiObject* nifBlock : nifBlockTree) {
        if (!boost::iequals(nifBlock->GetBlockName(), "NiBillboardNode")) {
            continue;
        }
        auto* const billboardNode = dynamic_cast<nifly::NiBillboardNode*>(nifBlock);

        // Get children
        set<NiRef*> childRefs;
        billboardNode->GetChildRefs(childRefs);

        if (childRefs.empty()) {
            // no children
            continue;
        }

        // get relevant objects
        nifly::NiShape* shape = nullptr;
        nifly::NiAlphaProperty* alphaProperty = nullptr;
        nifly::BSEffectShaderProperty* effectShader = nullptr;
        nifly::NiParticleSystem* particleSystem = nullptr;

        // Loop through children and assign whatever is found
        for (auto* childRef : childRefs) {
            if (shape == nullptr) {
                shape = getNIF()->GetHeader().GetBlock<NiShape>(childRef);
            }

            if (particleSystem == nullptr) {
                particleSystem = getNIF()->GetHeader().GetBlock<NiParticleSystem>(childRef);
            }
        }

        if (shape == nullptr) {
            // No shape available
            continue;
        }

        if (particleSystem != nullptr) {
            // This node has a particle system
            // Find the alpha property and effect shader from particle system
            const auto psAlphaPropertyRef = particleSystem->alphaPropertyRef;
            if (!psAlphaPropertyRef.IsEmpty() && alphaProperty == nullptr) {
                alphaProperty = getNIF()->GetHeader().GetBlock<NiAlphaProperty>(psAlphaPropertyRef);
            }

            const auto psShaderPropertyRef = particleSystem->shaderPropertyRef;
            if (!psShaderPropertyRef.IsEmpty() && effectShader == nullptr) {
                effectShader = getNIF()->GetHeader().GetBlock<BSEffectShaderProperty>(psShaderPropertyRef);
            }
        }

        if (particleSystem == nullptr) {
            // This node does not have a particle system
            // Find the alpha property and effect shader from shape
            auto* const shapeAlphaPropertyRef = shape->AlphaPropertyRef();
            if (!shapeAlphaPropertyRef->IsEmpty() && alphaProperty == nullptr) {
                alphaProperty = getNIF()->GetHeader().GetBlock<NiAlphaProperty>(shapeAlphaPropertyRef);
            }

            auto* const shapeShaderPropertyRef = shape->ShaderPropertyRef();
            if (!shapeShaderPropertyRef->IsEmpty() && effectShader == nullptr) {
                effectShader = getNIF()->GetHeader().GetBlock<BSEffectShaderProperty>(shapeShaderPropertyRef);
            }
        }

        if (alphaProperty == nullptr || effectShader == nullptr) {
            // No alpha property or effect shader
            continue;
        }

        // Validate that all parameters are met for a particle light
        if (alphaProperty->flags != PARTICLE_LIGHT_FLAGS) {
            // no additive blending
            continue;
        }

        if ((effectShader->shaderFlags1 & SLSF1_SOFT_EFFECT) == 0U) {
            // no soft effect shader flag
            continue;
        }

        Logger::trace(L"Found Particle Light: {}", ParallaxGenUtil::asciitoUTF16(nifBlock->GetBlockName()));

        // Apply patch to this particle light
        if (applySinglePatch(billboardNode, shape, effectShader)) {
            // Delete block if patch was applied
            appliedPatch = true;
            const auto nifBlockRef = nifly::NiRef(getNIF()->GetBlockID(nifBlock));
            getNIF()->GetHeader().DeleteBlock(nifBlockRef);
        }
    }

    // delete unreferenced blocks (there are probably a lot)
    getNIF()->DeleteUnreferencedBlocks();

    return appliedPatch;
}

auto PatcherMeshGlobalParticleLightsToLP::applySinglePatch(
    nifly::NiBillboardNode* node, nifly::NiShape* shape, nifly::BSEffectShaderProperty* effectShader) -> bool
{
    // Start generating LP JSON
    nlohmann::json lpJson;

    // Set model path
    lpJson["models"] = nlohmann::json::array();

    // Remove "meshes\\" from start of path
    const auto nifPath = boost::ireplace_first_copy(getNIFPath().string(), "meshes\\", "");
    lpJson["models"].push_back(nifPath);

    // Create lights array
    lpJson["lights"] = nlohmann::json::array();

    // LightEntry will hold all JSON data for light
    nlohmann::json lightEntry;
    lightEntry["data"] = nlohmann::json::object();

    // Set position
    MatTransform globalPosition;
    getNIF()->GetNodeTransformToGlobal(node->name.get(), globalPosition);

    // Setup points array
    lightEntry["points"] = nlohmann::json::array();
    lightEntry["points"].push_back(nlohmann::json::array({ round(globalPosition.translation.x * 100.0) / 100.0,
        round(globalPosition.translation.y * 100.0) / 100.0, round(globalPosition.translation.z * 100.0) / 100.0 }));

    // Set Light
    lightEntry["data"]["light"] = "MagicLightWhite01"; // Placeholder light that will be overridden

    // BSTriShape conversion
    auto* const shapeBSTriShape = dynamic_cast<nifly::BSTriShape*>(shape);
    if (shapeBSTriShape == nullptr) {
        // not a BSTriShape
        // TODO support other shape types?
        return false;
    }

    const auto vertData = shapeBSTriShape->vertData;
    const auto numVerts = vertData.size();

    // set color
    auto baseColor = effectShader->baseColor;
    const bool getColorFromVertex = abs(baseColor.r - 1.0) < MIN_VALUE && abs(baseColor.g - 1.0) < MIN_VALUE
        && abs(baseColor.b - 1.0) < MIN_VALUE && shape->HasVertexColors();
    if (getColorFromVertex) {
        // Reset basecolor
        baseColor = { 0, 0, 0, 0 };

        // Get color from vertex
        for (const auto& vertex : vertData) {
            baseColor.r += static_cast<float>(vertex.colorData[0]);
            baseColor.g += static_cast<float>(vertex.colorData[1]);
            baseColor.b += static_cast<float>(vertex.colorData[2]);
        }

        baseColor.r /= static_cast<float>(numVerts);
        baseColor.g /= static_cast<float>(numVerts);
        baseColor.b /= static_cast<float>(numVerts);
    } else {
        // Convert to 0-255
        baseColor *= WHITE_COLOR;
    }

    lightEntry["data"]["color"] = nlohmann::json::array(
        { static_cast<int>(baseColor.r), static_cast<int>(baseColor.g), static_cast<int>(baseColor.b) });

    // Calculate radius from average of vertex distances
    double radius = 0.0;
    for (const auto& vertex : vertData) {
        // Find distance with pythagorean theorem
        const auto curDistance = sqrt(pow(vertex.vert.x, 2) + pow(vertex.vert.y, 2) + pow(vertex.vert.z, 2));
        // update radius with average
        radius += curDistance;
    }

    radius /= static_cast<double>(numVerts);

    lightEntry["data"]["radius"] = static_cast<int>(radius);

    lightEntry["data"]["fade"] = round(effectShader->baseColorScale * 100.0) / 100.0;

    // Get controllers
    auto controllerRef = effectShader->controllerRef;
    while (!controllerRef.IsEmpty()) {
        auto* const controller = getNIF()->GetHeader().GetBlock(controllerRef);
        if (controller == nullptr) {
            break;
        }

        string jsonField;
        const auto controllerJson = getControllerJSON(controller, jsonField);
        if (!controllerJson.empty()) {
            lightEntry["data"][jsonField] = controllerJson;
        }

        controllerRef = controller->nextControllerRef;
    }

    lpJson["lights"].push_back(lightEntry);

    // Save JSON
    {
        const lock_guard<mutex> lock(s_lpJsonDataMutex);
        PatcherMeshGlobalParticleLightsToLP::s_lpJsonData.push_back(lpJson);
    }

    return true;
}

auto PatcherMeshGlobalParticleLightsToLP::getControllerJSON(nifly::NiTimeController* controller, string& jsonField)
    -> nlohmann::json
{
    nlohmann::json controllerJson = nlohmann::json::object();

    if (controller == nullptr) {
        return controllerJson;
    }

    auto* const floatController = dynamic_cast<nifly::BSEffectShaderPropertyFloatController*>(controller);
    NiBlockRef<NiInterpController> interpRef;
    if (floatController != nullptr) {
        if (floatController->typeOfControlledVariable != 0) {
            // We don't care about this controller (not controlling emissive mult)
            return controllerJson;
        }

        interpRef = floatController->interpolatorRef;

        jsonField = "fadeController";
    }

    auto* const colorController = dynamic_cast<nifly::BSEffectShaderPropertyColorController*>(controller);
    if (colorController != nullptr) {
        interpRef = colorController->interpolatorRef;

        jsonField = "colorController";
    }

    if ((floatController == nullptr && colorController == nullptr)
        || (floatController != nullptr && colorController != nullptr)) {
        // We don't care about this controller
        return controllerJson;
    }

    if (interpRef.IsEmpty()) {
        return controllerJson;
    }

    // Find data block
    nifly::NiFloatData* fadeDataBlock = nullptr;
    nifly::NiPosData* colorDataBlock = nullptr;

    auto* const floatInterpolator = getNIF()->GetHeader().GetBlock<NiFloatInterpolator>(interpRef);
    if (floatInterpolator != nullptr) {
        const auto dataRef = floatInterpolator->dataRef;
        fadeDataBlock = getNIF()->GetHeader().GetBlock<nifly::NiFloatData>(dataRef);
    }

    auto* const niPoint3Interpolator = dynamic_cast<nifly::NiPoint3Interpolator*>(floatInterpolator);
    if (niPoint3Interpolator != nullptr) {
        const auto dataRef = niPoint3Interpolator->dataRef;
        colorDataBlock = getNIF()->GetHeader().GetBlock<nifly::NiPosData>(dataRef);
    }

    if ((fadeDataBlock == nullptr && floatController != nullptr)
        || (colorDataBlock == nullptr && colorController != nullptr)) {
        // Could not find data block
        return controllerJson;
    }

    controllerJson["keys"] = nlohmann::json::array();

    nifly::NiKeyType interpolationType = nifly::NiKeyType::LINEAR_KEY;
    if (floatController != nullptr) {
        interpolationType = fadeDataBlock->data.GetInterpolationType();
    } else if (colorController != nullptr) {
        interpolationType = colorDataBlock->data.GetInterpolationType();
    }

    // Get interpolation method
    switch (interpolationType) {
    case nifly::NiKeyType::LINEAR_KEY:
        controllerJson["interpolation"] = "Linear";
        break;
    case nifly::NiKeyType::QUADRATIC_KEY:
        controllerJson["interpolation"] = "Cubic";
        break;
    case nifly::NiKeyType::NO_INTERP:
        controllerJson["interpolation"] = "Step";
        break;
    default:
        // Linear interpolation default
        controllerJson["interpolation"] = "Linear";
        break;
    }

    // Add keys to JSON
    uint32_t numKeys = 0;
    if (floatController != nullptr) {
        numKeys = fadeDataBlock->data.GetNumKeys();
    } else if (colorController != nullptr) {
        numKeys = colorDataBlock->data.GetNumKeys();
    }

    for (uint32_t i = 0; i < numKeys; i++) {
        if (floatController != nullptr) {
            const auto curKey = fadeDataBlock->data.GetKey(static_cast<int>(i));

            controllerJson["keys"].push_back(
                nlohmann::json::object({ { "time", round(curKey.time * ROUNDING_VALUE) / ROUNDING_VALUE },
                    { "value", round(curKey.value * ROUNDING_VALUE) / ROUNDING_VALUE },
                    { "forward", round(curKey.forward * ROUNDING_VALUE) / ROUNDING_VALUE },
                    { "backward", round(curKey.backward * ROUNDING_VALUE) / ROUNDING_VALUE } }));
        } else if (colorController != nullptr) {
            const auto curKey = colorDataBlock->data.GetKey(static_cast<int>(i));

            controllerJson["keys"].push_back(
                nlohmann::json::object({ { "time", round(curKey.time * ROUNDING_VALUE) / ROUNDING_VALUE },
                    { "color",
                        nlohmann::json::array({ static_cast<int>(curKey.value.x), static_cast<int>(curKey.value.y),
                            static_cast<int>(curKey.value.z) }) },
                    { "forward",
                        nlohmann::json::array({ static_cast<int>(curKey.forward.x), static_cast<int>(curKey.forward.y),
                            static_cast<int>(curKey.forward.z) }) },
                    { "backward",
                        nlohmann::json::array({ static_cast<int>(curKey.backward.x),
                            static_cast<int>(curKey.backward.y), static_cast<int>(curKey.backward.z) }) } }));
        }
    }

    return controllerJson;
}

void PatcherMeshGlobalParticleLightsToLP::finalize()
{
    const lock_guard<mutex> lock(s_lpJsonDataMutex);

    // Check if output JSON is empty
    if (s_lpJsonData.empty()) {
        return;
    }

    // Key type for grouping: the stringified "models" array.
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<nlohmann::json>>> modelsToDataPoints;

    // Merge all lights into a single JSON object
    for (auto& obj : s_lpJsonData) {
        const std::string modelsKey = obj["models"].dump();

        auto& dataGroup = modelsToDataPoints[modelsKey];

        for (auto& light : obj["lights"]) {
            const std::string dataKey = light["data"].dump();
            for (auto& p : light["points"]) {
                dataGroup[dataKey].push_back(p);
            }
        }
    }

    nlohmann::json mergedOutput = nlohmann::json::array();

    for (auto& kv : modelsToDataPoints) {
        const auto& modelsKey = kv.first;
        const auto& dataGroup = kv.second;

        const nlohmann::json modelsJson = nlohmann::json::parse(modelsKey);

        nlohmann::json lightsArray = nlohmann::json::array();

        for (const auto& dataKv : dataGroup) {
            const std::string dataKey = dataKv.first;
            const nlohmann::json dataObj = nlohmann::json::parse(dataKey);

            const auto& allPoints = dataKv.second; // vector<json>

            nlohmann::json lightEntry;
            lightEntry["data"] = dataObj;
            lightEntry["points"] = nlohmann::json::array();

            for (const auto& pt : allPoints) {
                lightEntry["points"].push_back(pt);
            }

            lightsArray.push_back(lightEntry);
        }

        // Now assemble the final group object
        nlohmann::json groupObj;
        groupObj["models"] = modelsJson;
        groupObj["lights"] = lightsArray;

        // Add to the final merged output
        mergedOutput.push_back(groupObj);
    }

    const auto outputJSON = getPGD()->getGeneratedPath() / "LightPlacer/parallaxgen.json";

    // Create directories for parent path
    filesystem::create_directories(outputJSON.parent_path());

    // Save JSON to file
    ofstream lpJsonFile(outputJSON);
    lpJsonFile << mergedOutput.dump(2) << "\n";
    lpJsonFile.close();
}
