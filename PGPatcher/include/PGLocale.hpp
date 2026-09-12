#pragma once

#include <wx/string.h>

#include <filesystem>
#include <string>
#include <vector>

/**
 * @class PGLocale
 * @brief GUI localization system backed by JSON translation files
 *
 * Translation files live in the "translations" folder next to the executable, one file per language named by its
 * IETF/ISO language code (e.g. "en.json", "de.json", "pt-BR.json"). The schema is i18next-style nested JSON (the
 * de-facto standard JSON translation format, supported by Crowdin/Weblate/Lokalise and similar tools): nested objects
 * whose leaf string values are addressed by dot-separated keys, e.g. {"launcher": {"title": "..."}} is looked up as
 * "launcher.title". A reserved top-level "_language" key holds the language's native display name shown in the
 * language selector. Placeholders use printf-style formatting (e.g. %s) and are substituted with wxString::Format at
 * the call site.
 *
 * No user-facing string is hardcoded in C++. "en.json" is always loaded first as the fallback layer and the active
 * language is merged on top of it, so any key a translation lacks (or leaves empty) shows the English text. If a key
 * exists in neither file (which can only happen when the translations folder is missing or broken, i.e. a broken
 * install), the lookup key itself is shown so the problem is visible instead of silently masked.
 */
class PGLocale {
public:
    struct Language {
        std::string code; /** Language code, also the translation filename stem (e.g. "en") */
        wxString displayName; /** Native display name (e.g. "English", "Deutsch") */
    };

    /**
     * @brief Initializes the locale system and loads the translation for the given language code
     *
     * "en.json" is always loaded first; the requested language (file <langCode>.json) is merged on top of it. A
     * missing language file leaves the English strings in place.
     *
     * @param translationsDir Folder containing the translation JSON files
     * @param langCode Language code to load on top of English
     */
    static void init(const std::filesystem::path& translationsDir,
                     const std::string& langCode);

    /**
     * @brief Looks up a translated string by key
     *
     * @param key Dot-separated translation key (e.g. "launcher.title")
     * @return wxString Translated string, the English string if the active translation lacks the key, or the key
     * itself if no translation file provides it
     */
    [[nodiscard]] static auto tr(const std::string& key) -> wxString;

    /**
     * @brief Get the language code that is currently active
     */
    [[nodiscard]] static auto getCurrentLanguage() -> std::string;

    /**
     * @brief Lists the languages available in the translations folder (sorted by display name)
     */
    [[nodiscard]] static auto getAvailableLanguages() -> std::vector<Language>;
};

/**
 * @brief Shorthand for PGLocale::tr
 */
[[nodiscard]] inline auto PGTr(const std::string& key) -> wxString { return PGLocale::tr(key); }
