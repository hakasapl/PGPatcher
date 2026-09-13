#include "PGLocale.hpp"

#include "util/FileUtil.hpp"

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <wx/uilocale.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

/** Language whose translation file is always loaded underneath the active language */
constexpr const char* fallbackLanguage = "en";

/** Mutable state of the loaded translation table */
struct LocaleState {
    std::filesystem::path translationsDir;
    std::string currentLanguage = fallbackLanguage;
    std::unordered_map<std::string, wxString> strings;
};

/** Held in a function-local static rather than in globals so construction order is well defined */
LocaleState& localeState()
{
    static LocaleState state;
    return state;
}

bool parseTranslationFile(const std::filesystem::path& file,
                          nlohmann::json& j)
{
    try {
        j = nlohmann::json::parse(FileUtil::fileBytes(file));
    } catch (const std::exception&) {
        return false;
    }

    return j.is_object();
}

void flattenJSON(const nlohmann::json& j,
                 const std::string& prefix,
                 std::unordered_map<std::string,
                                    wxString>& out)
{
    for (const auto& [key, value] : j.items()) {
        if (prefix.empty() && key.starts_with('_')) {
            // Reserved metadata keys (e.g. "_language").
            continue;
        }

        std::string fullKey = prefix;
        if (!fullKey.empty())
            fullKey += '.';
        fullKey += key;

        if (value.is_object()) {
            flattenJSON(value, fullKey, out);
        } else if (value.is_string()) {
            const auto str = value.get<std::string>();
            if (!str.empty()) {
                // An empty value keeps whatever the fallback language already provided for this key.
                out[fullKey] = wxString::FromUTF8(str);
            }
        }
    }
}

/**
 * @brief Merges <langCode>.json into the string table, overwriting keys that are already present
 */
void loadLanguage(const std::string& langCode)
{
    const auto translationFile = localeState().translationsDir / (langCode + ".json");
    if (!std::filesystem::exists(translationFile))
        return;

    nlohmann::json j;
    if (parseTranslationFile(translationFile, j))
        flattenJSON(j, "", localeState().strings);
}

wxString languageDisplayName(const std::filesystem::path& file,
                             const std::string& code)
{
    nlohmann::json j;
    if (parseTranslationFile(file, j) && j.contains("_language") && j["_language"].is_string())
        return wxString::FromUTF8(j["_language"].get<std::string>());

    // Fall back to the wx language database (e.g. "de" -> "Deutsch")
    std::string canonical = code;
    std::ranges::replace(canonical, '-', '_');
    const auto* langInfo = wxUILocale::FindLanguageInfo(wxString::FromUTF8(canonical));
    if (langInfo != nullptr && !langInfo->DescriptionNative.empty())
        return langInfo->DescriptionNative;

    return wxString::FromUTF8(code);
}

} // namespace

void PGLocale::init(const std::filesystem::path& translationsDir,
                    const std::string& langCode)
{
    auto& state = localeState();
    state.translationsDir = translationsDir;
    state.currentLanguage = langCode.empty() ? fallbackLanguage : langCode;
    state.strings.clear();

    // English is always the base layer so that keys missing from the active translation still resolve.
    loadLanguage(fallbackLanguage);
    if (state.currentLanguage != fallbackLanguage)
        loadLanguage(state.currentLanguage);
}

wxString PGLocale::tr(const std::string& key)
{
    const auto& strings = localeState().strings;
    const auto it = strings.find(key);
    if (it != strings.end())
        return it->second;

    // Neither the active language nor en.json provides this key: the translations folder is missing or broken. Show.
    // The key itself so the problem is visible rather than masked by a hardcoded string.
    return wxString::FromUTF8(key);
}

std::string PGLocale::currentLanguage() { return localeState().currentLanguage; }

auto PGLocale::availableLanguages() -> std::vector<Language>
{
    std::vector<Language> languages;

    const auto& translationsDir = localeState().translationsDir;
    if (translationsDir.empty() || !std::filesystem::exists(translationsDir))
        return languages;

    for (const auto& entry : std::filesystem::directory_iterator(translationsDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;

        const auto code = entry.path().stem().string();
        languages.push_back({ .code = code, .displayName = languageDisplayName(entry.path(), code) });
    }

    std::ranges::sort(languages,
                      [](const Language& a, const Language& b) { return a.displayName.CmpNoCase(b.displayName) < 0; });

    return languages;
}
