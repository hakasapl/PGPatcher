#pragma once

namespace PGLite {

/**
 * @brief The console commands of PGLite: pgblock puts the mesh of the reference selected in the console on the mesh
 * block list of the settings, as a rule for that one mesh, and pgallow takes that rule off again. Each says in the
 * console what it did. They change the settings only: the meshes follow at the next Apply in the menu or at the next
 * game start.
 *
 * The game's console table has no free entry, so each command takes over the entry of a command that only the
 * PlayStation 3 build of the game used, as the console commands of other SKSE plugins do. An entry another plugin took
 * over first no longer has its original name and is passed over.
 */
class ConsoleCommands {
public:
    /// @brief Installs both commands. At data load, once.
    static void install();
};

}
