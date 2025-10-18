#include "BethesdaGame.hpp"
#include "GUI/CompletionDialog.hpp"
#include "GUI/WXLoggerSink.hpp"
#include "ModManagerDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGPatcherGlobals.hpp"
#include "ParallaxGen.hpp"
#include "ParallaxGenConfig.hpp"
#include "ParallaxGenD3D.hpp"
#include "ParallaxGenDirectory.hpp"
#include "ParallaxGenHandlers.hpp"
#include "ParallaxGenPlugin.hpp"
#include "ParallaxGenRunner.hpp"
#include "ParallaxGenUI.hpp"
#include "ParallaxGenWarnings.hpp"
#include "patchers/PatcherMeshGlobalFixEffectLightingCS.hpp"
#include "patchers/PatcherMeshPostFixSSS.hpp"
#include "patchers/PatcherMeshPostHairFlowMap.hpp"
#include "patchers/PatcherMeshPostRestoreDefaultShaders.hpp"
#include "patchers/PatcherMeshPreDisableMLP.hpp"
#include "patchers/PatcherMeshPreFixMeshLighting.hpp"
#include "patchers/PatcherMeshPreFixTextureSlotCount.hpp"
#include "patchers/PatcherMeshShaderComplexMaterial.hpp"
#include "patchers/PatcherMeshShaderDefault.hpp"
#include "patchers/PatcherMeshShaderTransformParallaxToCM.hpp"
#include "patchers/PatcherMeshShaderTruePBR.hpp"
#include "patchers/PatcherMeshShaderVanillaParallax.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "patchers/base/Patcher.hpp"
#include "patchers/base/PatcherUtil.hpp"
#include "util/Logger.hpp"
#include "util/ParallaxGenUtil.hpp"
#include "util/TaskQueue.hpp"

#include <CLI/CLI.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <cpptrace/from_current.hpp>
#include <miniz.h>
#include <miniz_zip.h>
#include <nlohmann/json_fwd.hpp>
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

constexpr unsigned MAX_LOG_SIZE = 10490000; // 10 MB
constexpr unsigned MAX_LOG_FILES = 1000;

using namespace std;
struct ParallaxGenCLIArgs {
    bool autostart = false;
    bool highmem = false;
};

namespace {

void addFileToZip(mz_zip_archive& zip, const filesystem::path& filePath, const filesystem::path& zipPath)
{
    // ignore Zip file itself
    if (filePath == zipPath) {
        return;
    }

    vector<std::byte> buffer = ParallaxGenUtil::getFileBytes(filePath);

    const filesystem::path relativePath = filePath.lexically_relative(PGGlobals::getPGD()->getGeneratedPath());
    const string relativeFilePathUTF8 = ParallaxGenUtil::utf16toUTF8(relativePath.wstring());

    // add file to Zip
    if (mz_zip_writer_add_mem(&zip, relativeFilePathUTF8.c_str(), buffer.data(), buffer.size(), MZ_NO_COMPRESSION)
        == 0) {
        spdlog::error(L"Error adding file to zip: {}", filePath.wstring());
        exit(1);
    }
}

void zipDirectory(const filesystem::path& dirPath, const filesystem::path& zipPath)
{
    mz_zip_archive zip;

    // init to 0
    memset(&zip, 0, sizeof(zip));

    // Check if file already exists and delete
    if (filesystem::exists(zipPath)) {
        Logger::info(L"Deleting existing output Zip file: {}", zipPath.wstring());
        filesystem::remove(zipPath);
    }

    // initialize file
    const string zipPathString = ParallaxGenUtil::utf16toUTF8(zipPath);
    if (mz_zip_writer_init_file(&zip, zipPathString.c_str(), 0) == 0) {
        Logger::critical(L"Error creating Zip file: {}", zipPath.wstring());
        return;
    }

    // add each file in directory to Zip
    for (const auto& entry : filesystem::recursive_directory_iterator(dirPath)) {
        if (filesystem::is_regular_file(entry.path())) {
            addFileToZip(zip, entry.path(), zipPath);
        }
    }

    // finalize Zip
    if (mz_zip_writer_finalize_archive(&zip) == 0) {
        Logger::critical(L"Error finalizing Zip archive: {}", zipPath.wstring());
        return;
    }

    mz_zip_writer_end(&zip);
}

auto deployAssets(const filesystem::path& outputDir, const filesystem::path& exePath) -> void
{
    Logger::info("Installing default dynamic cubemap file");

    // Create Directory
    const filesystem::path outputCubemapPath
        = outputDir / PatcherMeshShaderComplexMaterial::s_DYNCUBEMAPPATH.parent_path();
    filesystem::create_directories(outputCubemapPath);

    const filesystem::path assetPath = filesystem::path(exePath) / "assets/dynamic1pxcubemap_black.dds";
    const filesystem::path outputPath
        = filesystem::path(outputDir) / PatcherMeshShaderComplexMaterial::s_DYNCUBEMAPPATH;

    // Move File
    filesystem::copy_file(assetPath, outputPath, filesystem::copy_options::overwrite_existing);

    // Add any files to ignore as generated files
    PGGlobals::getPGD()->addGeneratedFile(PatcherMeshShaderComplexMaterial::s_DYNCUBEMAPPATH);
}

void initLogger(const filesystem::path& logpath, bool enableDebug = false, bool enableTrace = false)
{
    // delete old logs
    if (filesystem::exists(logpath.parent_path())) {
        try {
            // Only delete files that are .log and start with ParallaxGen
            for (const auto& entry : filesystem::directory_iterator(logpath.parent_path())) {
                if (entry.is_regular_file() && entry.path().extension() == ".log"
                    && entry.path().filename().wstring().starts_with(L"PGPatcher")) {
                    filesystem::remove(entry.path());
                }
            }
        } catch (const filesystem::filesystem_error& e) {
            cerr << "Failed to delete old logs: " << e.what() << "\n";
        }
    }

    // Create loggers
    vector<spdlog::sink_ptr> sinks;
    auto consoleSink = make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sinks.push_back(consoleSink);

    // Rotating file sink
    auto fileSink = make_shared<spdlog::sinks::rotating_file_sink_mt>(logpath.wstring(), MAX_LOG_SIZE, MAX_LOG_FILES);
    sinks.push_back(fileSink);

    // Messagebox sink
    auto wxSink = std::make_shared<WXLoggerSink<std::mutex>>();
    sinks.push_back(wxSink);

    auto logger = make_shared<spdlog::logger>("PG", sinks.begin(), sinks.end());

    // register logger parameters
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    wxSink->set_formatter(std::make_unique<spdlog::pattern_formatter>("%v"));

    if (enableDebug) {
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::debug);
        Logger::debug("DEBUG logging enabled");
    }
    if (enableTrace) {
        spdlog::set_level(spdlog::level::trace);
        consoleSink->set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::trace);
        Logger::trace("TRACE logging enabled");
    }
}

void mainRunner(ParallaxGenCLIArgs& args, const filesystem::path& exePath)
{
    // Define paths
    PGPatcherGlobals::setEXEPath(exePath);
    const filesystem::path cfgDir = exePath / "cfg";

    // Create cfg directory if it does not exist
    if (!filesystem::exists(cfgDir)) {
        filesystem::create_directories(cfgDir);
    }

    // Initialize ParallaxGenConfig
    ParallaxGenConfig::loadStatics(exePath);
    auto pgc = ParallaxGenConfig();
    pgc.loadConfig();

    PGPatcherGlobals::setPGC(&pgc);

    // Initialize UI
    ParallaxGenUI::init();

    auto params = pgc.getParams();

    // Show launcher UI
    if (!args.autostart) {
        ParallaxGenUI::showLauncher(pgc, params);
    }

    // Validate config
    vector<string> errors;
    if (!ParallaxGenConfig::validateParams(params, errors)) {
        // This should never happen because there is a frontend validation that would have to be bypassed
        string errorList;
        for (const auto& error : errors) {
            errorList += "- " + error + "\n";
        }
        cerr << "Configuration is invalid:\n" << errorList << "\n";
        return;
    }

    // LOGGING SHOULD ONLY HAPPEN PAST THIS POINT
    const filesystem::path logPath = exePath / "log" / "PGPatcher.log";
    initLogger(logPath, params.Processing.enableDebugLogging, params.Processing.enableTraceLogging);

    // Welcome Message
    Logger::info("Welcome to PGPatcher version {}!", PG_VERSION);

#if PG_TEST_BUILD
    // Post test message for test builds
    Logger::warn("This is an EXPERIMENTAL development build of PGPatcher");
#endif

    // Alpha message
    Logger::warn("PGPatcher is currently in BETA. Please file detailed bug reports on nexus or github.");

    // print output location
    Logger::info(L"PGPatcher output directory: {}", params.Output.dir.wstring());

    // Create relevant objects
    // TODO control the lifetime of these in PGLib
    auto bg = BethesdaGame(params.Game.type, params.Game.dir);

    auto mmd = ModManagerDirectory(params.ModManager.type);
    PGGlobals::setMMD(&mmd);
    auto pgd = ParallaxGenDirectory(&bg, params.Output.dir);
    PGGlobals::setPGD(&pgd);
    auto pgd3d = ParallaxGenD3D(exePath / "cshaders");
    PGGlobals::setPGD3D(&pgd3d);

    Patcher::loadStatics(pgd, pgd3d);

    // Check if GPU needs to be initialized
    Logger::info("Initializing GPU");
    if (!pgd3d.initGPU()) {
        Logger::critical("Failed to initialize GPU. Exiting.");
        return;
    }

    if (!pgd3d.initShaders()) {
        Logger::critical("Failed to initialize internal shaders. Exiting.");
        return;
    }

    //
    // Generation
    //

    // Get current time to compare later
    auto startTime = chrono::high_resolution_clock::now();
    long long timeTaken = 0;

    // Create output directory
    try {
        if (filesystem::create_directories(params.Output.dir)) {
            Logger::debug(L"Output directory created: {}", params.Output.dir.wstring());
        }
    } catch (const filesystem::filesystem_error& e) {
        Logger::critical("Failed to create output directory: {}", e.what());
        return;
    }

    // If output dir is the same as data dir meshes might get overwritten
    if (filesystem::equivalent(params.Output.dir, pgd.getDataPath())) {
        Logger::critical("Output directory cannot be the same directory as your data folder. "
                         "Exiting.");
        return;
    }

    // If output dir is a subdirectory of data dir vfs issues can occur
    if (boost::istarts_with(params.Output.dir.wstring(), bg.getGameDataPath().wstring() + "\\")) {
        Logger::critical("Output directory cannot be a subdirectory of your data folder. Exiting.");
        return;
    }

    // Check if dyndolod.esp exists
    const auto activePlugins = bg.getActivePlugins(false, true);
    if (ranges::find(activePlugins, L"dyndolod.esp") != activePlugins.end()) {
        Logger::critical(
            "DynDoLOD and TexGen outputs must be disabled prior to running PGPatcher. It is recommended to "
            "generate LODs after running PGPatcher with the PGPatcher output enabled.");
        return;
    }

    // Log active plugins
    const wstring loadOrderStr = boost::algorithm::join(activePlugins, L",");
    Logger::debug(L"Active Plugin Load Order: {}", loadOrderStr);

    TaskQueue pluginInit;

    // Init PGP library
    if (params.Processing.pluginPatching) {
        Logger::info("Initializing plugin patching");
        pluginInit.queueTask([&bg, &exePath, &params]() -> void {
            ParallaxGenPlugin::initialize(bg, exePath, params.Processing.pluginLang);
            ParallaxGenPlugin::populateObjs(params.Output.dir / "PGPatcher.esp");
        });
    }

    // Populate mod info
    nlohmann::json modJSON;
    const auto modListFile = cfgDir / "modrules.json";
    if (ParallaxGenUtil::getJSON(modListFile, modJSON)) {
        mmd.loadJSON(modJSON);
    }

    TaskQueue modManagerInit;

    if (params.ModManager.type == ModManagerDirectory::ModManagerType::MODORGANIZER2
        && !params.ModManager.mo2InstanceDir.empty()) {
        // Make sure running is USVFS
        if (!ParallaxGenHandlers::isUnderUSVFS()) {
            Logger::critical("Please verify that you are launching PGPatcher from MO2, VFS not detected.");
            return;
        }

        // MO2
        modManagerInit.queueTask([&mmd, &params]() -> void {
            mmd.populateModFileMapMO2(params.ModManager.mo2InstanceDir, params.Output.dir);
        });
    } else if (params.ModManager.type == ModManagerDirectory::ModManagerType::VORTEX) {
        // Vortex
        modManagerInit.queueTask([&mmd, &bg]() -> void { mmd.populateModFileMapVortex(bg.getGameDataPath()); });
    }

    // Check if ParallaxGen output already exists in data directory
    const filesystem::path pgStateFilePath = bg.getGameDataPath() / "ParallaxGen_Diff.json";
    if (filesystem::exists(pgStateFilePath)) {
        Logger::critical("ParallaxGen meshes exist in your data directory, please delete before "
                         "re-running.");
        return;
    }

    // delete existing output
    ParallaxGen::deleteOutputDir();

    // Init file map
    pgd.populateFileMap(params.Processing.bsa);

    // Plugins requires for map files
    pluginInit.waitForCompletion();
    pluginInit.shutdown();

    // Map files
    pgd.mapFiles(params.MeshRules.blockList, params.MeshRules.allowList, params.TextureRules.textureMaps,
        params.TextureRules.vanillaBSAList, params.Processing.pluginPatching, params.Processing.multithread,
        args.highmem);

    // Wait for threads
    modManagerInit.waitForCompletion();
    modManagerInit.shutdown();

    // Create patcher factory
    PatcherUtil::PatcherMeshSet meshPatchers;
    if (params.PrePatcher.disableMLP) {
        Logger::debug("Adding Disable MLP pre-patcher");
        meshPatchers.prePatchers.emplace_back(PatcherMeshPreDisableMLP::getFactory());
    }
    if (params.PrePatcher.fixMeshLighting) {
        Logger::debug("Adding Mesh Lighting Fix pre-patcher");
        meshPatchers.prePatchers.emplace_back(PatcherMeshPreFixMeshLighting::getFactory());
    }
    if (params.ShaderPatcher.parallax || params.ShaderPatcher.complexMaterial || params.ShaderPatcher.truePBR) {
        // fix slots only needed for shader patchers
        Logger::debug("Adding Texture Slot Count Fix pre-patcher");
        meshPatchers.prePatchers.emplace_back(PatcherMeshPreFixTextureSlotCount::getFactory());
    }

    bool conflictPatcherEnabled = false;
    meshPatchers.shaderPatchers.emplace(
        PatcherMeshShaderDefault::getShaderType(), PatcherMeshShaderDefault::getFactory());
    if (params.ShaderPatcher.parallax) {
        Logger::debug("Adding Parallax shader patcher");
        meshPatchers.shaderPatchers.emplace(
            PatcherMeshShaderVanillaParallax::getShaderType(), PatcherMeshShaderVanillaParallax::getFactory());
        conflictPatcherEnabled = true;
    }
    if (params.ShaderPatcher.complexMaterial) {
        Logger::debug("Adding Complex Material shader patcher");
        meshPatchers.shaderPatchers.emplace(
            PatcherMeshShaderComplexMaterial::getShaderType(), PatcherMeshShaderComplexMaterial::getFactory());
        PatcherMeshShaderComplexMaterial::loadStatics(
            params.ShaderPatcher.ShaderPatcherComplexMaterial.listsDyncubemapBlocklist);
        conflictPatcherEnabled = true;
    }
    if (params.ShaderPatcher.truePBR) {
        Logger::debug("Adding True PBR shader patcher");
        meshPatchers.shaderPatchers.emplace(
            PatcherMeshShaderTruePBR::getShaderType(), PatcherMeshShaderTruePBR::getFactory());
        PatcherMeshShaderTruePBR::loadOptions(params.ShaderPatcher.ShaderPatcherTruePBR.checkPaths,
            params.ShaderPatcher.ShaderPatcherTruePBR.printNonExistentPaths);
        PatcherMeshShaderTruePBR::loadStatics(pgd.getPBRJSONs());
        conflictPatcherEnabled = true;
    }
    if (params.ShaderTransforms.parallaxToCM) {
        Logger::debug("Adding Parallax to Complex Material shader transform patcher");
        meshPatchers.shaderTransformPatchers[PatcherMeshShaderTransformParallaxToCM::getFromShader()]
            = { PatcherMeshShaderTransformParallaxToCM::getToShader(),
                  PatcherMeshShaderTransformParallaxToCM::getFactory() };
        PatcherMeshShaderTransformParallaxToCM::loadOptions(
            params.ShaderTransforms.ShaderTransformParallaxToCM.onlyWhenRequired);

        // initialize patcher hooks
        if (!PatcherTextureHookConvertToCM::initShader()) {
            Logger::critical("Failed to initialize ConvertToCM shader");
            return;
        }
    }
    if (params.PostPatcher.disablePrePatchedMaterials) {
        Logger::debug("Adding Disable Pre-Patched Materials post-patcher");
        meshPatchers.postPatchers.emplace_back(PatcherMeshPostRestoreDefaultShaders::getFactory());
    }
    if (params.PostPatcher.fixSSS) {
        Logger::debug("Adding SSS fix post-patcher");
        meshPatchers.postPatchers.emplace_back(PatcherMeshPostFixSSS::getFactory());

        if (!PatcherTextureHookFixSSS::initShader()) {
            Logger::critical("Failed to initialize FixSSS shader");
            return;
        }
    }
    if (params.PostPatcher.hairFlowMap) {
        Logger::debug("Adding Hair Flow Map post-patcher");
        meshPatchers.postPatchers.emplace_back(PatcherMeshPostHairFlowMap::getFactory());
    }

    if (params.GlobalPatcher.fixEffectLightingCS) {
        Logger::debug("Adding Effect Lighting CS Fix pre-patcher");
        meshPatchers.globalPatchers.emplace_back(PatcherMeshGlobalFixEffectLightingCS::getFactory());
    }

    const PatcherUtil::PatcherTextureSet texPatchers;
    ParallaxGen::loadPatchers(meshPatchers, texPatchers);

    // Check if MO2 is used and MO2 use order is checked
    if (conflictPatcherEnabled && params.ModManager.type != ModManagerDirectory::ModManagerType::NONE) {
        // Find conflicts
        ParallaxGen::populateModData(params.Processing.multithread, params.Processing.pluginPatching);

        // Assign new mod priorities for new mods
        mmd.assignNewModPriorities();

        // pause timer for UI
        timeTaken += chrono::duration_cast<chrono::seconds>(chrono::high_resolution_clock::now() - startTime).count();

        // Select mod order
        Logger::info("Showing mod priority order dialog");
        ParallaxGenUI::selectModOrder();
        startTime = chrono::high_resolution_clock::now();
    }

    // Patch meshes if set
    ParallaxGenWarnings::init();
    ParallaxGen::patch(params.Processing.multithread, params.Processing.pluginPatching);

    // Write plugin
    if (params.Processing.pluginPatching) {
        Logger::info("Saving Plugins");
        ParallaxGenPlugin::savePlugin(params.Output.dir, params.Processing.pluginESMify);
    }

    // Check for empty output
    bool outputEmpty = false;
    if (ParallaxGen::isOutputEmpty()) {
        // output is empty
        Logger::warn("Output directory is empty. No files were generated. Is your game path set correctly?");
        outputEmpty = true;
    }

    if (!outputEmpty) {
        // Deploy Assets
        deployAssets(params.Output.dir, exePath);

        // Save diff json
        const auto diffJSON = ParallaxGen::getDiffJSON();
        if (!diffJSON.empty()) {
            const filesystem::path diffJSONPath = params.Output.dir / "ParallaxGen_Diff.json";
            ParallaxGenUtil::saveJSON(diffJSONPath, diffJSON, true);

            pgd.addGeneratedFile("ParallaxGen_Diff.json");
        }

        // archive
        if (params.Output.zip) {
            // create zip file
            const auto zipPath = params.Output.dir / "PGPatcher_Output.zip";
            zipDirectory(params.Output.dir, zipPath);
            ParallaxGen::deleteOutputDir(false);
        }
    }

    const auto endTime = chrono::high_resolution_clock::now();
    timeTaken += chrono::duration_cast<chrono::seconds>(endTime - startTime).count();

    Logger::info("PGPatcher took {} seconds to complete (does not include time in user interface)", timeTaken);

    // Show completion dialog
    CompletionDialog dlg(timeTaken);
    dlg.ShowModal();
}

void addArguments(CLI::App& app, ParallaxGenCLIArgs& args)
{
    // Logging
    app.add_flag("--autostart", args.autostart, "Start generation without user input");
    app.add_flag("--highmem", args.highmem, "Enable high memory mode");
}
}

auto main(int argC, char** argV) -> int
{
// Block until enter only in debug mode
#ifdef _DEBUG
    cout << "Press ENTER to start (DEBUG mode)...";
    cin.get();
#endif

    SetUnhandledExceptionFilter(ParallaxGenHandlers::customExceptionHandler);

    SetConsoleOutputCP(CP_UTF8);

    // Find location of ParallaxGen.exe
    const filesystem::path exePath = ParallaxGenHandlers::getExePath().parent_path();

    // CLI Arguments
    ParallaxGenCLIArgs args;
    CLI::App app { "PGPatcher" };
    addArguments(app, args);

    // Parse CLI Arguments (this is what exits on any validation issues)
    CLI11_PARSE(app, argC, argV);

    // Main Runner (Catches all exceptions)
    CPPTRACE_TRY { mainRunner(args, exePath); }
    CPPTRACE_CATCH(const exception& e)
    {
        ParallaxGenRunner::processException(e, cpptrace::from_current_exception().to_string());

        cout << "Press ENTER to abort...";
        cin.get();
        abort();
    }

    return 0;
}
