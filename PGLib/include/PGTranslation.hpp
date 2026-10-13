#pragma once

#include <filesystem>
#include <string>
#include <vector>

/**
 * @class PGTranslation
 * @brief The user-facing strings of the programs, from JSON translation files
 *
 * Translation files live in a "translations" folder, one file per language named by its IETF/ISO language code
 * (e.g. "en.json", "de.json", "pt-BR.json"). The schema is i18next-style nested JSON (the de-facto standard JSON
 * translation format, supported by Crowdin/Weblate/Lokalise and similar tools): nested objects whose leaf string
 * values are addressed by dot-separated keys, e.g. {"launcher": {"title": "..."}} is looked up as "launcher.title". A
 * reserved top-level "_language" key holds the language's native display name shown in the language selector.
 * Placeholders use printf-style formatting (e.g. %s) and are substituted at the call site.
 *
 * No user-facing string is hardcoded in C++. "en.json" is always loaded first as the fallback layer and the active
 * language is merged on top of it, so any key a translation lacks (or leaves empty) shows the English text. If a key
 * exists in neither file (which can only happen when the translations folder is missing or broken, i.e. a broken
 * install), the lookup key itself is shown so the problem is visible instead of silently masked.
 *
 * The strings are UTF-8. The GUI layers convert (wxString::FromUTF8 offline, Dear ImGui takes UTF-8 as is). The
 * table is one per process and not guarded: init() belongs to the GUI thread, and tr() may run on any thread while
 * no init() runs (the PGLite runtime translates its notices on its own threads).
 */
class PGTranslation {
public:
    struct Language {
        std::string code; /** Language code, also the translation filename stem (e.g. "en") */
        std::string displayName; /** Native display name from the file's "_language", empty when the file has none */
    };

    /// @brief The language whose file is always loaded underneath the active language.
    static constexpr const char* fallbackLanguage = "en";

    /**
     * @brief Loads the translation for the given language code
     *
     * "en.json" is always loaded first; the requested language (file <langCode>.json) is merged on top of it. A
     * missing language file leaves the English strings in place.
     *
     * @param translationsDir Folder containing the translation JSON files
     * @param langCode Language code to load on top of English; empty means English
     */
    static void init(const std::filesystem::path& translationsDir,
                     const std::string& langCode);

    /**
     * @brief Looks up a translated string by key
     *
     * @param key Dot-separated translation key (e.g. "launcher.title")
     * @return The translated string, the English string if the active translation lacks the key, or the key itself
     * if no translation file provides it
     */
    [[nodiscard]] static std::string tr(const std::string& key);

    /**
     * @brief The string of a key with its first "%s" placeholder replaced, for the strings that carry one argument.
     *
     * @param key Dot-separated translation key
     * @param argument UTF-8 text that replaces the placeholder
     * @return The string as tr() returns it, with the placeholder replaced when the string has one
     */
    [[nodiscard]] static std::string trFormat(const std::string& key,
                                              const std::string& argument);

    /// @brief The language code that is currently active.
    [[nodiscard]] static std::string currentLanguage();

    /// @brief The languages available in the translations folder, sorted by display name (the code when there is none).
    [[nodiscard]] static std::vector<Language> availableLanguages();

    /// @brief Every string of the loaded table, for a GUI that builds its font glyph ranges from them.
    [[nodiscard]] static std::vector<std::string> allStrings();
};
