#include "ParallaxGen.hpp"

#include <DirectXTex.h>
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/crc.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/thread.hpp>
#include <fstream>
#include <spdlog/spdlog.h>
#include <vector>

#include "NIFUtil.hpp"
#include "ParallaxGenDirectory.hpp"
#include "ParallaxGenUtil.hpp"

using namespace std;
using namespace ParallaxGenUtil;
using namespace nifly;

ParallaxGen::ParallaxGen(filesystem::path OutputDir, ParallaxGenDirectory *PGD, ParallaxGenConfig *PGC,
                         ParallaxGenD3D *PGD3D, const bool &OptimizeMeshes, const bool &IgnoreParallax,
                         const bool &IgnoreCM, const bool &IgnoreTruePBR)
    : OutputDir(std::move(OutputDir)), PGD(PGD), PGC(PGC), PGD3D(PGD3D), IgnoreParallax(IgnoreParallax),
      IgnoreCM(IgnoreCM), IgnoreTruePBR(IgnoreTruePBR) {
  // constructor

  // set optimize meshes flag
  NIFSaveOptions.optimize = OptimizeMeshes;
}

void ParallaxGen::upgradeShaders() {
  // Get height maps (vanilla _p.dds files)
  auto HeightMaps = PGD->getHeightMaps();

  // Define task parameters
  ParallaxGenTask TaskTracker("Shader Upgrades", HeightMaps.size());

  for (const auto &HeightMap : HeightMaps) {
    TaskTracker.completeJob(convertHeightMapToComplexMaterial(HeightMap));
  }
}

void ParallaxGen::patchMeshes(const bool &MultiThread) {
  auto Meshes = PGD->getMeshes();

  // Create task tracker
  ParallaxGenTask TaskTracker("Mesh Patcher", Meshes.size());

  // Create thread group
  boost::thread_group ThreadGroup;

  // Define diff JSON
  nlohmann::json DiffJSON;

  // Create threads
  if (MultiThread) {
#ifdef _DEBUG
    size_t NumThreads = 1;
#else
    size_t NumThreads = boost::thread::hardware_concurrency();
#endif

    boost::asio::thread_pool MeshPatchPool(NumThreads);

    for (const auto &Mesh : Meshes) {
      boost::asio::post(MeshPatchPool,
                        [this, &TaskTracker, &DiffJSON, Mesh] { TaskTracker.completeJob(processNIF(Mesh, DiffJSON)); });
    }

    MeshPatchPool.join();
  } else {
    patchMeshBatch(Meshes, 0, Meshes.size(), TaskTracker, DiffJSON);
  }

  // Save DiffJSON file
  spdlog::info("Saving diff JSON file...");
  filesystem::path DiffJSONPath = OutputDir / getDiffJSONName();
  ofstream DiffJSONFile(DiffJSONPath);
  DiffJSONFile << DiffJSON;
  DiffJSONFile.close();
}

void ParallaxGen::patchMeshBatch(const std::vector<std::filesystem::path> &Meshes, const size_t &Start,
                                 const size_t &End, ParallaxGenTask &TaskTracker, nlohmann::json &DiffJSON) {
  for (size_t I = Start; I < End; I++) {
    TaskTracker.completeJob(processNIF(Meshes[I], DiffJSON));
  }
}

auto ParallaxGen::convertHeightMapToComplexMaterial(const filesystem::path &HeightMap) -> ParallaxGenTask::PGResult {
  spdlog::trace(L"Upgrading height map: {}", HeightMap.wstring());

  auto Result = ParallaxGenTask::PGResult::SUCCESS;

  string HeightMapStr = HeightMap.string();

  // Get texture base (remove _p.dds)
  static const auto ParallaxSuffixes = PGC->getConfig()["suffixes"][3].get<vector<string>>();
  string TexBase = NIFUtil::getTexBase(HeightMapStr, ParallaxSuffixes);
  if (TexBase.empty()) {
    // no height map (this shouldn't happen)
    return Result;
  }

  static const auto *CMBaseList = &PGD->getComplexMaterialMapsBases();
  static const auto *CMList = &PGD->getComplexMaterialMaps();
  auto ExistingCM = NIFUtil::getTexMatch(TexBase, *CMBaseList, *CMList);
  if (!ExistingCM.empty()) {
    // complex material already exists
    return Result;
  }

  // Check if vanilla env mask exists (should be included in CM map)
  filesystem::path EnvMask = filesystem::path();
  vector<filesystem::path> EnvMaskPossibilities = {TexBase + "_m.dds", TexBase + "_em.dds"};
  for (const auto &EnvMaskPossibility : EnvMaskPossibilities) {
    if (PGD->isFile(EnvMaskPossibility)) {
      // found env mask
      EnvMask = EnvMaskPossibility;
      break;
    }
  }

  const filesystem::path ComplexMap = TexBase + "_m.dds";

  // upgrade to complex material
  DirectX::ScratchImage NewComplexMap = PGD3D->upgradeToComplexMaterial(HeightMap, EnvMask);

  // save to file
  if (NewComplexMap.GetImageCount() > 0) {
    filesystem::path OutputPath = OutputDir / ComplexMap;
    filesystem::create_directories(OutputPath.parent_path());

    HRESULT HR = DirectX::SaveToDDSFile(NewComplexMap.GetImages(), NewComplexMap.GetImageCount(),
                                        NewComplexMap.GetMetadata(), DirectX::DDS_FLAGS_NONE, OutputPath.c_str());
    if (FAILED(HR)) {
      spdlog::error(L"Unable to save complex material {}: {}", OutputPath.wstring(),
                    stringToWstring(ParallaxGenD3D::getHRESULTErrorMessage(HR)));
      Result = ParallaxGenTask::PGResult::FAILURE;
      return Result;
    }

    // add newly created file to complexMaterialMaps for later processing
    PGD->addComplexMaterialMap(ComplexMap, TexBase);

    spdlog::debug(L"Generated complex material map: {}", ComplexMap.wstring());
  } else {
    Result = ParallaxGenTask::PGResult::FAILURE;
  }

  return Result;
}

void ParallaxGen::zipMeshes() const {
  // Zip meshes
  spdlog::info("Zipping meshes...");
  zipDirectory(OutputDir, OutputDir / getOutputZipName());
}

void ParallaxGen::deleteMeshes() const {
  // delete meshes
  spdlog::info("Cleaning up meshes generated by ParallaxGen...");
  // Iterate through the folder
  for (const auto &Entry : filesystem::directory_iterator(OutputDir)) {
    if (filesystem::is_directory(Entry.path())) {
      // Remove the directory and all its contents
      try {
        filesystem::remove_all(Entry.path());
        spdlog::trace(L"Deleted directory {}", Entry.path().wstring());
      } catch (const exception &E) {
        spdlog::error(L"Error deleting directory {}: {}", Entry.path().wstring(), stringToWstring(E.what()));
      }
    }

    // remove stray files except output
    if (!boost::equals(Entry.path().filename().wstring(), getOutputZipName().wstring())) {
      try {
        filesystem::remove(Entry.path());
      } catch (const exception &E) {
        spdlog::error(L"Error deleting state file {}: {}", Entry.path().wstring(), stringToWstring(E.what()));
      }
    }
  }
}

void ParallaxGen::deleteOutputDir() const {
  // delete output directory
  if (filesystem::exists(OutputDir) && filesystem::is_directory(OutputDir)) {
    spdlog::info("Deleting existing ParallaxGen output...");

    try {
      for (const auto &Entry : filesystem::directory_iterator(OutputDir)) {
        filesystem::remove_all(Entry.path());
      }
    } catch (const exception &E) {
      spdlog::critical(L"Error deleting output directory {}: {}", OutputDir.wstring(), stringToWstring(E.what()));
      exit(1);
    }
  }
}

auto ParallaxGen::getOutputZipName() -> filesystem::path { return "ParallaxGen_Output.zip"; }

auto ParallaxGen::getDiffJSONName() -> filesystem::path { return "ParallaxGen_Diff.json"; }

// shorten some enum names
auto ParallaxGen::processNIF(const filesystem::path &NIFFile, nlohmann::json &DiffJSON) -> ParallaxGenTask::PGResult {
  auto Result = ParallaxGenTask::PGResult::SUCCESS;

  // Determine output path for patched NIF
  const filesystem::path OutputFile = OutputDir / NIFFile;
  if (filesystem::exists(OutputFile)) {
    spdlog::error(L"Unable to process NIF file, file already exists: {}", NIFFile.wstring());
    Result = ParallaxGenTask::PGResult::FAILURE;
    return Result;
  }

  // NIF file object
  NifFile NIF;

  // Get NIF Bytes
  const vector<std::byte> NIFFileData = PGD->getFile(NIFFile);
  if (NIFFileData.empty()) {
    spdlog::error(L"Unable to read NIF file: {}", NIFFile.wstring());
    Result = ParallaxGenTask::PGResult::FAILURE;
    return Result;
  }

  // Convert Byte Vector to Stream
  boost::iostreams::array_source NIFArraySource(reinterpret_cast<const char *>(NIFFileData.data()), NIFFileData.size());
  boost::iostreams::stream<boost::iostreams::array_source> NIFStream(NIFArraySource);

  try {
    // try block for loading nif
    NIF.Load(NIFStream);
  } catch (const exception &E) {
    spdlog::error(L"Error reading NIF file from BSA: {}, {}", NIFFile.wstring(), stringToWstring(E.what()));
    Result = ParallaxGenTask::PGResult::FAILURE;
    return Result;
  }

  if (!NIF.IsValid()) {
    spdlog::error(L"Invalid NIF file (ignoring): {}", NIFFile.wstring());
    Result = ParallaxGenTask::PGResult::FAILURE;
    return Result;
  }

  // Stores whether the NIF has been modified throughout the patching process
  bool NIFModified = false;

  // Build static search vectors
  static const auto SlotSearchVP = PGC->getConfig()["parallax_processing"]["lookup_order"].get<vector<int>>();
  static const auto SlotSearchCM = PGC->getConfig()["complexmaterial_processing"]["lookup_order"].get<vector<int>>();
  static const auto DynCubeBlocklist = stringVecToWstringVec(
      PGC->getConfig()["complexmaterial_processing"]["dyncubemap_blocklist"].get<vector<string>>());

  // Create Patcher objects
  PatcherVanillaParallax PatchVP(NIFFile, &NIF, SlotSearchVP, PGC, PGD, PGD3D);
  PatcherComplexMaterial PatchCM(NIFFile, &NIF, SlotSearchCM, DynCubeBlocklist, PGC, PGD, PGD3D);
  PatcherTruePBR PatchTPBR(NIFFile, &NIF);

  // Patch each shape in NIF
  size_t NumShapes = 0;
  bool OneShapeSuccess = false;
  for (NiShape *NIFShape : NIF.GetShapes()) {
    NumShapes++;

    bool ShapeModified = false;
    size_t ShaderApplied = 0;
    ParallaxGenTask::updatePGResult(
        Result, processShape(NIF, NIFShape, PatchVP, PatchCM, PatchTPBR, ShapeModified, ShaderApplied),
        ParallaxGenTask::PGResult::SUCCESS_WITH_WARNINGS);

    // Update NIFModified if shape was modified
    if (ShapeModified) {
      NIFModified = true;

      // Update DiffJSON
      const auto ShapeBlockID = NIF.GetBlockID(NIFShape);
      DiffJSON[NIFFile.string()]["shapes"][to_string(ShapeBlockID)]["shaderapplied"] = ShaderApplied;
    }

    if (Result == ParallaxGenTask::PGResult::SUCCESS) {
      OneShapeSuccess = true;
    }
  }

  if (!OneShapeSuccess && NumShapes > 0) {
    // No shapes were successfully processed
    Result = ParallaxGenTask::PGResult::FAILURE;
    return Result;
  }

  // Save patched NIF if it was modified
  if (NIFModified) {
    // Calculate CRC32 hash before
    boost::crc_32_type CRCBeforeResult;
    CRCBeforeResult.process_bytes(NIFFileData.data(), NIFFileData.size());
    const auto CRCBefore = CRCBeforeResult.checksum();

    spdlog::debug(L"NIF Patched: {}", NIFFile.wstring());

    // create directories if required
    filesystem::create_directories(OutputFile.parent_path());

    if (NIF.Save(OutputFile, NIFSaveOptions) != 0) {
      spdlog::error(L"Unable to save NIF file: {}", NIFFile.wstring());
      Result = ParallaxGenTask::PGResult::FAILURE;
      return Result;
    }

    // Clear NIF from memory (no longer needed)
    NIF.Clear();

    // Calculate CRC32 hash after
    const auto OutputFileBytes = getFileBytes(OutputFile);
    boost::crc_32_type CRCResultAfter;
    CRCResultAfter.process_bytes(OutputFileBytes.data(), OutputFileBytes.size());
    const auto CRCAfter = CRCResultAfter.checksum();

    // Add to diff JSON
    DiffJSON[NIFFile.string()]["crc32original"] = CRCBefore;
    DiffJSON[NIFFile.string()]["crc32patched"] = CRCAfter;
  }

  return Result;
}

auto ParallaxGen::processShape(NifFile &NIF, NiShape *NIFShape, PatcherVanillaParallax &PatchVP,
                               PatcherComplexMaterial &PatchCM, PatcherTruePBR &PatchTPBR, bool &ShapeModified,
                               size_t &ShaderApplied) -> ParallaxGenTask::PGResult {
  auto Result = ParallaxGenTask::PGResult::SUCCESS;

  // Prep
  const auto ShapeBlockID = NIF.GetBlockID(NIFShape);

  // Check for exclusions
  // get NIFShader type
  if (!NIFShape->HasShaderProperty()) {
    spdlog::trace(L"Rejecting shape {}: No NIFShader property", ShapeBlockID);
    return Result;
  }

  // only allow BSLightingShaderProperty blocks
  string NIFShapeName = NIFShape->GetBlockName();
  if (NIFShapeName != "NiTriShape" && NIFShapeName != "BSTriShape") {
    spdlog::trace(L"Rejecting shape {}: Incorrect shape block type", ShapeBlockID);
    return Result;
  }

  // get NIFShader from shape
  NiShader *NIFShader = NIF.GetShader(NIFShape);
  if (NIFShader == nullptr) {
    // skip if no NIFShader
    spdlog::trace(L"Rejecting shape {}: No NIFShader", ShapeBlockID);
    return Result;
  }

  // check that NIFShader has a texture set
  if (!NIFShader->HasTextureSet()) {
    spdlog::trace(L"Rejecting shape {}: No texture set", ShapeBlockID);
    return Result;
  }

  // check that NIFShader is a BSLightingShaderProperty
  string NIFShaderName = NIFShader->GetBlockName();
  if (NIFShaderName != "BSLightingShaderProperty") {
    spdlog::trace(L"Rejecting shape {}: Incorrect NIFShader block type", ShapeBlockID);
    return Result;
  }

  static const vector<vector<string>> Suffixes = PGC->getConfig()["suffixes"].get<vector<vector<string>>>();
  auto SearchPrefixes = NIFUtil::getSearchPrefixes(NIF, NIFShape, Suffixes);
  string MatchedPath;

  // TRUEPBR CONFIG
  if (!IgnoreTruePBR) {
    bool EnableTruePBR = false;
    map<size_t, tuple<nlohmann::json, string>> TruePBRData;
    ParallaxGenTask::updatePGResult(Result, PatchTPBR.shouldApply(SearchPrefixes, EnableTruePBR, TruePBRData),
                                    ParallaxGenTask::PGResult::SUCCESS_WITH_WARNINGS);
    if (EnableTruePBR) {
      // Enable TruePBR on shape
      for (auto &TruePBRCFG : TruePBRData) {
        ParallaxGenTask::updatePGResult(Result, PatchTPBR.applyPatch(NIFShape, get<0>(TruePBRCFG.second),
                                                                     get<1>(TruePBRCFG.second), ShapeModified));
      }

      ShaderApplied = 3;
      return Result;
    }
  }

  // COMPLEX MATERIAL
  if (!IgnoreCM) {
    bool EnableCM = false;
    bool EnableDynCubemaps = false;
    MatchedPath = "";
    ParallaxGenTask::updatePGResult(
        Result, PatchCM.shouldApply(NIFShape, SearchPrefixes, EnableCM, EnableDynCubemaps, MatchedPath),
        ParallaxGenTask::PGResult::SUCCESS_WITH_WARNINGS);
    if (EnableCM) {
      // Enable complex material on shape
      ParallaxGenTask::updatePGResult(Result,
                                      PatchCM.applyPatch(NIFShape, MatchedPath, EnableDynCubemaps, ShapeModified));

      ShaderApplied = 2;
      return Result;
    }
  }

  // VANILLA PARALLAX
  if (!IgnoreParallax) {
    bool EnableParallax = false;
    MatchedPath = "";
    ParallaxGenTask::updatePGResult(Result, PatchVP.shouldApply(NIFShape, SearchPrefixes, EnableParallax, MatchedPath),
                                    ParallaxGenTask::PGResult::SUCCESS_WITH_WARNINGS);
    if (EnableParallax) {
      // Enable Parallax on shape
      ParallaxGenTask::updatePGResult(Result, PatchVP.applyPatch(NIFShape, MatchedPath, ShapeModified));

      ShaderApplied = 1;
      return Result;
    }
  }

  ShaderApplied = 0;
  return Result;
}

void ParallaxGen::addFileToZip(mz_zip_archive &Zip, const filesystem::path &FilePath,
                               const filesystem::path &ZipPath) const {
  // ignore Zip file itself
  if (FilePath == ZipPath) {
    return;
  }

  // open file stream
  vector<std::byte> Buffer = getFileBytes(FilePath);

  // get relative path
  filesystem::path ZipRelativePath = FilePath.lexically_relative(OutputDir);

  string ZipFilePath = wstringToString(ZipRelativePath.wstring());

  // add file to Zip
  if (mz_zip_writer_add_mem(&Zip, ZipFilePath.c_str(), Buffer.data(), Buffer.size(), MZ_NO_COMPRESSION) == 0) {
    spdlog::error(L"Error adding file to zip: {}", FilePath.wstring());
    exit(1);
  }
}

void ParallaxGen::zipDirectory(const filesystem::path &DirPath, const filesystem::path &ZipPath) const {
  mz_zip_archive Zip;

  // init to 0
  memset(&Zip, 0, sizeof(Zip));

  // Check if file already exists and delete
  if (filesystem::exists(ZipPath)) {
    spdlog::info(L"Deleting existing output Zip file: {}", ZipPath.wstring());
    filesystem::remove(ZipPath);
  }

  // initialize file
  string ZipPathString = wstringToString(ZipPath);
  if (mz_zip_writer_init_file(&Zip, ZipPathString.c_str(), 0) == 0) {
    spdlog::critical(L"Error creating Zip file: {}", ZipPath.wstring());
    exit(1);
  }

  // add each file in directory to Zip
  for (const auto &Entry : filesystem::recursive_directory_iterator(DirPath)) {
    if (filesystem::is_regular_file(Entry.path())) {
      addFileToZip(Zip, Entry.path(), ZipPath);
    }
  }

  // finalize Zip
  if (mz_zip_writer_finalize_archive(&Zip) == 0) {
    spdlog::critical(L"Error finalizing Zip archive: {}", ZipPath.wstring());
    exit(1);
  }

  mz_zip_writer_end(&Zip);

  spdlog::info(L"Please import this file into your mod manager: {}", ZipPath.wstring());
}
