#pragma once

namespace PGLite {

/**
 * @brief The console commands of PGLite: pgblock puts the mesh of the reference selected in the console on the mesh
 * block list of the settings, as a rule for that one mesh, and pgallow takes that rule off again. Each says in the
 * console what it did. They change the settings only: the meshes follow at the next Apply in the menu or at the next
 * game start.
 *
 * The commands take over no command of the game's. The console menu's ExecuteCommand callback, which its Scaleform
 * delegate calls with the line typed, is wrapped: a line whose first word is a PGLite command runs here, and every
 * other line goes on to the callback that was there. A line the console menu does not run (a batch file's, one a plugin
 * compiles itself) never reaches PGLite, and the game's help lists the game's commands only.
 */
class ConsoleCommands {
public:
    /// @brief Wraps the callback, now and whenever the console menu opens with a delegate PGLite has not wrapped. At
    /// data load, once.
    static void install();
};

}
