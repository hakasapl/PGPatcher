#include "ParallaxGenWarnings.hpp"

#include <spdlog/spdlog.h>

using namespace std;

// statics
ParallaxGenDirectory *ParallaxGenWarnings::PGD = nullptr;
const std::unordered_map<std::wstring, int> *ParallaxGenWarnings::ModPriority = nullptr;

unordered_set<pair<wstring, wstring>, ParallaxGenWarnings::PairHash> ParallaxGenWarnings::MismatchWarnTracker;
mutex ParallaxGenWarnings::MismatchWarnTrackerMutex;
unordered_set<pair<wstring, wstring>, ParallaxGenWarnings::PairHash> ParallaxGenWarnings::MismatchWarnDebugTracker;
mutex ParallaxGenWarnings::MismatchWarnDebugTrackerMutex;

unordered_set<pair<wstring, wstring>, ParallaxGenWarnings::PairHash> ParallaxGenWarnings::MeshWarnTracker;
mutex ParallaxGenWarnings::MeshWarnTrackerMutex;
unordered_set<pair<wstring, wstring>, ParallaxGenWarnings::PairHash> ParallaxGenWarnings::MeshWarnDebugTracker;
mutex ParallaxGenWarnings::MeshWarnDebugTrackerMutex;

void ParallaxGenWarnings::init(ParallaxGenDirectory *PGD, const unordered_map<wstring, int> *ModPriority) {
  ParallaxGenWarnings::PGD = PGD;
  ParallaxGenWarnings::ModPriority = ModPriority;

  MismatchWarnTracker.clear();
  MismatchWarnDebugTracker.clear();
  MeshWarnTracker.clear();
  MeshWarnDebugTracker.clear();
}

void ParallaxGenWarnings::mismatchWarn(const wstring &MatchedPath, const wstring &BaseTex) {
  // construct key
  auto MatchedPathMod = PGD->getMod(MatchedPath);
  auto BaseTexMod = PGD->getMod(BaseTex);

  if (MatchedPathMod.empty() || BaseTexMod.empty()) {
    return;
  }

  if (MatchedPathMod == BaseTexMod) {
    return;
  }

  auto Key = make_pair(MatchedPathMod, BaseTexMod);

  // Issue debug log
  {
    auto KeyDebug = make_pair(MatchedPath, BaseTex);
    const lock_guard<mutex> Lock(MismatchWarnDebugTrackerMutex);
    if (MismatchWarnDebugTracker.find(KeyDebug) != MismatchWarnDebugTracker.end()) {
      return;
    }

    MismatchWarnDebugTracker.insert(KeyDebug);

    spdlog::debug(L"[Potential Texture Mismatch] Matched path {} from mod {} does not come from the same diffuse or normal {} from mod {}",
                  MatchedPath, MatchedPathMod, BaseTex, BaseTexMod);
  }

  // check if mod warning was already issued
  {
    const lock_guard<mutex> Lock(MismatchWarnTrackerMutex);
    if (MismatchWarnTracker.find(Key) != MismatchWarnTracker.end()) {
      return;
    }

    // add to tracker if not
    MismatchWarnTracker.insert(Key);
  }

  // log warning
  spdlog::warn(L"[Potential Texture Mismatch] Mod \"{}\" assets were used with diffuse or normal from mod \"{}\". Please verify that "
               L"this is intended.",
               MatchedPathMod, BaseTexMod);
}

void ParallaxGenWarnings::meshWarn(const wstring &MatchedPath, const wstring &NIFPath) {
  // construct key
  auto MatchedPathMod = PGD->getMod(MatchedPath);
  auto NIFPathMod = PGD->getMod(NIFPath);

  if (MatchedPathMod.empty() || NIFPathMod.empty()) {
    return;
  }

  if (MatchedPathMod == NIFPathMod) {
    return;
  }

  int Priority = 0;
  if (ModPriority != nullptr && ModPriority->find(NIFPathMod) != ModPriority->end()) {
    Priority = ModPriority->at(NIFPathMod);
  }

  if (Priority < 0) {
    return;
  }

  auto Key = make_pair(MatchedPathMod, NIFPathMod);

  // Issue debug log
  {
    auto KeyDebug = make_pair(MatchedPath, NIFPath);
    const lock_guard<mutex> Lock(MeshWarnDebugTrackerMutex);
    if (MeshWarnDebugTracker.find(KeyDebug) != MeshWarnDebugTracker.end()) {
      return;
    }

    MeshWarnDebugTracker.insert(KeyDebug);

    spdlog::debug(L"[Potential Mesh Mismatch] Matched path {} from mod {} were used on mesh {} from mod {}", MatchedPath, MatchedPathMod, NIFPath,
                  NIFPathMod);
  }

  // check if warning was already issued
  {
    const lock_guard<mutex> Lock(MeshWarnTrackerMutex);
    if (MeshWarnTracker.find(Key) != MeshWarnTracker.end()) {
      return;
    }

    // add to tracker if not
    MeshWarnTracker.insert(Key);
  }

  // log warning
  spdlog::warn(
      L"[Potential Mesh Mismatch] Mod \"{}\" assets were used on meshes from mod \"{}\". Please verify that this is intended.",
      MatchedPathMod, NIFPathMod);
}
