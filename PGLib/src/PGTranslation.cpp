#include "PGTranslation.hpp"

#include "util/FileUtil.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

/** Mutable state of the loaded translation table. */
struct TranslationState {
    std::filesystem::path translationsDir;
    std::string currentLanguage = PGTranslation::fallbackLanguage;
    std::unordered_map<std::string, std::string> strings;
};

/** Held in a function-local static rather than in globals so construction order is well defined. */
TranslationState& translationState()
{
    static TranslationState state;
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
                                    std::string>& out)
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
                out[fullKey] = str;
            }
        }
    }
}

/**
 * @brief Merges <langCode>.json into the string table, overwriting keys that are already present
 */
void loadLanguage(const std::string& langCode)
{
    const auto translationFile = translationState().translationsDir / (langCode + ".json");
    if (!std::filesystem::exists(translationFile))
        return;

    nlohmann::json j;
    if (parseTranslationFile(translationFile, j))
        flattenJSON(j, "", translationState().strings);
}

std::string languageDisplayName(const std::filesystem::path& file)
{
    nlohmann::json j;
    if (parseTranslationFile(file, j) && j.contains("_language") && j["_language"].is_string())
        return j["_language"].get<std::string>();

    return { };
}

} // namespace

void PGTranslation::init(const std::filesystem::path& translationsDir,
                         const std::string& langCode)
{
    auto& state = translationState();
    state.translationsDir = translationsDir;
    state.currentLanguage = langCode.empty() ? fallbackLanguage : langCode;
    state.strings.clear();

    // English is always the base layer so that keys missing from the active translation still resolve.
    loadLanguage(fallbackLanguage);
    if (state.currentLanguage != fallbackLanguage)
        loadLanguage(state.currentLanguage);
}

std::string PGTranslation::tr(const std::string& key)
{
    const auto& strings = translationState().strings;
    const auto it = strings.find(key);
    if (it != strings.end())
        return it->second;

    // Neither the active language nor en.json provides this key: the translations folder is missing or broken. Show
    // the key itself so the problem is visible rather than masked by a hardcoded string.
    return key;
}

std::string PGTranslation::trFormat(const std::string& key,
                                    const std::string& argument)
{
    auto text = tr(key);
    const auto placeholder = text.find("%s");
    if (placeholder != std::string::npos)
        text.replace(placeholder, 2, argument);

    return text;
}

std::string PGTranslation::currentLanguage() { return translationState().currentLanguage; }

auto PGTranslation::availableLanguages() -> std::vector<Language>
{
    std::vector<Language> languages;

    const auto& translationsDir = translationState().translationsDir;
    if (translationsDir.empty() || !std::filesystem::exists(translationsDir))
        return languages;

    for (const auto& entry : std::filesystem::directory_iterator(translationsDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;

        languages.push_back({ .code = entry.path().stem().string(), .displayName = languageDisplayName(entry.path()) });
    }

    const auto sortName
        = [](const Language& language) { return language.displayName.empty() ? language.code : language.displayName; };
    std::ranges::sort(languages, [&sortName](const Language& a, const Language& b) {
        return boost::algorithm::ilexicographical_compare(sortName(a), sortName(b));
    });

    return languages;
}

std::vector<std::string> PGTranslation::allStrings()
{
    std::vector<std::string> strings;
    strings.reserve(translationState().strings.size());
    for (const auto& [key, value] : translationState().strings)
        strings.push_back(value);
    return strings;
}
