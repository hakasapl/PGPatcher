#include "GUI/CompletionDialog.hpp"
#include "GUI/ProgressWindow.hpp"
#include "GUI/WXLoggerSink.hpp"
#include "PGConfig.hpp"
#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGHandlers.hpp"
#include "PGLocale.hpp"
#include "PGModManager.hpp"
#include "PGPatcher.hpp"
#include "PGPatcherGlobals.hpp"
#include "PGPlugin.hpp"
#include "PGRunCache.hpp"
#include "PGUI.hpp"
#include "common/BethesdaGame.hpp"
#include "patchers/PatcherMeshPostFixSSS.hpp"
#include "patchers/PatcherMeshPostHairFlowMap.hpp"
#include "patchers/PatcherMeshPostRestoreDefaultShaders.hpp"
#include "patchers/PatcherMeshPreFixMeshLighting.hpp"
#include "patchers/PatcherMeshPreFixTextureSlotCount.hpp"
#include "patchers/PatcherMeshShaderComplexMaterial.hpp"
#include "patchers/PatcherMeshShaderDefault.hpp"
#include "patchers/PatcherMeshShaderTransformParallaxToCM.hpp"
#include "patchers/PatcherMeshShaderTruePBR.hpp"
#include "patchers/PatcherMeshShaderVanillaParallax.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "patchers/base/PatcherUtil.hpp"
#include "util/ExceptionHandler.hpp"
#include "util/FileUtil.hpp"
#include "util/HashUtil.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"
#include "util/TaskPoolRunner.hpp"
#include "util/TaskQueue.hpp"

#include <CLI/CLI.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <consoleapi.h>
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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <wx/colour.h>

constexpr unsigned maxLogSize = 10490000; // 10 MB
constexpr unsigned maxLogFiles = 1000;

namespace {

struct ParallaxGenCLIArgs {
    bool autostart = false;
    bool autostartUpdate = false;
    bool console = false;
    bool considerAllMeshes = false;
    bool ignoreMO2Check = false;
    bool disableDynCubemap = false;
    bool forceAlwaysCM = false;
    bool excludeFacegens = false;
    bool esmAll = false;
    bool noEsm = false;
};

void addFileToZip(mz_zip_archive& zip,
                  const std::filesystem::path& filePath,
                  const std::filesystem::path& zipPath)
{
    // Ignore Zip file itself.
    if (filePath == zipPath)
        return;

    std::vector<std::byte> buffer = FileUtil::getFileBytes(filePath);

    const std::filesystem::path relativePath = filePath.lexically_relative(PGGlobals::getPGD()->getGeneratedPath());

    // Build ZIP path directly with forward slashes.
    std::string relativeFilePathUTF8;
    bool first = true;
    for (const auto& part : relativePath) {
        if (!first)
            relativeFilePathUTF8 += '/';
        first = false;
        relativeFilePathUTF8 += StringUtil::utf16toUTF8(part.wstring());
    }

    // Add file to Zip.
    if (mz_zip_writer_add_mem(&zip, relativeFilePathUTF8.c_str(), buffer.data(), buffer.size(), MZ_NO_COMPRESSION)
        == 0) {
        spdlog::critical(L"Error creating output zip file");
        return;
    }
}

void zipDirectory(const std::filesystem::path& dirPath,
                  const std::filesystem::path& zipPath)
{
    mz_zip_archive zip;

    // Init to 0.
    memset(&zip, 0, sizeof(zip));

    // Check if file already exists and delete.
    if (std::filesystem::exists(zipPath)) {
        Logger::info(L"Deleting existing output Zip file: {}", zipPath.wstring());
        std::filesystem::remove(zipPath);
    }

    // Initialize file.
    const std::string zipPathString = StringUtil::utf16toUTF8(zipPath);
    if (mz_zip_writer_init_file(&zip, zipPathString.c_str(), 0) == 0) {
        Logger::critical(L"Error creating Zip file: {}", zipPath.wstring());
        return;
    }

    // Add each file in directory to Zip.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dirPath))
        if (std::filesystem::is_regular_file(entry.path()))
            addFileToZip(zip, entry.path(), zipPath);

    // Finalize Zip.
    if (mz_zip_writer_finalize_archive(&zip) == 0) {
        Logger::critical(L"Error finalizing Zip archive: {}", zipPath.wstring());
        return;
    }

    mz_zip_writer_end(&zip);
}

auto deployDynamicCubemapFile(const std::filesystem::path& outputDir,
                              const std::filesystem::path& exePath) -> void
{
    Logger::info("Installing default dynamic cubemap file");

    // Create Directory.
    const std::filesystem::path outputCubemapPath
        = outputDir / PatcherMeshShaderComplexMaterial::s_dynCubemapPath.parent_path();
    std::filesystem::create_directories(outputCubemapPath);

    const std::filesystem::path assetPath = std::filesystem::path(exePath) / "assets/dynamic1pxcubemap_black.dds";
    const std::filesystem::path outputPath
        = std::filesystem::path(outputDir) / PatcherMeshShaderComplexMaterial::s_dynCubemapPath;

    // Move File.
    std::filesystem::copy_file(assetPath, outputPath, std::filesystem::copy_options::overwrite_existing);

    // Add any files to ignore as generated files.
    PGGlobals::getPGD()->addGeneratedFile(PatcherMeshShaderComplexMaterial::s_dynCubemapPath);
}

void initLogger(const std::filesystem::path& logpath,
                bool enableDebug = false,
                bool enableTrace = false)
{
    // Delete old logs.
    if (std::filesystem::exists(logpath.parent_path())) {
        try {
            // Only delete files that are .log and start with ParallaxGen.
            for (const auto& entry : std::filesystem::directory_iterator(logpath.parent_path())) {
                if (entry.is_regular_file() && entry.path().extension() == ".log"
                    && entry.path().filename().wstring().starts_with(L"PGPatcher")) {
                    std::filesystem::remove(entry.path());
                }
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Failed to delete old logs: " << e.what() << "\n";
        }
    }

    // Create loggers.
    std::vector<spdlog::sink_ptr> sinks;
    const auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sinks.push_back(consoleSink);

    // Rotating file sink.
    const auto fileSink
        = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logpath.wstring(), maxLogSize, maxLogFiles);
    sinks.push_back(fileSink);

    // Messagebox sink.
    const auto wxSink = std::make_shared<WXLoggerSink<std::mutex>>();
    PGPatcherGlobals::setWXLoggerSink(wxSink);
    sinks.push_back(wxSink);

    const auto logger = std::make_shared<spdlog::logger>("PG", sinks.begin(), sinks.end());

    // Register logger parameters.
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

void configureDotNetLibDirectory(const std::filesystem::path& exeDir)
{
    const auto libDir = exeDir / "dotnetlib";
    if (!std::filesystem::exists(libDir))
        return;

    if (SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS) == 0) {
        std::cerr << "Failed to configure DLL search directories.\n";
        exit(1);
    }

    if (AddDllDirectory(libDir.c_str()) == nullptr) {
        std::cerr << "Failed to add dotnetlib directory to DLL search path.\n";
        exit(1);
    }
}

constexpr auto numPreparingSteps = 10;
constexpr auto numFinalizingSteps = 5;
constexpr auto numTotalSteps = 6;

/**
 * @brief Fingerprint of every run setting that influences what is written for a mesh.
 *
 * When this differs from the fingerprint stored in the update cache, every mesh is re-patched (the classification
 * caches, which do not depend on settings, are still reused). Settings that only influence plugin saving or logging
 * are excluded on purpose.
 */
auto computeConfigFingerprint(const PGConfig::PGParams& params,
                              const ParallaxGenCLIArgs& args) -> uint64_t
{
    HashUtil::Fnv1a64 hasher;

    hasher.add(std::string(PG_FULL_VERSION));

    hasher.add(params.game.type);
    hasher.add(StringUtil::toLowerASCII(params.game.dir.wstring()));

    hasher.add(params.modManager.type);
    hasher.add(StringUtil::toLowerASCII(params.modManager.mo2InstanceDir.wstring()));

    hasher.add(params.processing.enableModDevMode);

    std::vector<uint8_t> recTypes;
    for (const auto& recType : params.processing.allowedModelRecordTypes)
        recTypes.push_back(static_cast<uint8_t>(recType));
    std::ranges::sort(recTypes);
    hasher.add(static_cast<uint64_t>(recTypes.size()));
    for (const auto& recType : recTypes)
        hasher.add(recType);

    hasher.add(static_cast<uint64_t>(params.processing.vanillaBSAList.size()));
    for (const auto& bsa : params.processing.vanillaBSAList)
        hasher.add(StringUtil::toLowerASCII(bsa));

    hasher.add(static_cast<uint64_t>(params.processing.textureMaps.size()));
    for (const auto& [texture, type] : params.processing.textureMaps) {
        hasher.add(StringUtil::toLowerASCII(texture));
        hasher.add(type);
    }

    hasher.add(static_cast<uint64_t>(params.processing.allowList.size()));
    for (const auto& entry : params.processing.allowList)
        hasher.add(StringUtil::toLowerASCII(entry));

    hasher.add(static_cast<uint64_t>(params.processing.blockList.size()));
    for (const auto& entry : params.processing.blockList)
        hasher.add(StringUtil::toLowerASCII(entry));

    hasher.add(params.prePatcher.fixMeshLighting);
    hasher.add(params.shaderPatcher.parallax);
    hasher.add(params.shaderPatcher.complexMaterial);
    hasher.add(params.shaderPatcher.truePBR);
    hasher.add(params.shaderTransforms.parallaxToCM);
    hasher.add(params.postPatcher.disablePrePatchedMaterials);
    hasher.add(params.postPatcher.fixSSS);
    hasher.add(params.postPatcher.hairFlowMap);

    hasher.add(args.considerAllMeshes);
    hasher.add(args.disableDynCubemap);
    hasher.add(args.forceAlwaysCM);
    hasher.add(args.excludeFacegens);

    return hasher.value();
}

/**
 * @brief Fingerprint of the active plugin load order: plugin names in order plus each plugin's size and write time.
 */
auto computePluginFingerprint(const BethesdaGame& bg,
                              const std::vector<std::wstring>& activePlugins) -> uint64_t
{
    HashUtil::Fnv1a64 hasher;
    hasher.add(bg.getGameType());
    hasher.add(static_cast<uint64_t>(activePlugins.size()));

    for (const auto& plugin : activePlugins) {
        hasher.add(StringUtil::toLowerASCII(plugin));

        const auto pluginPath = bg.getGameDataPath() / plugin;
        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(pluginPath, ec);
        if (ec) {
            hasher.add(static_cast<uint8_t>(0));
            continue;
        }

        hasher.add(static_cast<uint8_t>(1));
        hasher.add(static_cast<int64_t>(mtime.time_since_epoch().count()));

        ec.clear();
        const auto size = std::filesystem::file_size(pluginPath, ec);
        hasher.add(static_cast<uint64_t>(ec ? 0 : size));
    }

    return hasher.value();
}

void mainRunnerPrep(const ParallaxGenCLIArgs& args,
                    const PGConfig::PGParams& params,
                    const bool& updateOutput,
                    const std::filesystem::path& exePath,
                    const std::filesystem::path& cfgDir,
                    ProgressWindow* progressWindow,
                    const std::function<void(size_t,
                                             size_t)>& progressCallback)
{
    // Initialize "Preparing" Step.
    progressWindow->CallAfter([progressWindow]() -> void {
        progressWindow->setMainLabel(pgTr("progress.steps.preparing"));
        progressWindow->setStepLabel("");
        progressWindow->setMainProgress(0, numTotalSteps, true);
        progressWindow->setStepProgress(0, numPreparingSteps);
    });

    auto* bg = PGGlobals::getBG();
    auto* pgd = PGGlobals::getPGD();
    auto* pgd3d = PGGlobals::getPGD3D();
    auto* pgmm = PGGlobals::getPGMM();

    //
    // GPU INITIALIZATION.
    //
    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.initGpu")); });

    // Check if GPU needs to be initialized.
    Logger::info("Initializing GPU");
    if (!pgd3d->initGPU()) {
        Logger::critical("Failed to initialize GPU. Exiting.");
        return;
    }

    if (!pgd3d->initShaders()) {
        Logger::critical("Failed to initialize internal shaders. Exiting.");
        return;
    }

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setStepProgress(1, numPreparingSteps); });
    //
    // END GPU INITIALIZATION.
    //

    //
    // OUTPUT DIRECTORY INITIALIZATION.
    //
    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.outputDir")); });

    // Print output location.
    Logger::info(L"PGPatcher output directory: {}", params.output.dir.wstring());

    // Create output directory.
    try {
        if (std::filesystem::create_directories(params.output.dir))
            Logger::debug(L"Output directory created: {}", params.output.dir.wstring());
    } catch (const std::filesystem::filesystem_error& e) {
        Logger::critical("Failed to create output directory: {}", e.what());
        return;
    }

    // If output dir is the same as data dir meshes might get overwritten.
    if (std::filesystem::equivalent(params.output.dir, pgd->getDataPath())) {
        Logger::critical("Output directory cannot be the same directory as your data folder. "
                         "Exiting.");
        return;
    }

    // If output dir is a subdirectory of data dir vfs issues can occur.
    if (boost::istarts_with(params.output.dir.wstring(), bg->getGameDataPath().wstring() + L"\\")) {
        Logger::critical("Output directory cannot be a subdirectory of your data folder. Exiting.");
        return;
    }

    // Update cache: "Update Output" re-patches only what changed since the previous output in the output directory,
    // "Start Patching" regenerates everything. Zipped outputs are always generated from scratch and leave no cache.
    if (updateOutput && params.output.zip)
        Logger::warn("Zip output is enabled, so the previous output cannot be updated and is generated from scratch");
    PGRunCache::initialize(params.output.dir / PGRunCache::s_cacheFilename, !params.output.zip, !updateOutput);
    PGRunCache::setConfigFingerprint(computeConfigFingerprint(params, args));

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setStepProgress(2, numPreparingSteps); });
    //
    // END OUTPUT DIRECTORY INITIALIZATION.
    //

    //
    // PlUGIN VALIDATION.
    //
    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.validatingPlugins")); });

    // Check if dyndolod.esp exists.
    const auto activePlugins = bg->getActivePlugins(false, true);
    if (std::ranges::find(activePlugins, L"dyndolod.esp") != activePlugins.end()) {
        Logger::critical(
            "DynDoLOD and TexGen outputs must be disabled prior to running PGPatcher. It is recommended to "
            "generate LODs after running PGPatcher with the PGPatcher output enabled.");
        return;
    }

    // Log active plugins.
    const std::wstring loadOrderStr = boost::algorithm::join(activePlugins, L",");
    Logger::debug(L"Active Plugin Load Order: {}", loadOrderStr);

    // Update cache: mesh uses can be reused from the previous run if no plugin changed.
    PGRunCache::setPluginFingerprint(computePluginFingerprint(*bg, activePlugins));

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setStepProgress(3, numPreparingSteps); });
    //
    // END PLUGIN VALIDATION.
    //

    //
    // PLUGIN INITIALIZATION.
    //
    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.initPluginPatching")); });

    TaskQueue pluginInit;

    // Init PGP library.
    Logger::info("Initializing plugin patching");
    if (params.processing.multithread) {
        pluginInit.queueTask([&bg, &exePath, &params]() -> void {
            PGPlugin::initialize(*bg, exePath, params.output.pluginLang);
            PGPlugin::populateObjs(params.output.dir / "PGPatcher.esp");
        });
    } else {
        PGPlugin::initialize(*bg, exePath, params.output.pluginLang);
        PGPlugin::populateObjs(params.output.dir / "PGPatcher.esp");
    }

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setStepProgress(4, numPreparingSteps); });
    //
    // END PLUGIN INITIALIZATION.
    //

    //
    // MOD MANAGER INITIALIZATION.
    //
    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.initModManager")); });

    // Populate mod info.
    nlohmann::json modJSON;
    const auto modListFile = cfgDir / "modrules.json";
    if (FileUtil::getJSON(modListFile, modJSON))
        pgmm->loadJSON(modJSON);

    TaskQueue modManagerInit;

    if (params.modManager.type == PGModManager::ModManagerType::MODORGANIZER2
        && !params.modManager.mo2InstanceDir.empty()) {
        // Make sure running is USVFS.
        if (!args.ignoreMO2Check && !PGHandlers::isUnderUSVFS()) {
            Logger::critical("Please verify that you are launching PGPatcher from MO2, VFS not detected.");
            return;
        }

        // MO2.
        if (params.processing.multithread) {
            modManagerInit.queueTask([&pgmm, &params]() -> void {
                pgmm->populateModFileMapMO2(params.modManager.mo2InstanceDir, params.output.dir);
            });
        } else {
            pgmm->populateModFileMapMO2(params.modManager.mo2InstanceDir, params.output.dir);
        }
    } else if (params.modManager.type == PGModManager::ModManagerType::VORTEX) {
        // Vortex.
        if (params.processing.multithread)
            modManagerInit.queueTask([&pgmm, &bg]() -> void { pgmm->populateModFileMapVortex(bg->getGameDataPath()); });
        else
            pgmm->populateModFileMapVortex(bg->getGameDataPath());
    }

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setStepProgress(5, numPreparingSteps); });
    //
    // END MOD MANAGER INITIALIZATION.
    //

    //
    // POPULATING FILE MAP.
    //
    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.populatingFileMap")); });

    // Init file map.
    pgd->populateFileMap(true);

    // Update cache: texture metadata of unchanged textures does not need to be read again.
    PGRunCache::seedTextureMetadata();

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setStepProgress(6, numPreparingSteps); });
    //
    // END POPULATING FILE MAP.
    //

    //
    // VALIDATING DATA FILES.
    //
    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.validatingDataFiles")); });

    // Check if PGPatcheroutput already exists in data directory.
    // FIXME: Check using PGD instead.
    const std::filesystem::path pgStateFilePath = bg->getGameDataPath() / "ParallaxGen_Diff.json";
    if (std::filesystem::exists(pgStateFilePath)) {
        Logger::critical("PGPatcher meshes exist in your data directory, please delete before "
                         "re-running.");
        return;
    }

    // Check if VRAMR Output is enabled.
    if (params.modManager.type != PGModManager::ModManagerType::None && pgd->isFile("vramroutput.tmp")) {
        Logger::critical("Please disable VRAMr output mod before running PGPatcher.");
        return;
    }

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setStepProgress(7, numPreparingSteps); });
    //
    // END VALIDATING DATA FILES.
    //

    //
    // PATCHER INITIALIZATION.
    //
    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.initPatchers")); });

    // Create patcher factory.
    PatcherUtil::PatcherMeshSet meshPatchers;
    if (params.prePatcher.fixMeshLighting) {
        Logger::debug("Adding Mesh Lighting Fix pre-patcher");
        meshPatchers.prePatchers.emplace_back(PatcherMeshPreFixMeshLighting::getFactory());
    }
    if (params.shaderPatcher.parallax || params.shaderPatcher.complexMaterial || params.shaderPatcher.truePBR) {
        // Fix slots only needed for shader patchers.
        Logger::debug("Adding Texture Slot Count Fix pre-patcher");
        meshPatchers.prePatchers.emplace_back(PatcherMeshPreFixTextureSlotCount::getFactory());
    }

    meshPatchers.shaderPatchers.emplace(PatcherMeshShaderDefault::getShaderType(),
                                        PatcherMeshShaderDefault::getFactory());
    if (params.shaderPatcher.parallax) {
        Logger::debug("Adding Parallax shader patcher");
        meshPatchers.shaderPatchers.emplace(PatcherMeshShaderVanillaParallax::getShaderType(),
                                            PatcherMeshShaderVanillaParallax::getFactory());
    }
    if (params.shaderPatcher.complexMaterial) {
        Logger::debug("Adding Complex Material shader patcher");
        meshPatchers.shaderPatchers.emplace(PatcherMeshShaderComplexMaterial::getShaderType(),
                                            PatcherMeshShaderComplexMaterial::getFactory());
        PatcherMeshShaderComplexMaterial::loadOptions(args.disableDynCubemap);
    }
    if (params.shaderPatcher.truePBR) {
        Logger::debug("Adding True PBR shader patcher");
        meshPatchers.shaderPatchers.emplace(PatcherMeshShaderTruePBR::getShaderType(),
                                            PatcherMeshShaderTruePBR::getFactory());
        PatcherMeshShaderTruePBR::loadOptions(true, params.processing.enableModDevMode);
    }
    if (params.shaderTransforms.parallaxToCM) {
        Logger::debug("Adding Parallax to Complex Material shader transform patcher");
        meshPatchers.shaderTransformPatchers[PatcherMeshShaderTransformParallaxToCM::getFromShader()]
            = { PatcherMeshShaderTransformParallaxToCM::getToShader(),
                PatcherMeshShaderTransformParallaxToCM::getFactory() };
        PatcherMeshShaderTransformParallaxToCM::loadOptions(!args.forceAlwaysCM);

        // Initialize patcher hooks.
        if (!PatcherTextureHookConvertToCM::initShader()) {
            Logger::critical("Failed to initialize ConvertToCM shader");
            return;
        }
    }
    if (params.postPatcher.disablePrePatchedMaterials) {
        Logger::debug("Adding Disable Pre-Patched Materials post-patcher");
        meshPatchers.postPatchers.emplace_back(PatcherMeshPostRestoreDefaultShaders::getFactory());
    }
    if (params.postPatcher.fixSSS) {
        Logger::debug("Adding SSS fix post-patcher");
        meshPatchers.postPatchers.emplace_back(PatcherMeshPostFixSSS::getFactory());

        if (!PatcherTextureHookFixSSS::initShader()) {
            Logger::critical("Failed to initialize FixSSS shader");
            return;
        }
    }
    if (params.postPatcher.hairFlowMap) {
        Logger::debug("Adding Hair Flow Map post-patcher");
        meshPatchers.postPatchers.emplace_back(PatcherMeshPostHairFlowMap::getFactory());
    }

    const PatcherUtil::PatcherTextureSet texPatchers;
    PGPatcher::loadPatchers(meshPatchers, texPatchers);

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setStepProgress(8, numPreparingSteps); });
    //
    // END PATCHER INITIALIZATION.
    //

    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.waitPluginInit")); });

    // Plugins required for map files.
    pluginInit.waitForCompletion();
    pluginInit.shutdown();

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setStepProgress(9, numPreparingSteps); });

    //
    // END OUTPUT DIRECTORY CLEANUP.
    //

    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.waitModManagerInit")); });

    // Mods required for map files.
    modManagerInit.waitForCompletion();
    modManagerInit.shutdown();

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setStepProgress(10, numPreparingSteps); });

    // Initialize "Loading meshes" Step.
    progressWindow->CallAfter([progressWindow]() -> void {
        progressWindow->setMainLabel(pgTr("progress.steps.loadingMeshes"));
        progressWindow->setStepLabel(pgTr("progress.steps.readingNifs"));
        progressWindow->setMainProgress(1, numTotalSteps, true);
        progressWindow->setStepProgress(0, 1);
    });

    // Map files.
    pgd->mapFiles(params.processing.blockList,
                  params.processing.allowList,
                  params.processing.textureMaps,
                  params.processing.vanillaBSAList,
                  params.processing.multithread,
                  progressCallback);

    // Any patcher initialization that requires PGD.
    if (params.shaderPatcher.truePBR)
        PatcherMeshShaderTruePBR::loadStatics(pgd->getPBRJSONs());

    // Extended texture classification (complex material detection) runs on a background.
    // Queue and adds shader types to mods as it completes. Wait for it here so mod enable.
    // State and priorities below are computed from complete shader data, and so we do not.
    // Race the classification threads while reading mod shader sets.
    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.classifyingTextures")); });
    pgd->waitForCMClassification();

    // Assign new mod priorities for new mods.
    pgmm->updateStateFromModlist(params.modManager.mo2UseLooseFileOrder);

    // Modrules.json is deliberately not saved here: the state computed above is re-derived on every.
    // Run, and the file must only change when the user applies changes in the conflict manager.
}

void mainRunnerPatch(const ParallaxGenCLIArgs& args,
                     const PGConfig::PGParams& params,
                     const std::filesystem::path& exePath,
                     ProgressWindow* progressWindow,
                     const std::function<void(size_t,
                                              size_t)>& progressCallback)
{
    // Make sure the state is clean for the patch.
    PGPatcher::resetRunState();
    PGPlugin::resetPatchingState();
    PGGlobals::getPGD()->clearGeneratedFiles();
    PGPatcherGlobals::getWXLoggerSink()->resetToRunStart();
    // Messages of a previous patching step must be logged again when the step is re-run (the completion dialog only.
    // Shows messages of the latest step), including messages replayed for meshes that did not need re-patching.
    Logger::resetToRunStart();
    PGRunCache::beginRun();

    //
    // OUTPUT DIRECTORY CLEANUP.
    //

    // Delete existing output.
    // We delete after pluginInit is done because we need to make sure it had a chance to read the old plugin.
    if (PGRunCache::hasPreviousRun()) {
        // Updating a previous output: meshes and textures are kept and pruned per mesh during patching.
        PGPatcher::deleteOutputDir(true, true);
        PGRunCache::snapshotOutputDirectory();
    } else {
        PGPatcher::deleteOutputDir();
    }

    if (params.shaderPatcher.complexMaterial && !args.disableDynCubemap) {
        // Deployed after patching, must not be treated as a stale output.
        PGRunCache::addProtectedOutput(PatcherMeshShaderComplexMaterial::s_dynCubemapPath);
    }

    progressWindow->CallAfter([progressWindow]() -> void {
        progressWindow->setMainLabel(pgTr("progress.steps.patchingMeshes"));
        progressWindow->setStepLabel("");
        progressWindow->setMainProgress(3, numTotalSteps, true);
        progressWindow->setStepProgress(0, 1);
    });

    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.processingNifs")); });

    PGPatcher::patchMeshes(params.processing.multithread,
                           args.considerAllMeshes,
                           params.processing.allowedModelRecordTypes,
                           true,
                           args.excludeFacegens,
                           progressCallback);

    progressWindow->CallAfter([progressWindow]() -> void {
        progressWindow->setMainLabel(pgTr("progress.steps.patchingTextures"));
        progressWindow->setStepLabel(pgTr("progress.steps.processingTextures"));
        progressWindow->setMainProgress(4, numTotalSteps, true);
        progressWindow->setStepProgress(0, 1);
    });

    PGPatcher::patchTextures(params.processing.multithread, progressCallback);

    progressWindow->CallAfter([progressWindow]() -> void {
        progressWindow->setMainLabel(pgTr("progress.steps.finalizing"));
        progressWindow->setStepLabel("");
        progressWindow->setMainProgress(5, numTotalSteps, true);
        progressWindow->setStepProgress(0, numFinalizingSteps);
    });

    //
    // FINISH WRITING FILES.
    //
    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.finishingWritingFiles")); });

    // Wait for file saver to complete.
    if (PGGlobals::getFileSaver().isWorking()) {
        Logger::info("Waiting for files to finish saving...");
        PGGlobals::getFileSaver().waitForCompletion();
    }

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setStepProgress(1, numFinalizingSteps); });
    //
    // END FINISH WRITING FILES.
    //

    // Check for empty output.
    if (PGPatcher::isOutputEmpty()) {
        // Output is empty, there is no previous output to update anymore.
        PGRunCache::discard();
        Logger::warn("Output directory is empty. No files were generated.");
        return;
    }

    //
    // SAVING PLUGINS.
    //
    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.savingPlugins")); });

    Logger::info("Saving Plugins");
    auto esmMode = PGPlugin::ESMMode::PGPatcherOnly;
    if (args.esmAll)
        esmMode = PGPlugin::ESMMode::All;
    else if (args.noEsm)
        esmMode = PGPlugin::ESMMode::None;
    PGPlugin::savePlugin(params.output.dir, esmMode);

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setStepProgress(2, numFinalizingSteps); });

    //
    // END SAVING PLUGINS.
    //

    //
    // DEPLOY ASSETS.
    //
    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.deployingAssets")); });

    if (params.shaderPatcher.complexMaterial && !args.disableDynCubemap) {
        // Deploy Assets.
        deployDynamicCubemapFile(params.output.dir, exePath);
    }

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setStepProgress(3, numFinalizingSteps); });
    //
    // END DEPLOY ASSETS.
    //

    //
    // SAVING DIFF JSON.
    //
    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.savingDiffJson")); });

    // Save diff json.
    const auto diffJSON = PGPatcher::getDiffJSON();
    if (!diffJSON.empty()) {
        const std::filesystem::path diffJSONPath = params.output.dir / "ParallaxGen_Diff.json";
        FileUtil::saveJSON(diffJSONPath, diffJSON, true);

        PGGlobals::getPGD()->addGeneratedFile("ParallaxGen_Diff.json");
    }

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setStepProgress(4, numFinalizingSteps); });
    //
    // END SAVING DIFF JSON.
    //

    //
    // SAVING UPDATE CACHE.
    //
    // Describes this output so the next run into this directory only re-patches what changed (disabled when zipping).
    progressWindow->CallAfter(
        [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.savingUpdateCache")); });

    PGRunCache::finishRun(!params.output.zip);
    //
    // END SAVING UPDATE CACHE.
    //

    // Archive.
    if (params.output.zip) {
        //
        // OUTPUT ZIP.
        //
        progressWindow->CallAfter(
            [progressWindow]() -> void { progressWindow->setStepLabel(pgTr("progress.steps.creatingZipArchive")); });

        Logger::info("Creating output Zip archive");
        const auto zipPath = params.output.dir / "PGPatcher_Output.zip";
        zipDirectory(params.output.dir, zipPath);
        PGPatcher::deleteOutputDir(false);

        progressWindow->CallAfter(
            [progressWindow]() -> void { progressWindow->setStepProgress(5, numFinalizingSteps); });
        //
        // END OUTPUT ZIP.
        //
    }

    progressWindow->CallAfter([progressWindow]() -> void { progressWindow->setMainProgress(6, numTotalSteps, true); });
}

void mainRunner(ParallaxGenCLIArgs& args,
                const std::filesystem::path& exePath)
{
    ExceptionHandler::setMainThread();

    // Define paths.
    PGPatcherGlobals::setEXEPath(exePath);
    const std::filesystem::path cfgDir = exePath / "cfg";

    // Create cfg directory if it does not exist.
    if (!std::filesystem::exists(cfgDir))
        std::filesystem::create_directories(cfgDir);

    // Initialize PGConfig.
    PGConfig::loadStatics(exePath);
    auto pgc = PGConfig();
    pgc.loadConfig();

    PGPatcherGlobals::setPGC(&pgc);

    // Initialize localization (GUI strings).
    PGLocale::init(exePath / "translations", pgc.getUILanguage());

    // Initialize UI.
    PGUI::init();

    auto params = pgc.getParams();

    // Show launcher UI. "Update Output" (or --autostart-update) updates the previous output in the output location in.
    // Place, "Start Patching" (or --autostart) regenerates it from scratch.
    const bool autostart = args.autostart || args.autostartUpdate;
    bool updateOutput = args.autostartUpdate;
    if (!autostart)
        updateOutput = PGUI::showLauncher(pgc, params);

    // Paths in the config may be relative to the PGPatcher.exe folder: the config keeps them as typed, the run uses.
    // The resolved paths.
    PGConfig::resolveRelativePaths(params);

    // Validate config.
    std::vector<std::string> errors;
    if (!PGConfig::validateParams(params, errors)) {
        // This should never happen because there is a frontend validation that would have to be bypassed.
        std::string errorList;
        for (const auto& error : errors)
            errorList += "- " + error + "\n";
        std::cerr << "Configuration is invalid:\n" << errorList << "\n";
        return;
    }

    // LOGGING SHOULD ONLY HAPPEN PAST THIS POINT.
    const std::filesystem::path logPath = exePath / "log" / "PGPatcher.log";
    initLogger(logPath, params.processing.enableDebugLogging, params.processing.enableTraceLogging);

    // Welcome Message.
    Logger::info("Welcome to PGPatcher version {}!", PG_FULL_VERSION);

#if defined(PG_PRERELEASE) && (PG_PRERELEASE > 0)
    // Post test message for test builds.
    Logger::warn("This is an EXPERIMENTAL pre-release build of PGPatcher");
#endif

    // Create relevant objects.
    // FIXME: Control the lifetime of these in PGLib.
    auto bg = BethesdaGame(params.game.type, params.game.dir);
    PGGlobals::setBG(&bg);
    auto pgmm = PGModManager(params.modManager.type);
    PGGlobals::setPGMM(&pgmm);
    auto pgd = PGDirectory(&bg, params.output.dir);
    PGGlobals::setPGD(&pgd);
    auto pgd3d = PGD3D(exePath / "cshaders");
    PGGlobals::setPGD3D(&pgd3d);

    // Create progress dialog object.
    auto* progressWindow = new ProgressWindow(); // NOLINT(cppcoreguidelines-owning-memory)

    // Create callback function for progress bars.
    const std::function<void(size_t, size_t)>& progressCallback
        = [&progressWindow](size_t completed, size_t total) -> void {
        progressWindow->CallAfter([=]() -> void {
            progressWindow->setStepProgress(static_cast<int>(completed), static_cast<int>(total), true);
        });
    };

    const std::function<void()>& exceptionCallback = [&progressWindow]() -> void {
        progressWindow->CallAfter([=]() -> void { progressWindow->EndModal(wxID_OK); });
    };

    TaskPoolRunner::setExceptionCallback(exceptionCallback);
    TaskQueue::setExceptionCallback(exceptionCallback);

    // Get current time to compare later.
    const auto startTime = std::chrono::high_resolution_clock::now();
    long long timeTaken = 0;

    // Dispatch the pre-generation task.
    TaskQueue backgroundRunners;
    backgroundRunners.queueTask(
        [&args, &params, &updateOutput, &exePath, &progressWindow, &cfgDir, &progressCallback]() -> void {
            mainRunnerPrep(args, params, updateOutput, exePath, cfgDir, progressWindow, progressCallback);

            // Snapshot message counts after prep so re-runs of the patching step can discard.
            // Messages from a previous patch run while keeping preparation-phase messages.
            PGPatcherGlobals::getWXLoggerSink()->markRunStart();
            Logger::markRunStart();

            mainRunnerPatch(args, params, exePath, progressWindow, progressCallback);
            auto* const progressWindowPtr = progressWindow;
            progressWindow->CallAfter([progressWindowPtr]() -> void { progressWindowPtr->EndModal(wxID_OK); });
        });

    // Show progress dialog (this will block until closed by one of the callafters).
    progressWindow->ShowModal();

    // Verify tasks are finished.
    ExceptionHandler::throwExceptionOnMainThread();
    backgroundRunners.waitForCompletion();

    // Confirmation UI.
    const auto endTime = std::chrono::high_resolution_clock::now();
    timeTaken += std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();

    Logger::info("PGPatcher took {} seconds to complete (does not include time in user interface)", timeTaken);

    // Show completion dialog.
    CompletionDialog dlg(timeTaken);
    while (dlg.ShowModal() == wxID_RETRY) {
        // Restart time.
        const auto startTime = std::chrono::high_resolution_clock::now();

        // Return code RETRY means we redo the patching process.
        backgroundRunners.queueTask([&args, &params, &exePath, &progressWindow, &cfgDir, &progressCallback]() -> void {
            mainRunnerPatch(args, params, exePath, progressWindow, progressCallback);
            auto* const progressWindowPtr = progressWindow;
            progressWindow->CallAfter([progressWindowPtr]() -> void { progressWindowPtr->EndModal(wxID_OK); });
        });

        // Show progress dialog (this will block until closed by one of the callafters).
        progressWindow->ShowModal();

        // Verify tasks are finished.
        ExceptionHandler::throwExceptionOnMainThread();
        backgroundRunners.waitForCompletion();

        const auto endTime = std::chrono::high_resolution_clock::now();
        timeTaken = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
        dlg.updateTimingInfo(timeTaken);
        dlg.refreshLogMessages();

        Logger::info("PGPatcher took {} seconds to complete (does not include time in user interface)", timeTaken);
    }
}

void addArguments(CLI::App& app,
                  ParallaxGenCLIArgs& args)
{
    // Logging.
    auto* const autostartFlag = app.add_flag(
        "--autostart", args.autostart, "Start generation without user input (regenerates the output from scratch)");
    auto* const autostartUpdateFlag
        = app.add_flag("--autostart-update",
                       args.autostartUpdate,
                       "Start updating the previous output in the output location without user input, like the "
                       "\"Update Output\" button (generates from scratch if there is no previous output)");
    autostartFlag->excludes(autostartUpdateFlag);
    autostartUpdateFlag->excludes(autostartFlag);
    app.add_flag("--console", args.console, "Show console in the background");
    app.add_flag(
        "--consider-allmeshes", args.considerAllMeshes, "Consider all meshes, even those not in plugins, for patching");
    app.add_flag("--ignore-mo2vfscheck", args.ignoreMO2Check, "Ignore MO2 VFS check - might be useful for Linux users");
    app.add_flag(
        "--disable-dyncubemap", args.disableDynCubemap, "Do not apply dynamic cubemap to any Complex Material");
    app.add_flag(
        "--force-always-cm",
        args.forceAlwaysCM,
        "If upgrade to CM patcher is enabled, everything will be upgraded to CM no matter what (no parallax will be "
        "used)");
    app.add_flag("--exclude-facegens", args.excludeFacegens, "Do not patch facegen meshes");
    auto* const esmAllFlag
        = app.add_flag("--esm-all", args.esmAll, "ESM flag all output plugins, not just PGPatcher.esp");
    auto* const noEsmFlag = app.add_flag("--no-esm", args.noEsm, "Do not ESM flag PGPatcher.esp");
    esmAllFlag->excludes(noEsmFlag);
    noEsmFlag->excludes(esmAllFlag);
}
}

auto WINAPI WinMain(HINSTANCE /*hInstance*/,
                    HINSTANCE /*hPrevInstance*/,
                    LPSTR /*lpCmdLine*/,
                    int /*nCmdShow*/) -> int
{
// Block until enter only in debug mode.
#ifdef _DEBUG
    std::cout << "Press ENTER to start (DEBUG mode)...";
    std::cin.get();
#endif

    SetUnhandledExceptionFilter(PGHandlers::customExceptionHandler);

    SetConsoleOutputCP(CP_UTF8);

    // Find location of ParallaxGen.exe.
    const std::filesystem::path exePath = PGHandlers::getExePath().parent_path();
    configureDotNetLibDirectory(exePath);

    // CLI Arguments.
    ParallaxGenCLIArgs args;
    CLI::App app { "PGPatcher" };
    addArguments(app, args);

    // Parse CLI Arguments (this is what exits on any validation issues).
    CLI11_PARSE(app, __argc, __argv);

    if (args.console) {
        // Allocate a console.
        AllocConsole();
        SetConsoleOutputCP(CP_UTF8);
    }

    // Main Runner (Catches all exceptions).
    CPPTRACE_TRY { mainRunner(args, exePath); }
    CPPTRACE_CATCH(const std::exception& e)
    {
        ExceptionHandler::setException(e, cpptrace::from_current_exception().to_string());
    }

    int returnCode = 0;
    if (ExceptionHandler::hasException()) {
        ExceptionHandler::throwExceptionOnMainThread();
        returnCode = 1;
    }

    return returnCode;
}
