#include "PGDiag.hpp"

#include <mutex>

#include "ParallaxGenUtil.hpp"

using namespace std;

// statics
nlohmann::json PGDiag::s_diagJSON;
mutex PGDiag::s_diagJSONMutex;
bool PGDiag::s_enabled = false;

thread_local vector<string> PGDiag::s_ptrPrefixStack;

// ScopedPrefix class implementation
PGDiag::Prefix::Prefix(const wstring& prefix, const nlohmann::json::value_t& type)
    : Prefix(ParallaxGenUtil::utf16toUTF8(prefix), type)
{
}

PGDiag::Prefix::Prefix(const string& prefix, const nlohmann::json::value_t& type)
{
    const lock_guard<mutex> lock(s_diagJSONMutex);

    if (!s_enabled) {
        return;
    }

    // create object if needed
    auto* ptrJSON = resolvePrefix();
    if (!ptrJSON->contains(prefix)) {
        (*ptrJSON)[prefix] = nlohmann::json(type);
    }

    s_ptrPrefixStack.push_back(prefix);
}

PGDiag::Prefix::~Prefix()
{
    const lock_guard<mutex> lock(s_diagJSONMutex);

    if (!s_enabled) {
        return;
    }

    if (!s_ptrPrefixStack.empty()) {
        // remove last element of s_ptrPrefixStack
        s_ptrPrefixStack.pop_back();
    }
}

auto PGDiag::Prefix::resolvePrefix() -> nlohmann::json*
{
    nlohmann::json* outPtr = &s_diagJSON;

    for (const auto& prefix : s_ptrPrefixStack) {
        outPtr = &outPtr->at(prefix);
    }

    return outPtr;
}

void PGDiag::init()
{
    const lock_guard<mutex> lock(s_diagJSONMutex);

    s_diagJSON = nlohmann::json::object();
    s_enabled = true;
}

auto PGDiag::getJSON() -> nlohmann::json
{
    const lock_guard<mutex> lock(s_diagJSONMutex);

    if (!s_enabled) {
        return nlohmann::json::object();
    }

    return s_diagJSON;
}

auto PGDiag::isEnabled() -> bool { return s_enabled; }

// insert functions
void PGDiag::insert(const wstring& key, const wstring& value) { insertTemplate(key, value); }
void PGDiag::insert(const wstring& key, const wchar_t* value) { insertTemplate(key, value); }
void PGDiag::insert(const wstring& key, const string& value) { insertTemplate(key, value); }
void PGDiag::insert(const wstring& key, const char* value) { insertTemplate(key, value); }
void PGDiag::insert(const wstring& key, const int& value) { insertTemplate(key, value); }
void PGDiag::insert(const wstring& key, const unsigned int& value) { insertTemplate(key, value); }
void PGDiag::insert(const wstring& key, const size_t& value) { insertTemplate(key, value); }
void PGDiag::insert(const wstring& key, const bool& value) { insertTemplate(key, value); }
void PGDiag::insert(const wstring& key, const float& value) { insertTemplate(key, value); }
void PGDiag::insert(const wstring& key, const nlohmann::json& value) { insertTemplate(key, value); }

void PGDiag::insert(const wchar_t* key, const wstring& value) { insertTemplate(key, value); }
void PGDiag::insert(const wchar_t* key, const wchar_t* value) { insertTemplate(key, value); }
void PGDiag::insert(const wchar_t* key, const string& value) { insertTemplate(key, value); }
void PGDiag::insert(const wchar_t* key, const char* value) { insertTemplate(key, value); }
void PGDiag::insert(const wchar_t* key, const int& value) { insertTemplate(key, value); }
void PGDiag::insert(const wchar_t* key, const unsigned int& value) { insertTemplate(key, value); }
void PGDiag::insert(const wchar_t* key, const size_t& value) { insertTemplate(key, value); }
void PGDiag::insert(const wchar_t* key, const bool& value) { insertTemplate(key, value); }
void PGDiag::insert(const wchar_t* key, const float& value) { insertTemplate(key, value); }
void PGDiag::insert(const wchar_t* key, const nlohmann::json& value) { insertTemplate(key, value); }

void PGDiag::insert(const string& key, const wstring& value) { insertTemplate(key, value); }
void PGDiag::insert(const string& key, const wchar_t* value) { insertTemplate(key, value); }
void PGDiag::insert(const string& key, const string& value) { insertTemplate(key, value); }
void PGDiag::insert(const string& key, const char* value) { insertTemplate(key, value); }
void PGDiag::insert(const string& key, const int& value) { insertTemplate(key, value); }
void PGDiag::insert(const string& key, const unsigned int& value) { insertTemplate(key, value); }
void PGDiag::insert(const string& key, const size_t& value) { insertTemplate(key, value); }
void PGDiag::insert(const string& key, const bool& value) { insertTemplate(key, value); }
void PGDiag::insert(const string& key, const float& value) { insertTemplate(key, value); }
void PGDiag::insert(const string& key, const nlohmann::json& value) { insertTemplate(key, value); }

void PGDiag::insert(const char* key, const wstring& value) { insertTemplate(key, value); }
void PGDiag::insert(const char* key, const wchar_t* value) { insertTemplate(key, value); }
void PGDiag::insert(const char* key, const string& value) { insertTemplate(key, value); }
void PGDiag::insert(const char* key, const char* value) { insertTemplate(key, value); }
void PGDiag::insert(const char* key, const int& value) { insertTemplate(key, value); }
void PGDiag::insert(const char* key, const unsigned int& value) { insertTemplate(key, value); }
void PGDiag::insert(const char* key, const size_t& value) { insertTemplate(key, value); }
void PGDiag::insert(const char* key, const bool& value) { insertTemplate(key, value); }
void PGDiag::insert(const char* key, const float& value) { insertTemplate(key, value); }
void PGDiag::insert(const char* key, const nlohmann::json& value) { insertTemplate(key, value); }

// pushBack functions
void PGDiag::pushBack(const wstring& value) { pushBackTemplate(value); }
void PGDiag::pushBack(const wchar_t* value) { pushBackTemplate(value); }
void PGDiag::pushBack(const string& value) { pushBackTemplate(value); }
void PGDiag::pushBack(const char* value) { pushBackTemplate(value); }
void PGDiag::pushBack(const int& value) { pushBackTemplate(value); }
void PGDiag::pushBack(const unsigned int& value) { pushBackTemplate(value); }
void PGDiag::pushBack(const size_t& value) { pushBackTemplate(value); }
void PGDiag::pushBack(const bool& value) { pushBackTemplate(value); }
void PGDiag::pushBack(const float& value) { pushBackTemplate(value); }
void PGDiag::pushBack(const nlohmann::json& value) { pushBackTemplate(value); }
