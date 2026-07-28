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
 * Every lookup carries a hardcoded English default which is returned when the key is missing from the active
 * translation (or when no translation file is loaded at all), so English is the built-in fallback language.
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
     * @param translationsDir Folder containing the translation JSON files
     * @param langCode Language code to load (file <langCode>.json); missing files fall back to hardcoded English
     */
    static void init(const std::filesystem::path& translationsDir,
                     const std::string& langCode);

    /**
     * @brief Looks up a translated string by key
     *
     * @param key Dot-separated translation key (e.g. "launcher.title")
     * @param defaultValue Hardcoded English fallback used when the key is missing from the active translation
     * @return wxString Translated string, or the fallback
     */
    [[nodiscard]] static auto tr(const std::string& key,
                                 const char* defaultValue) -> wxString;

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
[[nodiscard]] inline auto PGTr(const std::string& key,
                               const char* defaultValue) -> wxString
{
    return PGLocale::tr(key, defaultValue);
}
