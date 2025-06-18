#include "util/Logger.hpp"

#include <spdlog/spdlog.h>

#include "util/ParallaxGenUtil.hpp"

using namespace std;

// Static thread-local variable
thread_local vector<wstring> Logger::s_prefixStack;

// Helper function to build the full prefix string
auto Logger::buildPrefixWString() -> wstring
{
    wstringstream fullPrefix;
    for (const auto& block : Logger::s_prefixStack) {
        fullPrefix << L"[" << block << L"] ";
    }
    return fullPrefix.str();
}

auto Logger::buildPrefixString() -> string { return ParallaxGenUtil::utf16toUTF8(buildPrefixWString()); }

// ScopedPrefix class implementation
Logger::Prefix::Prefix(const wstring& prefix)
{
    // Add the new prefix block to the stack
    s_prefixStack.push_back(prefix);
}

Logger::Prefix::Prefix(const string& prefix)
{
    // Add the new prefix block to the stack
    s_prefixStack.push_back(ParallaxGenUtil::utf8toUTF16(prefix));
}

Logger::Prefix::~Prefix()
{
    // Remove the last prefix block
    if (!s_prefixStack.empty()) {
        s_prefixStack.pop_back();
    }
}
