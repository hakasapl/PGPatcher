#include "patchers/base/Patcher.hpp"

#include <string>
#include <utility>

Patcher::Patcher(std::string patcherName)
    : m_patcherName(std::move(patcherName))
{
}

std::string Patcher::patcherName() const { return m_patcherName; }
