#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGPatcher.hpp"
#include "patchers/PatcherMeshGlobalParticleLightsToLP.hpp"
#include "patchers/PatcherMeshPostFixSSS.hpp"
#include "patchers/PatcherMeshPostHairFlowMap.hpp"
#include "patchers/PatcherMeshPostRestoreDefaultShaders.hpp"
#include "patchers/PatcherMeshPreFixMeshLighting.hpp"
#include "patchers/PatcherMeshPreFixTextureSlotCount.hpp"
#include "patchers/PatcherMeshShaderComplexMaterial.hpp"
#include "patchers/PatcherMeshShaderTransformParallaxToCM.hpp"
#include "patchers/PatcherMeshShaderTruePBR.hpp"
#include "patchers/PatcherMeshShaderVanillaParallax.hpp"
#include "patchers/PatcherTextureGlobalConvertToHDR.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
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

namespace {
std::filesystem::path executablePath()
{
    std::array<wchar_t, MAX_PATH> buffer { };
    if (!GetModuleFileNameW(nullptr, buffer.data(), MAX_PATH)) {
        std::cerr << "Error getting executable path: " << GetLastError() << "\n";
        exit(1);
    }

    std::filesystem::path outPath = std::filesystem::path(buffer.data());

    if (std::filesystem::exists(outPath))
        return outPath;

    std::cerr << "Error getting executable path: path does not exist\n";
    exit(1);

    return { };
}

void configureDotnetLibDirectory(const std::filesystem::path& exeDir)
{
    const auto libDir = exeDir / "dotnetlib";
    if (!std::filesystem::exists(libDir))
        return;

    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        std::cerr << "Failed to configure DLL search directories.\n";
        exit(1);
    }

    if (!AddDllDirectory(libDir.c_str())) {
        std::cerr << "Failed to add dotnetlib directory to DLL search path.\n";
        exit(1);
    }
}

struct PGToolsCLIArgs {
    int verbosity = 0;
    bool multithreading = true;
    bool shortcut = false;

    struct Patch {
        CLI::App* subCommand = nullptr;
        std::unordered_set<std::string> patchers;
        std::filesystem::path source = ".";
        std::filesystem::path output = "ParallaxGen_Output";
        bool mapTexturesFromMeshes = false;
        bool highMem = false;
    } patch;
};

void mainRunner(PGToolsCLIArgs& args)
{
    // Welcome Message.
    spdlog::info("Welcome to PGTools version {}!", PG_FULL_VERSION);

    // Get EXE path.
    const auto exePath = executablePath().parent_path();

#if defined(PG_PRERELEASE) && (PG_PRERELEASE > 0)
    // Post test message for test builds.
    spdlog::warn("This is an EXPERIMENTAL pre-release build of PGTools");
#endif

    ExceptionHandler::setMainThread();

    // Check if patch subcommand was used.
    if (args.patch.subCommand->parsed()) {
        // Get current time to compare later.
        const auto startTime = std::chrono::high_resolution_clock::now();
        long long timeTaken = 0;

        args.patch.source = std::filesystem::absolute(args.patch.source);
        args.patch.output = std::filesystem::absolute(args.patch.output);

        auto pgd = PGDirectory(args.patch.source, args.patch.output);
        PGGlobals::setPGD(&pgd);
        auto pgd3D = PGD3D(exePath / "cshaders");
        PGGlobals::setPGD3D(&pgd3D);

        // Check if GPU needs to be initialized.
        if (!pgd3D.initGPU())
            spdlog::critical("Failed to initialize GPU. Exiting.");

        if (!pgd3D.initShaders())
            spdlog::critical("Failed to initialize internal shaders. Exiting.");

        // Create output directory.
        try {
            std::filesystem::create_directories(args.patch.output);
        } catch (const std::filesystem::filesystem_error& e) {
            spdlog::critical("Failed to create output directory: {}", e.what());
            exit(1);
        }

        // If output dir is the same as data dir meshes might get overwritten.
        if (std::filesystem::equivalent(args.patch.output, pgd.dataPath())) {
            spdlog::critical("Output directory cannot be the same directory as your data folder. "
                             "Exiting.");
            exit(1);
        }

        // Delete existing output.
        PGPatcher::deleteOutputDir();

        // Init file map.
        pgd.populateFileMap(false);

        // Map files.
        pgd.mapFiles({ }, { }, { }, { }, args.multithreading);

        // Split patchers into names and options.
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> patcherDefs;
        for (const auto& patcher : args.patch.patchers) {
            const auto openBracket = patcher.find('[');
            const auto closeBracket = patcher.find(']');
            if (openBracket == std::string::npos || closeBracket == std::string::npos) {
                patcherDefs[patcher] = { };
                continue;
            }

            // Get substring between brackets.
            auto options = patcher.substr(openBracket + 1, closeBracket - openBracket - 1);
            // Split options by | into unordered set.
            std::unordered_map<std::string, std::string> optionSet;
            for (const auto& option : options | std::views::split('|')) {
                // Check if = in option string.
                const auto optionStr = std::string(option.begin(), option.end());
                const auto eqPos = optionStr.find('=');
                if (eqPos != std::string::npos) {
                    optionSet[optionStr.substr(0, eqPos)] = optionStr.substr(eqPos + 1);
                    continue;
                }

                optionSet[optionStr] = "";
            }

            // Add to set.
            patcherDefs[patcher.substr(0, openBracket)] = optionSet;
        }

        // Create patcher factory.
        PatcherUtil::PatcherMeshSet meshPatchers;
        if (patcherDefs.contains("fixmeshlighting"))
            meshPatchers.prePatchers.emplace_back(PatcherMeshPreFixMeshLighting::factory());
        if (patcherDefs.contains("fixtextureslotcount"))
            meshPatchers.prePatchers.emplace_back(PatcherMeshPreFixTextureSlotCount::factory());
        if (patcherDefs.contains("parallax")) {
            meshPatchers.shaderPatchers.emplace(PatcherMeshShaderVanillaParallax::shaderType(),
                                                PatcherMeshShaderVanillaParallax::factory());
        }
        if (patcherDefs.contains("complexmaterial")) {
            meshPatchers.shaderPatchers.emplace(PatcherMeshShaderComplexMaterial::shaderType(),
                                                PatcherMeshShaderComplexMaterial::factory());
            PatcherMeshShaderComplexMaterial::loadOptions(patcherDefs["complexmaterial"]);
        }
        if (patcherDefs.contains("truepbr")) {
            meshPatchers.shaderPatchers.emplace(PatcherMeshShaderTruePBR::shaderType(),
                                                PatcherMeshShaderTruePBR::factory());
            PatcherMeshShaderTruePBR::loadStatics(pgd.pbrjsoNs());
            PatcherMeshShaderTruePBR::loadOptions(patcherDefs["truepbr"]);
        }
        if (patcherDefs.contains("parallaxtocm")) {
            meshPatchers.shaderTransformPatchers[PatcherMeshShaderTransformParallaxToCM::fromShader()]
                = { PatcherMeshShaderTransformParallaxToCM::toShader(),
                    PatcherMeshShaderTransformParallaxToCM::factory() };

            PatcherTextureHookConvertToCM::initShader();
        }
        if (patcherDefs.contains("particlelightstolp"))
            meshPatchers.globalPatchers.emplace_back(PatcherMeshGlobalParticleLightsToLP::factory());

        if (patcherDefs.contains("restoredefaultshaders"))
            meshPatchers.postPatchers.emplace_back(PatcherMeshPostRestoreDefaultShaders::factory());
        if (patcherDefs.contains("fixsss")) {
            meshPatchers.postPatchers.emplace_back(PatcherMeshPostFixSSS::factory());

            PatcherTextureHookFixSSS::initShader();
        }
        if (patcherDefs.contains("hairflowmap"))
            meshPatchers.postPatchers.emplace_back(PatcherMeshPostHairFlowMap::factory());

        PatcherUtil::PatcherTextureSet texPatchers;
        if (patcherDefs.contains("converttohdr")) {
            PatcherTextureGlobalConvertToHDR::initShader();

            texPatchers.globalPatchers.emplace_back(PatcherTextureGlobalConvertToHDR::factory());
            PatcherTextureGlobalConvertToHDR::loadOptions(patcherDefs["converttohdr"]);
        }

        PGPatcher::loadPatchers(meshPatchers, texPatchers);
        PGPatcher::patchMeshes(args.multithreading, true);
        PGPatcher::patchTextures(args.multithreading);

        // Finalize step.
        if (patcherDefs.contains("particlelightstolp"))
            PatcherMeshGlobalParticleLightsToLP::finalize();

        // Check if dynamic cubemap file is needed.
        if (args.patch.patchers.contains("complexmaterial")
            && !patcherDefs["complexmaterial"].contains("disable_dyncubemap")) {
            // Install default cubemap file if needed.
            static const std::filesystem::path dynCubeMapPath = "textures/cubemaps/dynamic1pxcubemap_black.dds";

            spdlog::info("Installing default dynamic cubemap file");

            // Create Directory.
            const std::filesystem::path outputCubemapPath = args.patch.output / dynCubeMapPath.parent_path();
            std::filesystem::create_directories(outputCubemapPath);

            const std::filesystem::path assetPath
                = std::filesystem::path(exePath) / "assets/dynamic1pxcubemap_black.dds";
            const std::filesystem::path outputPath = std::filesystem::path(args.patch.output) / dynCubeMapPath;

            // Move File.
            std::filesystem::copy_file(assetPath, outputPath, std::filesystem::copy_options::overwrite_existing);
        }

        const auto endTime = std::chrono::high_resolution_clock::now();
        timeTaken += std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();

        spdlog::info("PGPatcher took {} seconds to complete", timeTaken);
    }
}

void addArguments(CLI::App& app,
                  PGToolsCLIArgs& args)
{
    // Logging.
    app.add_flag("-v",
                 args.verbosity,
                 "Verbosity level -v for DEBUG data or -vv for TRACE data "
                 "(warning: TRACE data is very verbose)");
    app.add_flag("--no-multithreading{false}", args.multithreading, "Disable multithreading");
    app.add_flag("--shortcut",
                 args.shortcut,
                 "Keep pgtools running at the end (useful if you are running not in a terminal directly)");

    args.patch.subCommand = app.add_subcommand("patch", "Patch meshes");
    args.patch.subCommand->add_option("patcher", args.patch.patchers, "List of patchers to use")
        ->required()
        ->delimiter(',');
    args.patch.subCommand->add_option("source", args.patch.source, "Source directory")->default_str("");
    args.patch.subCommand->add_option("output", args.patch.output, "Output directory")
        ->default_str("ParallaxGen_Output");
    args.patch.subCommand->add_flag("--high-mem", args.patch.highMem, "High memory usage mode (default: false)");
}
}

int main(int argC,
         char* const* argV)
{
// Block until enter only in debug mode.
#ifdef _DEBUG
    std::cout << "Press ENTER to start (DEBUG mode)...";
    std::cin.get();
#endif

    SetConsoleOutputCP(CP_UTF8);

    const auto exePath = executablePath().parent_path();
    configureDotnetLibDirectory(exePath);

    // CLI Arguments.
    PGToolsCLIArgs args;
    CLI::App app { "PGTools: A collection of tools for ParallaxGen" };
    addArguments(app, args);

    // Parse CLI Arguments (this is what exits on any validation issues).
    CLI11_PARSE(app, argC, argV);

    // Initialize Logger.
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    // Set logging mode.
    if (args.verbosity >= 1) {
        spdlog::set_level(spdlog::level::debug);
        spdlog::debug("DEBUG logging enabled");
    }

    if (args.verbosity >= 2) {
        spdlog::set_level(spdlog::level::trace);
        spdlog::trace("TRACE logging enabled");
    }

    // Main Runner (Catches all exceptions).
    CPPTRACE_TRY { mainRunner(args); }
    CPPTRACE_CATCH(const std::exception& e)
    {
        ExceptionHandler::setException(e, cpptrace::from_current_exception().to_string());
    }

    int returnCode = 0;
    if (ExceptionHandler::hasException()) {
        ExceptionHandler::throwExceptionOnMainThread();
        returnCode = 1;
    }

    if (args.shortcut) {
        std::cout << "Press ENTER to exit...";
        std::cin.get();
    }

    return returnCode;
}
