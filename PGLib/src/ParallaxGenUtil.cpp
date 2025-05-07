#include "ParallaxGenUtil.hpp"

#include <DirectXTex.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/locale.hpp>
#include <fstream>
#include <iostream>
#include <tchar.h>
#include <tlhelp32.h>
#include <windows.h>
#include <wingdi.h>
#include <winnt.h>

using namespace std;
namespace ParallaxGenUtil {

constexpr unsigned ASCII_UPPER_BOUND = 127;

auto windows1252toUTF16(const std::string& str) -> std::wstring
{
    return boost::locale::conv::to_utf<wchar_t>(str, "windows-1252");
}

auto utf16toWindows1252(const std::wstring& str) -> std::string
{
    return boost::locale::conv::from_utf<wchar_t>(str, "windows-1252");
}

auto asciitoUTF16(const std::string& str) -> std::wstring
{
    return boost::locale::conv::to_utf<wchar_t>(str, "US-ASCII");
}

auto utf16toASCII(const std::wstring& str) -> std::string
{
    return boost::locale::conv::from_utf<wchar_t>(str, "US-ASCII");
}

auto utf8VectorToUTF16(const vector<string>& vec) -> vector<wstring>
{
    vector<wstring> out;
    out.reserve(vec.size());
    for (const auto& item : vec) {
        out.push_back(utf8toUTF16(item));
    }

    return out;
}

auto utf16VectorToUTF8(const vector<wstring>& vec) -> vector<string>
{
    vector<string> out;
    out.reserve(vec.size());
    for (const auto& item : vec) {
        out.push_back(utf16toUTF8(item));
    }

    return out;
}

auto windows1252VectorToUTF16(const vector<string>& vec) -> vector<wstring>
{
    vector<wstring> out;
    out.reserve(vec.size());
    for (const auto& item : vec) {
        out.push_back(windows1252toUTF16(item));
    }

    return out;
}

auto utf16VectorToWindows1252(const vector<wstring>& vec) -> vector<string>
{
    vector<string> out;
    out.reserve(vec.size());
    for (const auto& item : vec) {
        out.push_back(utf16toWindows1252(item));
    }

    return out;
}

auto asciiVectorToUTF16(const vector<string>& vec) -> vector<wstring>
{
    vector<wstring> out;
    out.reserve(vec.size());
    for (const auto& item : vec) {
        out.push_back(asciitoUTF16(item));
    }

    return out;
}

auto utf16VectorToASCII(const vector<wstring>& vec) -> vector<string>
{
    vector<string> out;
    out.reserve(vec.size());
    for (const auto& item : vec) {
        out.push_back(utf16toASCII(item));
    }

    return out;
}

auto toLowerASCII(const std::wstring& str) -> std::wstring { return boost::to_lower_copy(str, std::locale::classic()); }

auto utf8toUTF16(const string& str) -> wstring
{
    // Just return empty string if empty
    if (str.empty()) {
        return {};
    }

    // Convert string > wstring
    const int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), nullptr, 0);
    std::wstring wStr(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.length(), wStr.data(), sizeNeeded);

    return wStr;
}

auto utf16toUTF8(const wstring& wStr) -> string
{
    // Just return empty string if empty
    if (wStr.empty()) {
        return {};
    }

    // Convert wstring > string
    const int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wStr.data(), (int)wStr.size(), nullptr, 0, nullptr, nullptr);
    string str(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wStr.data(), (int)wStr.size(), str.data(), sizeNeeded, nullptr, nullptr);

    return str;
}

auto containsOnlyAscii(const std::string& str) -> bool
{
    return std::ranges::all_of(str, [](char wc) { return wc <= ASCII_UPPER_BOUND; });
}

auto containsOnlyAscii(const std::wstring& str) -> bool
{
    return std::ranges::all_of(str, [](wchar_t wc) { return wc <= ASCII_UPPER_BOUND; });
}

auto getFileBytes(const filesystem::path& filePath) -> vector<std::byte>
{
    ifstream inputFile(filePath, ios::binary | ios::ate);
    if (!inputFile.is_open()) {
        // Unable to open file
        return {};
    }

    auto length = inputFile.tellg();
    if (length == -1) {
        // Unable to find length
        inputFile.close();
        return {};
    }

    inputFile.seekg(0, ios::beg);

    // Make a buffer of the exact size of the file and read the data into it.
    vector<std::byte> buffer(length);
    inputFile.read(
        reinterpret_cast<char*>(buffer.data()), length); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

    inputFile.close();

    return buffer;
}

auto getThreadID() -> string
{
    // Get the current thread ID
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

auto getJSON(const std::filesystem::path& filePath, nlohmann::json& json) -> bool
{
    ifstream inputFile(filePath);
    if (!inputFile.is_open()) {
        // Unable to open file
        return false;
    }

    try {
        inputFile >> json;
    } catch (...) {
        // Handle JSON parsing error
        return false;
    }

    inputFile.close();
    return true;
}

auto saveJSON(const std::filesystem::path& filePath, const nlohmann::json& json, const bool& readable) -> bool
{
    ofstream outputFile(filePath, ios::binary);
    if (!outputFile.is_open()) {
        // Unable to open file
        return false;
    }

    if (readable) {
        outputFile << json.dump(2, ' ', false, nlohmann::detail::error_handler_t::replace);
    } else {
        outputFile << json.dump(-1, ' ', false, nlohmann::detail::error_handler_t::replace);
    }

    outputFile.close();
    return true;
}

auto checkIfStringInJSONArray(const nlohmann::json& json, const string& str) -> bool
{
    if (json.is_array()) {
        for (const auto& item : json) {
            if (item.is_string() && item.get<string>() == str) {
                return true;
            }
        }
    }
    return false;
}

auto isProcessRunning(const std::wstring& processName) -> bool
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(hSnapshot, &pe) != 0) {
        if (_wcsicmp(pe.szExeFile, processName.c_str()) // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            == 0) {
            CloseHandle(hSnapshot);
            return true;
        }

        while (Process32NextW(hSnapshot, &pe) != 0) {
            if (_wcsicmp(
                    pe.szExeFile, processName.c_str()) // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
                == 0) {
                CloseHandle(hSnapshot);
                return true;
            }
        }
    }

    CloseHandle(hSnapshot);
    return false;
}

auto execCommand(const string& cmd) -> vector<string>
{
    static constexpr auto BUFFER_SIZE = 1024;
    array<char, BUFFER_SIZE> buffer {};
    vector<string> result;

    const string runCmd = "cmd /C \"" + cmd + "\"";

    const unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(runCmd.c_str(), "r"), _pclose);
    if (!pipe) {
        throw std::runtime_error("Failed to run command");
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result.emplace_back(buffer.data());
        // Optionally trim newlines:
        if (!result.back().empty() && result.back().back() == '\n') {
            result.back().pop_back();
            if (!result.back().empty() && result.back().back() == '\r') {
                result.back().pop_back(); // Windows CRLF
            }
        }
    }

    return result;
}

void insertWithRuleOrThrow(
    std::vector<std::string>& list, const std::string& modA, const std::string& modB, bool before)
{
    auto itA = std::ranges::find(list, modA);
    auto itB = std::ranges::find(list, modB);

    const bool hasA = itA != list.end();
    const bool hasB = itB != list.end();

    if (hasA && hasB) {
        auto indexA = std::distance(list.begin(), itA);
        auto indexB = std::distance(list.begin(), itB);

        if (before && indexA > indexB) {
            // move A before B
            const string& mod = *itA;
            list.erase(itA);
            itB = std::ranges::find(list, modB); // re-locate B
            list.insert(itB, mod);
        } else if (!before && indexA < indexB) {
            // move A after B
            const string& mod = *itA;
            list.erase(itA);
            itB = std::ranges::find(list, modB); // re-locate B
            list.insert(itB + 1, mod);
        }
        return;
    }

    // If only one is present
    if (before) {
        if (!hasA && hasB) {
            list.insert(itB, modA);
        } else if (hasA && !hasB) {
            list.push_back(modB);
        } else if (!hasA && !hasB) {
            list.push_back(modA);
            list.push_back(modB);
        }
    } else { // after
        if (!hasA && hasB) {
            list.insert(itB + 1, modA);
        } else if (hasA && !hasB) {
            list.push_back(modB);
        } else if (!hasA && !hasB) {
            list.push_back(modB);
            list.push_back(modA);
        }
    }
}

} // namespace ParallaxGenUtil
