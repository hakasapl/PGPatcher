#pragma once

#include "util/ParallaxGenUtil.hpp"

#include <cstddef>
#include <nlohmann/json.hpp>

#include <mutex>
#include <string>

class PGDiag {
private:
    static nlohmann::json s_diagJSON;
    static std::mutex s_diagJSONMutex;
    static bool s_enabled;

    thread_local static std::vector<std::string> s_ptrPrefixStack;

public:
    // Scoped prefix class
    class Prefix {
    public:
        static auto resolvePrefix() -> nlohmann::json*;

        explicit Prefix(const std::wstring& prefix, const nlohmann::json::value_t& type);
        explicit Prefix(const std::string& prefix, const nlohmann::json::value_t& type);
        ~Prefix();
        Prefix(const Prefix&) = delete;
        auto operator=(const Prefix&) -> Prefix& = delete;
        Prefix(Prefix&&) = delete;
        auto operator=(Prefix&&) -> Prefix& = delete;
    };

    static void init();
    static auto getJSON() -> nlohmann::json;
    static auto isEnabled() -> bool;

    // yes, this is necessary and cannot be done using template functions because this method needs to be exported from
    // DLL for use in EXEs
    static void insert(const std::wstring& key, const std::wstring& value);
    static void insert(const std::wstring& key, const wchar_t* value);
    static void insert(const std::wstring& key, const std::string& value);
    static void insert(const std::wstring& key, const char* value);
    static void insert(const std::wstring& key, const int& value);
    static void insert(const std::wstring& key, const unsigned int& value);
    static void insert(const std::wstring& key, const size_t& value);
    static void insert(const std::wstring& key, const bool& value);
    static void insert(const std::wstring& key, const float& value);
    static void insert(const std::wstring& key, const nlohmann::json& value);

    static void insert(const wchar_t* key, const std::wstring& value);
    static void insert(const wchar_t* key, const wchar_t* value);
    static void insert(const wchar_t* key, const std::string& value);
    static void insert(const wchar_t* key, const char* value);
    static void insert(const wchar_t* key, const int& value);
    static void insert(const wchar_t* key, const unsigned int& value);
    static void insert(const wchar_t* key, const size_t& value);
    static void insert(const wchar_t* key, const bool& value);
    static void insert(const wchar_t* key, const float& value);
    static void insert(const wchar_t* key, const nlohmann::json& value);

    static void insert(const std::string& key, const std::wstring& value);
    static void insert(const std::string& key, const wchar_t* value);
    static void insert(const std::string& key, const std::string& value);
    static void insert(const std::string& key, const char* value);
    static void insert(const std::string& key, const int& value);
    static void insert(const std::string& key, const unsigned int& value);
    static void insert(const std::string& key, const size_t& value);
    static void insert(const std::string& key, const bool& value);
    static void insert(const std::string& key, const float& value);
    static void insert(const std::string& key, const nlohmann::json& value);

    static void insert(const char* key, const std::wstring& value);
    static void insert(const char* key, const wchar_t* value);
    static void insert(const char* key, const std::string& value);
    static void insert(const char* key, const char* value);
    static void insert(const char* key, const int& value);
    static void insert(const char* key, const unsigned int& value);
    static void insert(const char* key, const size_t& value);
    static void insert(const char* key, const bool& value);
    static void insert(const char* key, const float& value);
    static void insert(const char* key, const nlohmann::json& value);

    static void pushBack(const std::wstring& value);
    static void pushBack(const wchar_t* value);
    static void pushBack(const std::string& value);
    static void pushBack(const char* value);
    static void pushBack(const int& value);
    static void pushBack(const unsigned int& value);
    static void pushBack(const size_t& value);
    static void pushBack(const bool& value);
    static void pushBack(const float& value);
    static void pushBack(const nlohmann::json& value);

private:
    template <typename T> static void insertTemplate(const std::wstring& key, const T& value)
    {
        insert(ParallaxGenUtil::utf16toUTF8(key), value);
    }

    template <typename T> static void insertTemplate(const std::string& key, const T& value)
    {
        const std::lock_guard<std::mutex> lock(s_diagJSONMutex);

        if (!s_enabled) {
            return;
        }

        auto* ptrJSON = Prefix::resolvePrefix();

        // check that type of nlohmann::json::object
        if (ptrJSON->type() != nlohmann::json::value_t::object) {
            throw std::runtime_error("Cannot insert to non-object type");
        }

        if constexpr (std::is_same_v<T, std::wstring>) {
            (*ptrJSON)[key] = ParallaxGenUtil::utf16toUTF8(value);
        } else {
            (*ptrJSON)[key] = value;
        }
    }

    template <typename T> static void pushBackTemplate(const T& value)
    {
        const std::lock_guard<std::mutex> lock(s_diagJSONMutex);

        if (!s_enabled) {
            return;
        }

        auto* ptrJSON = Prefix::resolvePrefix();

        // check that type is nlohmann::json::array
        if (ptrJSON->type() != nlohmann::json::value_t::array) {
            throw std::runtime_error("Cannot push back to non-array type");
        }

        if constexpr (std::is_same_v<T, std::wstring>) {
            ptrJSON->push_back(ParallaxGenUtil::utf16toUTF8(value));
        } else {
            ptrJSON->push_back(value);
        }
    }
};
