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
#include "patchers/PatcherMeshPreStockMarker.hpp"
#include "patchers/PatcherMeshShaderComplexMaterial.hpp"
#include "patchers/PatcherMeshShaderTransformParallaxToCM.hpp"
#include "patchers/PatcherMeshShaderTruePBR.hpp"
#include "patchers/PatcherMeshShaderVanillaParallax.hpp"
#include "patchers/PatcherTextureGlobalConvertToHDR.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "patchers/base/PatcherUtil.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "util/ExceptionHandler.hpp"
#include "util/StringUtil.hpp"

#include "NifFile.hpp"
#include <CLI/CLI.hpp>
#include <cpptrace/from_current.hpp>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
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

    struct GenDiffBlocks {
        CLI::App* subCommand = nullptr;
        std::filesystem::path original;
        std::filesystem::path patched;
        std::filesystem::path output = "PGStock_Output";
    } genDiffBlocks;
};

std::vector<std::byte> readFileBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Unable to open file: " + StringUtil::utf16toUTF8(path.wstring()));

    const auto size = static_cast<std::streamoff>(file.tellg());
    if (size < 0)
        throw std::runtime_error("Unable to read file: " + StringUtil::utf16toUTF8(path.wstring()));

    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<size_t>(size));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file)
        throw std::runtime_error("Unable to read file: " + StringUtil::utf16toUTF8(path.wstring()));

    return bytes;
}

bool isNIFFile(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec)
        && StringUtil::toLowerASCIIFast(path.extension().wstring()) == L".nif";
}

/**
 * @brief Writes a copy of a patched mesh whose shapes carry PG_STOCK blocks for everything that differs from the
 * original mesh
 *
 * @param originalFile Unpatched mesh
 * @param patchedFile Patched mesh (left untouched)
 * @param outputFile Where the copy is written
 * @param[in,out] numMarkedShapes Incremented by the number of shapes that received a block
 * @return true The copy was written
 * @return false The meshes could not be processed (reported)
 */
bool markMeshPair(const std::filesystem::path& originalFile,
                  const std::filesystem::path& patchedFile,
                  const std::filesystem::path& outputFile,
                  size_t& numMarkedShapes)
{
    const auto label = StringUtil::utf16toUTF8(patchedFile.wstring());

    std::error_code ec;
    if (std::filesystem::exists(outputFile, ec)
        && (std::filesystem::equivalent(outputFile, patchedFile, ec)
            || std::filesystem::equivalent(outputFile, originalFile, ec))) {
        spdlog::error("{}: the output would overwrite an input mesh, skipping", label);
        return false;
    }

    nifly::NifFile originalNif;
    nifly::NifFile patchedNif;
    try {
        originalNif = PGNIFUtil::loadNIFFromBytes(readFileBytes(originalFile), false);
        patchedNif = PGNIFUtil::loadNIFFromBytes(readFileBytes(patchedFile), false);
    } catch (const std::exception& e) {
        spdlog::error("{}: unable to load the meshes: {}", label, e.what());
        return false;
    }

    numMarkedShapes += PatcherMeshPreStockMarker::markPatchedShapes(originalNif, patchedNif, patchedFile.wstring());

    std::filesystem::create_directories(outputFile.parent_path(), ec);
    patchedNif.PrettySortBlocks();
    if (patchedNif.Save(outputFile, { .optimize = false, .sortBlocks = false })) {
        spdlog::error("{}: unable to save {}", label, StringUtil::utf16toUTF8(outputFile.wstring()));
        return false;
    }

    return true;
}

void runGenDiffBlocks(const PGToolsCLIArgs::GenDiffBlocks& args)
{
    const auto original = std::filesystem::absolute(args.original);
    const auto patched = std::filesystem::absolute(args.patched);
    const auto output = std::filesystem::absolute(args.output);

    if (!std::filesystem::exists(original) || !std::filesystem::exists(patched)) {
        spdlog::critical("The original and the patched path must both exist");
        return;
    }

    const bool isOriginalFolder = std::filesystem::is_directory(original);
    if (isOriginalFolder != std::filesystem::is_directory(patched)) {
        spdlog::critical("The original and the patched path must both be folders or both be mesh files");
        return;
    }

    // Original mesh, patched mesh and the path of the output relative to the output folder.
    std::vector<std::tuple<std::filesystem::path, std::filesystem::path, std::filesystem::path>> meshPairs;
    if (!isOriginalFolder) {
        meshPairs.emplace_back(original, patched, patched.filename());
    } else {
        std::error_code ec;
        if (std::filesystem::equivalent(output, original, ec) || std::filesystem::equivalent(output, patched, ec)) {
            spdlog::critical("The output folder must differ from the original and the patched folder");
            return;
        }

        // Every patched mesh with an original counterpart is a pair. A mesh that only exists on one side is reported.
        std::unordered_set<std::wstring> patchedRelPaths;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 patched, std::filesystem::directory_options::skip_permission_denied)) {
            if (!isNIFFile(entry.path()))
                continue;

            const auto relPath = entry.path().lexically_relative(patched);
            patchedRelPaths.insert(StringUtil::toLowerASCIIFast(relPath.wstring()));

            const auto originalFile = original / relPath;
            if (isNIFFile(originalFile)) {
                meshPairs.emplace_back(originalFile, entry.path(), relPath);
            } else {
                spdlog::warn("{} only exists in the patched folder, skipping it",
                             StringUtil::utf16toUTF8(relPath.wstring()));
            }
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 original, std::filesystem::directory_options::skip_permission_denied)) {
            if (!isNIFFile(entry.path()))
                continue;

            const auto relPath = entry.path().lexically_relative(original);
            if (!patchedRelPaths.contains(StringUtil::toLowerASCIIFast(relPath.wstring()))) {
                spdlog::warn("{} only exists in the original folder, skipping it",
                             StringUtil::utf16toUTF8(relPath.wstring()));
            }
        }
    }

    size_t numWritten = 0;
    size_t numMarkedShapes = 0;
    size_t numFailed = 0;
    for (const auto& [originalFile, patchedFile, relPath] : meshPairs)
        if (markMeshPair(originalFile, patchedFile, output / relPath, numMarkedShapes))
            numWritten++;
        else
            numFailed++;

    spdlog::info("Wrote {} meshes with {} marked shapes to {}",
                 numWritten,
                 numMarkedShapes,
                 StringUtil::utf16toUTF8(output.wstring()));
    if (numFailed)
        spdlog::warn("{} meshes could not be processed", numFailed);
}

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

    // Check if gendiffblocks subcommand was used.
    if (args.genDiffBlocks.subCommand->parsed()) {
        runGenDiffBlocks(args.genDiffBlocks);
        return;
    }

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
        // The stock marker reverts pre-patched shapes to stock, so it has to run before every other patcher.
        meshPatchers.prePatchers.emplace_back(PatcherMeshPreStockMarker::factory());
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
            PatcherMeshShaderTruePBR::loadStatics(pgd.pbrJSONs(), args.multithreading);
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

    args.genDiffBlocks.subCommand = app.add_subcommand("gendiffblocks",
                                                       "Record the stock state of pre-patched meshes in PG_STOCK extra "
                                                       "data blocks, so that PGPatcher can patch them from stock");
    args.genDiffBlocks.subCommand
        ->add_option("original", args.genDiffBlocks.original, "Original (unpatched) mesh file or folder")
        ->required();
    args.genDiffBlocks.subCommand->add_option("patched", args.genDiffBlocks.patched, "Patched mesh file or folder")
        ->required();
    args.genDiffBlocks.subCommand
        ->add_option("output",
                     args.genDiffBlocks.output,
                     "Output folder that receives the patched meshes with their PG_STOCK blocks")
        ->default_str("PGStock_Output");
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
