#include "PGGlobals.hpp"
#include "ParallaxGen.hpp"
#include "ParallaxGenD3D.hpp"
#include "ParallaxGenDirectory.hpp"
#include "ParallaxGenWarnings.hpp"
#include "patchers/PatcherMeshGlobalFixEffectLightingCS.hpp"
#include "patchers/PatcherMeshGlobalParticleLightsToLP.hpp"
#include "patchers/PatcherMeshPostFixSSS.hpp"
#include "patchers/PatcherMeshPostHairFlowMap.hpp"
#include "patchers/PatcherMeshPostRestoreDefaultShaders.hpp"
#include "patchers/PatcherMeshPreDisableMLP.hpp"
#include "patchers/PatcherMeshPreFixMeshLighting.hpp"
#include "patchers/PatcherMeshPreFixTextureSlotCount.hpp"
#include "patchers/PatcherMeshShaderComplexMaterial.hpp"
#include "patchers/PatcherMeshShaderTransformParallaxToCM.hpp"
#include "patchers/PatcherMeshShaderTruePBR.hpp"
#include "patchers/PatcherMeshShaderVanillaParallax.hpp"
#include "patchers/PatcherTextureGlobalConvertToHDR.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "patchers/base/Patcher.hpp"
#include "patchers/base/PatcherUtil.hpp"
#include "util/ExceptionHandler.hpp"

#include <CLI/CLI.hpp>
#include <cpptrace/from_current.hpp>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>

using namespace std;

namespace {
auto getExecutablePath() -> filesystem::path
{
    array<wchar_t, MAX_PATH> buffer {};
    if (GetModuleFileNameW(nullptr, buffer.data(), MAX_PATH) == 0) {
        cerr << "Error getting executable path: " << GetLastError() << "\n";
        exit(1);
    }

    filesystem::path outPath = filesystem::path(buffer.data());

    if (filesystem::exists(outPath)) {
        return outPath;
    }

    cerr << "Error getting executable path: path does not exist\n";
    exit(1);

    return {};
}

struct PGToolsCLIArgs {
    int verbosity = 0;
    bool multithreading = true;

    struct Patch {
        CLI::App* subCommand = nullptr;
        unordered_set<string> patchers;
        filesystem::path source = ".";
        filesystem::path output = "ParallaxGen_Output";
        bool mapTexturesFromMeshes = false;
        bool highMem = false;
    } Patch;
};

void mainRunner(PGToolsCLIArgs& args)
{
    // Welcome Message
    spdlog::info("Welcome to PGTools version {}!", PG_VERSION);

    // Get EXE path
    const auto exePath = getExecutablePath().parent_path();

#if PG_TEST_BUILD
    // Post test message for test builds
    spdlog::warn("This is an EXPERIMENTAL development build of PG Patcher");
#endif

    ExceptionHandler::setMainThread();

    // Check if patch subcommand was used
    if (args.Patch.subCommand->parsed()) {
        // Get current time to compare later
        auto startTime = chrono::high_resolution_clock::now();
        long long timeTaken = 0;

        args.Patch.source = filesystem::absolute(args.Patch.source);
        args.Patch.output = filesystem::absolute(args.Patch.output);

        auto pgd = ParallaxGenDirectory(args.Patch.source, args.Patch.output);
        PGGlobals::setPGD(&pgd);
        auto pgd3D = ParallaxGenD3D(exePath / "cshaders");
        PGGlobals::setPGD3D(&pgd3D);

        Patcher::loadStatics(pgd, pgd3D);
        ParallaxGenWarnings::init();

        // Check if GPU needs to be initialized
        if (!pgd3D.initGPU()) {
            spdlog::critical("Failed to initialize GPU. Exiting.");
        }

        if (!pgd3D.initShaders()) {
            spdlog::critical("Failed to initialize internal shaders. Exiting.");
        }

        // Create output directory
        try {
            filesystem::create_directories(args.Patch.output);
        } catch (const filesystem::filesystem_error& e) {
            spdlog::error("Failed to create output directory: {}", e.what());
            exit(1);
        }

        // If output dir is the same as data dir meshes might get overwritten
        if (filesystem::equivalent(args.Patch.output, pgd.getDataPath())) {
            spdlog::critical("Output directory cannot be the same directory as your data folder. "
                             "Exiting.");
            exit(1);
        }

        // delete existing output
        ParallaxGen::deleteOutputDir();

        // Init file map
        pgd.populateFileMap(false);

        // Map files
        pgd.mapFiles({}, {}, {}, {}, false, args.multithreading, false);

        // Split patchers into names and options
        unordered_map<string, unordered_map<string, string>> patcherDefs;
        for (const auto& patcher : args.Patch.patchers) {
            auto openBracket = patcher.find('[');
            auto closeBracket = patcher.find(']');
            if (openBracket == string::npos || closeBracket == string::npos) {
                patcherDefs[patcher] = {};
                continue;
            }

            // Get substring between brackets
            auto options = patcher.substr(openBracket + 1, closeBracket - openBracket - 1);
            // Split options by | into unordered set
            unordered_map<string, string> optionSet;
            for (const auto& option : options | views::split('|')) {
                // check if = in option string
                const auto optionStr = string(option.begin(), option.end());
                const auto eqPos = optionStr.find('=');
                if (eqPos != string::npos) {
                    optionSet[optionStr.substr(0, eqPos)] = optionStr.substr(eqPos + 1);
                    continue;
                }

                optionSet[optionStr] = "";
            }

            // Add to set
            patcherDefs[patcher.substr(0, openBracket)] = optionSet;
        }

        // Create patcher factory
        PatcherUtil::PatcherMeshSet meshPatchers;
        if (patcherDefs.contains("disablemlp")) {
            meshPatchers.prePatchers.emplace_back(PatcherMeshPreDisableMLP::getFactory());
        }
        if (patcherDefs.contains("fixmeshlighting")) {
            meshPatchers.prePatchers.emplace_back(PatcherMeshPreFixMeshLighting::getFactory());
        }
        if (patcherDefs.contains("fixtextureslotcount")) {
            meshPatchers.prePatchers.emplace_back(PatcherMeshPreFixTextureSlotCount::getFactory());
        }
        if (patcherDefs.contains("parallax")) {
            meshPatchers.shaderPatchers.emplace(
                PatcherMeshShaderVanillaParallax::getShaderType(), PatcherMeshShaderVanillaParallax::getFactory());
        }
        if (patcherDefs.contains("complexmaterial")) {
            meshPatchers.shaderPatchers.emplace(
                PatcherMeshShaderComplexMaterial::getShaderType(), PatcherMeshShaderComplexMaterial::getFactory());
            PatcherMeshShaderComplexMaterial::loadStatics({});
        }
        if (patcherDefs.contains("truepbr")) {
            meshPatchers.shaderPatchers.emplace(
                PatcherMeshShaderTruePBR::getShaderType(), PatcherMeshShaderTruePBR::getFactory());
            PatcherMeshShaderTruePBR::loadStatics(pgd.getPBRJSONs());
            PatcherMeshShaderTruePBR::loadOptions(patcherDefs["truepbr"]);
        }
        if (patcherDefs.contains("parallaxtocm")) {
            meshPatchers.shaderTransformPatchers[PatcherMeshShaderTransformParallaxToCM::getFromShader()]
                = { PatcherMeshShaderTransformParallaxToCM::getToShader(),
                      PatcherMeshShaderTransformParallaxToCM::getFactory() };

            PatcherTextureHookConvertToCM::initShader();
        }
        if (patcherDefs.contains("particlelightstolp")) {
            meshPatchers.globalPatchers.emplace_back(PatcherMeshGlobalParticleLightsToLP::getFactory());
        }
        if (patcherDefs.contains("fixeffectlightingcs")) {
            meshPatchers.globalPatchers.emplace_back(PatcherMeshGlobalFixEffectLightingCS::getFactory());
        }

        if (patcherDefs.contains("restoredefaultshaders")) {
            meshPatchers.postPatchers.emplace_back(PatcherMeshPostRestoreDefaultShaders::getFactory());
        }
        if (patcherDefs.contains("fixsss")) {
            meshPatchers.postPatchers.emplace_back(PatcherMeshPostFixSSS::getFactory());

            PatcherTextureHookFixSSS::initShader();
        }
        if (patcherDefs.contains("hairflowmap")) {
            meshPatchers.postPatchers.emplace_back(PatcherMeshPostHairFlowMap::getFactory());
        }

        PatcherUtil::PatcherTextureSet texPatchers;
        if (patcherDefs.contains("converttohdr")) {
            PatcherTextureGlobalConvertToHDR::initShader();

            texPatchers.globalPatchers.emplace_back(PatcherTextureGlobalConvertToHDR::getFactory());
            PatcherTextureGlobalConvertToHDR::loadOptions(patcherDefs["converttohdr"]);
        }

        ParallaxGen::loadPatchers(meshPatchers, texPatchers);
        ParallaxGen::patchMeshes(args.multithreading, false);
        ParallaxGen::patchTextures(args.multithreading);

        // Finalize step
        if (patcherDefs.contains("particlelightstolp")) {
            PatcherMeshGlobalParticleLightsToLP::finalize();
        }

        // Check if dynamic cubemap file is needed
        if (args.Patch.patchers.contains("complexmaterial")) {
            // Install default cubemap file if needed
            static const filesystem::path dynCubeMapPath = "textures/cubemaps/dynamic1pxcubemap_black.dds";

            spdlog::info("Installing default dynamic cubemap file");

            // Create Directory
            const filesystem::path outputCubemapPath = args.Patch.output / dynCubeMapPath.parent_path();
            filesystem::create_directories(outputCubemapPath);

            const filesystem::path assetPath = filesystem::path(exePath) / "assets/dynamic1pxcubemap_black_ENB.dds";
            const filesystem::path outputPath = filesystem::path(args.Patch.output) / dynCubeMapPath;

            // Move File
            filesystem::copy_file(assetPath, outputPath, filesystem::copy_options::overwrite_existing);
        }

        const auto endTime = chrono::high_resolution_clock::now();
        timeTaken += chrono::duration_cast<chrono::seconds>(endTime - startTime).count();

        spdlog::info("ParallaxGen took {} seconds to complete", timeTaken);
    }
}

void addArguments(CLI::App& app, PGToolsCLIArgs& args)
{
    // Logging
    app.add_flag("-v", args.verbosity,
        "Verbosity level -v for DEBUG data or -vv for TRACE data "
        "(warning: TRACE data is very verbose)");
    app.add_flag("--no-multithreading", args.multithreading, "Disable multithreading");

    args.Patch.subCommand = app.add_subcommand("patch", "Patch meshes");
    args.Patch.subCommand->add_option("patcher", args.Patch.patchers, "List of patchers to use")
        ->required()
        ->delimiter(',');
    args.Patch.subCommand->add_option("source", args.Patch.source, "Source directory")->default_str("");
    args.Patch.subCommand->add_option("output", args.Patch.output, "Output directory")
        ->default_str("ParallaxGen_Output");
    args.Patch.subCommand->add_flag("--high-mem", args.Patch.highMem, "High memory usage mode (default: false)");
}
}

auto main(int argC, char** argV) -> int
{
// Block until enter only in debug mode
#ifdef _DEBUG
    cout << "Press ENTER to start (DEBUG mode)...";
    cin.get();
#endif

    SetConsoleOutputCP(CP_UTF8);

    // CLI Arguments
    PGToolsCLIArgs args;
    CLI::App app { "PGTools: A collection of tools for ParallaxGen" };
    addArguments(app, args);

    // Parse CLI Arguments (this is what exits on any validation issues)
    CLI11_PARSE(app, argC, argV);

    // Initialize Logger
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    // Set logging mode
    if (args.verbosity >= 1) {
        spdlog::set_level(spdlog::level::debug);
        spdlog::debug("DEBUG logging enabled");
    }

    if (args.verbosity >= 2) {
        spdlog::set_level(spdlog::level::trace);
        spdlog::trace("TRACE logging enabled");
    }

    // Main Runner (Catches all exceptions)
    CPPTRACE_TRY
    {
        mainRunner(args);
        return 0;
    }
    CPPTRACE_CATCH(const exception& e)
    {
        ExceptionHandler::setException(e, cpptrace::from_current_exception().to_string());
    }

    ExceptionHandler::throwExceptionOnMainThread();
    return 1;
}
