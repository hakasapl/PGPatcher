#pragma once

// MeshReloadFramework: reloads meshes in game after their bytes changed (a mod manager swap, a runtime patcher's new
// output), without a cell or save reload. Look the exports up with GetProcAddress on "MeshReloadFramework.dll".
//
// The reload calls are thread-safe and only queue a request: it runs on the main thread at the next frame, and requests
// that arrive meanwhile join the same pass. Each returns false when the game is not ready (no data loaded yet).
// MRF_RegisterReloadDoneCallback says when they are done. MRF_WatchMesh, MRF_WatchReference and MRF_WatchBase reload
// each time a loose file of what they name changes.

#include <cstdint>

// The C names are the exports' ABI.
// NOLINTBEGIN(readability-identifier-naming)
extern "C" {

/// @brief One model the loaded game shows (MRF_ForEachLoadedModel).
struct MRF_LoadedModel {
    /// @brief Lowercase, backslashes, under "meshes\".
    const char* path;
    /// @brief The RE::TESModel that names the path, nullptr for a path no record names (a facegen head, debris, LOD,
    /// the sky and water models, the first-person skeleton).
    const void* model;
    /// @brief The form that owns the model (a reference's base, an armor addon, an art object...), 0 for none.
    std::uint32_t formID;
    /// @brief Whether the path is the other file (`_0`/`_1`) of the weighted pair the model names.
    bool isWeightCounterpart;
};

/// @brief Everything that shows the mesh loads it again from its current bytes. `path` is relative to Data, with or
/// without the leading "meshes\".
using MRF_ReloadMeshFn = bool (*)(const char* path);
/// @brief The reference loads its 3D again (an actor rebuilds its parts, head and animation objects).
using MRF_ReloadReferenceFn = bool (*)(std::uint32_t refFormID);
/// @brief Every loaded reference of the base loads its 3D again, and so does every actor part that comes from it (an
/// armor, armor addon, weapon or light worn), every effect showing it (an art object) and every grass type of it.
using MRF_ReloadBaseFn = bool (*)(std::uint32_t baseFormID);
/// @brief Main thread only: calls back for each model the loaded game shows.
using MRF_ForEachLoadedModelFn = void (*)(void (*callback)(const MRF_LoadedModel& model,
                                                           void* context),
                                          void* context);

/// @brief Requests the framework is done with (MRF_RegisterReloadDoneCallback). The arrays live for the call.
struct MRF_ReloadDone {
    /// @brief The paths MRF_ReloadMesh (or a watched file's change) asked for, spelled as MRF_LoadedModel::path is,
    /// sorted.
    const char* const* paths;
    std::uint32_t pathCount;
    /// @brief The references MRF_ReloadReference asked for, sorted.
    const std::uint32_t* references;
    std::uint32_t referenceCount;
    /// @brief The forms MRF_ReloadBase asked for, sorted.
    const std::uint32_t* bases;
    std::uint32_t baseCount;
};

/// @brief Called on the main thread with requests that are done (MRF_RegisterReloadDoneCallbackFn).
using MRF_ReloadDoneCallback = void (*)(const MRF_ReloadDone& done,
                                        void* context);
/// @brief Thread-safe: calls back each time requests are done, a frame after the pass that took them ran, once what it
/// left for later reloaded too or was given up on (a head part the old head still held, a reference the model loader
/// was loading). A request asked for again before it was done is reported once. Every caller's requests are reported,
/// the console's and Papyrus's too: look for yours. A new game or a save load ends what was left for later, and reports
/// it. False for a null callback or one registered already with the context.
using MRF_RegisterReloadDoneCallbackFn = bool (*)(MRF_ReloadDoneCallback callback,
                                                  void* context);
/// @brief Thread-safe: the callback registered with the context is not called again, except by a report already under
/// way on the main thread when this is called from another thread. False when it was not registered.
using MRF_UnregisterReloadDoneCallbackFn = bool (*)(MRF_ReloadDoneCallback callback,
                                                    void* context);

/// @brief Thread-safe: reloads the mesh, as MRF_ReloadMesh does, each time its loose file under Data changes (written,
/// replaced or removed), once the file stopped changing. `path` as MRF_ReloadMesh takes it. The game loads only the
/// loose files it had at startup. False when no loose file is at the path (a file in an archive is not watched), or
/// when the game reads the path from an archive or from a stream a plugin registered instead. Watching a watched path
/// again changes nothing and returns true.
using MRF_WatchMeshFn = bool (*)(const char* path);
/// @brief Thread-safe: stops watching the path. False when it was not watched.
using MRF_UnwatchMeshFn = bool (*)(const char* path);
/// @brief Main thread only: reloads the reference, as MRF_ReloadReference does, each time a loose file of the models it
/// shows now changes, as MRF_WatchMesh watches one. False when the game reads none of them from a loose file.
using MRF_WatchReferenceFn = bool (*)(std::uint32_t refFormID);
/// @brief Thread-safe: stops watching for the reference. False when it was not watched.
using MRF_UnwatchReferenceFn = bool (*)(std::uint32_t refFormID);
/// @brief Main thread only: reloads what comes from the form, as MRF_ReloadBase does, each time a loose file of the
/// models that reload covers now changes. False when the game reads none of them from a loose file.
using MRF_WatchBaseFn = bool (*)(std::uint32_t baseFormID);
/// @brief Thread-safe: stops watching for the form. False when it was not watched.
using MRF_UnwatchBaseFn = bool (*)(std::uint32_t baseFormID);

/// @brief A watch provider's (MRF_RegisterWatchProviderFn): a watch began for a path the game reads from a stream the
/// provider registered. True when the provider watches what the stream is made of and will call
/// MRF_NotifyStreamChanged once the stream has new bytes; false declines the watch. Asking for a path it watches
/// already changes nothing. Called on the thread that watches (any for a path, the main thread for a reference or a
/// base), with no lock of MRF's held.
using MRF_WatchProviderWatchFn = bool (*)(const char* path,
                                          void* context);
/// @brief A watch provider's: no watch is left for a path it took; it can stop watching what the stream is made of.
/// Called on the thread that unwatches, with no lock of MRF's held.
using MRF_WatchProviderUnwatchFn = void (*)(const char* path,
                                            void* context);
/// @brief Thread-safe: a plugin that serves paths with streams of its own (a runtime patcher) takes the watches of
/// those paths: a watch of a path the game reads from a stream whose class is in the plugin's module asks the provider,
/// found by the module its `watch` is in, instead of polling the loose file, which the game does not read. One provider
/// per module. False for a null `watch` or a module that has one.
using MRF_RegisterWatchProviderFn = bool (*)(MRF_WatchProviderWatchFn watch,
                                             MRF_WatchProviderUnwatchFn unwatch,
                                             void* context);
/// @brief Thread-safe: unregisters the provider. The watches it took end, without its `unwatch`. False when it was not
/// registered.
using MRF_UnregisterWatchProviderFn = bool (*)(MRF_WatchProviderWatchFn watch,
                                               void* context);
/// @brief Thread-safe: the stream of the path has new bytes (a provider registered them): reloads what watches the
/// path, as a watched loose file's change does, and reports it to the done callbacks. False when nothing watches the
/// path.
using MRF_NotifyStreamChangedFn = bool (*)(const char* path);

/// @brief Called on MRF's watcher thread when a file MRF_WatchFile watches changed, with the path spelled as MRF
/// keeps it: lowercase, backslashes, relative to Data.
using MRF_FileChangedFn = void (*)(const char* path,
                                   void* context);
/// @brief Thread-safe: calls back each time the file under Data changes (written, replaced, created or removed), once
/// it stopped changing, as a watch polls it; nothing is reloaded. For a provider watching what its streams are made of,
/// meshes or textures alike. The file need not exist; read through the file system the game sees (a mod manager's
/// virtual one). False for a null callback or an empty path; true again, changing nothing, for one watching already.
using MRF_WatchFileFn = bool (*)(const char* path,
                                 MRF_FileChangedFn callback,
                                 void* context);
/// @brief Thread-safe: stops that callback for the file. False when it was not watching it.
using MRF_UnwatchFileFn = bool (*)(const char* path,
                                   MRF_FileChangedFn callback,
                                   void* context);
}
// NOLINTEND(readability-identifier-naming)
