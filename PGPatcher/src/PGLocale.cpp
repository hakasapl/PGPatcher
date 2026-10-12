#include "PGLocale.hpp"

#include "PGTranslation.hpp"

#include <wx/uilocale.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

wxString languageDisplayName(const PGTranslation::Language& language)
{
    if (!language.displayName.empty())
        return wxString::FromUTF8(language.displayName);

    // Fall back to the wx language database (e.g. "de" -> "Deutsch")
    std::string canonical = language.code;
    std::ranges::replace(canonical, '-', '_');
    const auto* langInfo = wxUILocale::FindLanguageInfo(wxString::FromUTF8(canonical));
    if (langInfo && !langInfo->DescriptionNative.empty())
        return langInfo->DescriptionNative;

    return wxString::FromUTF8(language.code);
}

} // namespace

void PGLocale::init(const std::filesystem::path& translationsDir,
                    const std::string& langCode)
{
    PGTranslation::init(translationsDir, langCode);
}

wxString PGLocale::tr(const std::string& key) { return wxString::FromUTF8(PGTranslation::tr(key)); }

std::string PGLocale::currentLanguage() { return PGTranslation::currentLanguage(); }

auto PGLocale::availableLanguages() -> std::vector<Language>
{
    std::vector<Language> languages;
    for (const auto& language : PGTranslation::availableLanguages())
        languages.push_back({ .code = language.code, .displayName = languageDisplayName(language) });

    std::ranges::sort(languages,
                      [](const Language& a, const Language& b) { return a.displayName.CmpNoCase(b.displayName) < 0; });

    return languages;
}
