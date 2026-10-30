#include "PCH.h"

#include "Menu.hpp"
#include "Runtime.hpp"

// The functions other SKSE plugins find by name, so that a plugin like Risa's All In One Menu can open and close the
// menu with its own keys: it turns the menu key of PGLite.ini off for the session and calls these instead. Their names
// are the ones those plugins look for, not this repository's style. Any thread: the menu applies the requests in its
// next frame.
extern "C" {

/// @brief Asks for the menu to open. Returns whether the request is taken: false before the game data is loaded,
/// while PGLite is disabled or still loading, and during a loading screen.
__declspec(dllexport) bool PGLite_Open() // NOLINT(readability-identifier-naming)
{
    auto* const menu = PGLite::Runtime::instance().menu();
    return menu != nullptr && menu->requestOpen();
}

/// @brief Asks for the menu to close; a closed menu stays closed.
__declspec(dllexport) void PGLite_Close() // NOLINT(readability-identifier-naming)
{
    if (auto* const menu = PGLite::Runtime::instance().menu())
        menu->requestClose();
}

__declspec(dllexport) bool PGLite_IsMenuOpen() // NOLINT(readability-identifier-naming)
{
    const auto* const menu = PGLite::Runtime::instance().menu();
    return menu != nullptr && menu->isOpen();
}

/// @brief Turns the menu key of PGLite.ini on or off for the session.
__declspec(dllexport) void PGLite_SetHotkeyEnabled(bool isEnabled) // NOLINT(readability-identifier-naming)
{
    PGLite::Runtime::instance().setMenuKeyEnabled(isEnabled);
}
}
