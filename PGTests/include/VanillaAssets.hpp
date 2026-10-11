#pragma once

#include <filesystem>

/**
 * @brief Locates a vanilla Skyrim Special Edition install for the tests that read real game assets.
 *
 * The folder comes from the PG_TEST_SKYRIM_DIR environment variable, or the default Steam location. The tests that need
 * it skip themselves when it (or the game's plugins.txt in the local AppData folder) is missing, so they run on a
 * developer machine with the game and are skipped in CI.
 */
namespace TestUtil::VanillaAssets {
/// @brief Root folder of the game, empty if none is configured.
std::filesystem::path gameDir();

/// @brief Local AppData folder of the game (holds plugins.txt), empty if unknown.
std::filesystem::path appDataDir();

/// @brief Whether a usable install was found.
bool isAvailable();

/// @brief Whether PG_TEST_VANILLA_FULL is set, which makes the vanilla tests read every asset instead of a sample.
bool isFullRunRequested();
}
