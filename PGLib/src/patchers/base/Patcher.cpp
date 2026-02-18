#include "patchers/base/Patcher.hpp"

#include <string>
#include <utility>

using namespace std;

Patcher::Patcher(string patcherName)
    : m_patcherName(std::move(patcherName))
{
}

auto Patcher::getPatcherName() const -> string { return m_patcherName; }
