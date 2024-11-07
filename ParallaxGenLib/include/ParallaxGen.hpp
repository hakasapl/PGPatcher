#pragma once

#include <NifFile.hpp>
#include <filesystem>
#include <miniz.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_set>

#include "NIFUtil.hpp"
#include "ParallaxGenConfig.hpp"
#include "ParallaxGenD3D.hpp"
#include "ParallaxGenDirectory.hpp"
#include "ParallaxGenTask.hpp"
#include "patchers/PatcherShader.hpp"

#define MESHES_LENGTH 7

class ParallaxGen {
private:
  std::filesystem::path OutputDir; // ParallaxGen output directory

  // Dependency objects
  ParallaxGenDirectory *PGD;
  ParallaxGenConfig *PGC;
  ParallaxGenD3D *PGD3D;

  // sort blocks enabled, optimize disabled (for now)
  nifly::NifSaveOptions NIFSaveOptions = {false, true};

  // store CLI arguments
  bool UpgradeShaders;

  struct PatcherResult {
    std::wstring MatchedPath;
    bool ShouldApply;
  };

  // Mutex lock for shader upgrades
  std::mutex UpgradeMutex;

  struct ShapeKey {
    std::wstring NIFPath;
    int ShapeIndex;

    // Equality operator to compare two ShapeKey objects
    auto operator==(const ShapeKey &Other) const -> bool {
      return NIFPath == Other.NIFPath && ShapeIndex == Other.ShapeIndex;
    }
  };

  // Define a hash function for ShapeKey
  struct ShapeKeyHash {
    auto operator()(const ShapeKey &Key) const -> size_t {
      // Hash the NIFPath and ShapeIndex individually
      std::size_t H1 = std::hash<std::wstring>{}(Key.NIFPath);
      std::size_t H2 = std::hash<int>{}(Key.ShapeIndex);

      return H1 ^ (H2 << 1); // shifting to reduce collisions
    }
  };

  struct AllowedShader {
    NIFUtil::ShapeShader Shader;
    std::wstring MatchedPath;
    std::unordered_set<NIFUtil::TextureSlots> MatchedFrom;
  };
  std::unordered_map<ShapeKey, std::vector<std::tuple<std::wstring, NIFUtil::ShapeShader, PatcherShader::PatcherMatch>>,
                     ShapeKeyHash>
      AllowedShadersCache;
  std::mutex AllowedShadersCacheMutex;

public:
  //
  // The following methods are called from main.cpp and are public facing
  //

  // constructor
  ParallaxGen(std::filesystem::path OutputDir, ParallaxGenDirectory *PGD, ParallaxGenConfig *PGC, ParallaxGenD3D *PGD3D,
              const bool &OptimizeMeshes = false, const bool &UpgradeShaders = false);
  // enables parallax on relevant meshes
  void
  patchMeshes(const std::vector<std::function<std::unique_ptr<PatcherShader>(std::filesystem::path, nifly::NifFile *)>>
                  &Patchers,
              const bool &MultiThread = true, const bool &PatchPlugin = true);
  // Dry run for finding potential matches (used with mod manager integration)
  [[nodiscard]] auto findModConflicts(
      const std::vector<std::function<std::unique_ptr<PatcherShader>(std::filesystem::path, nifly::NifFile *)>>
          &Patchers,
      const bool &MultiThread = true,
      const bool &PatchPlugin = true) -> std::unordered_map<std::wstring, std::tuple<std::set<NIFUtil::ShapeShader>, std::unordered_set<std::wstring>>>;
  // zips all meshes and removes originals
  void zipMeshes() const;
  // deletes generated meshes
  void deleteMeshes() const;
  // deletes entire output folder
  void deleteOutputDir() const;
  // get output zip name
  [[nodiscard]] static auto getOutputZipName() -> std::filesystem::path;
  // get diff json name
  [[nodiscard]] static auto getDiffJSONName() -> std::filesystem::path;

private:
  // thread safe JSON update
  std::mutex JSONUpdateMutex;
  void threadSafeJSONUpdate(const std::function<void(nlohmann::json &)> &Operation, nlohmann::json &DiffJSON);

  // upgrades a height map to complex material
  auto convertHeightMapToComplexMaterial(const std::filesystem::path &HeightMap,
                                         std::wstring &NewCMMap) -> ParallaxGenTask::PGResult;

  // processes a NIF file (enable parallax if needed)
  auto
  processNIF(const std::vector<std::function<std::unique_ptr<PatcherShader>(std::filesystem::path, nifly::NifFile *)>>
                 &Patchers,
             const std::filesystem::path &NIFFile, nlohmann::json *DiffJSON, const bool &PatchPlugin = true,
             const bool &Dry = false,
             std::unordered_map<std::wstring, std::tuple<std::set<NIFUtil::ShapeShader>, std::unordered_set<std::wstring>>> *ConflictMods = nullptr,
             std::mutex *ConflictModsMutex = nullptr) -> ParallaxGenTask::PGResult;

  // processes a shape within a NIF file
  auto processShape(const std::filesystem::path &NIFPath, nifly::NifFile &NIF, nifly::NiShape *NIFShape,
                    const int &ShapeIndex, const std::vector<std::unique_ptr<PatcherShader>> &Patchers, bool &ShapeModified,
                    bool &ShapeDeleted, NIFUtil::ShapeShader &ShaderApplied, const bool &Dry = false,
                    std::unordered_map<std::wstring, std::tuple<std::set<NIFUtil::ShapeShader>, std::unordered_set<std::wstring>>> *ConflictMods = nullptr,
                    std::mutex *ConflictModsMutex = nullptr) -> ParallaxGenTask::PGResult;

  // Zip methods
  void addFileToZip(mz_zip_archive &Zip, const std::filesystem::path &FilePath,
                    const std::filesystem::path &ZipPath) const;

  void zipDirectory(const std::filesystem::path &DirPath, const std::filesystem::path &ZipPath) const;
};
