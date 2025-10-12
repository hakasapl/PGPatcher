#include "patchers/base/Patcher.hpp"

#include "ParallaxGenD3D.hpp"
#include "ParallaxGenDirectory.hpp"

#include <string>
#include <utility>

using namespace std;

ParallaxGenDirectory* Patcher::s_pgd = nullptr;
ParallaxGenD3D* Patcher::s_pgd3d = nullptr;

auto Patcher::loadStatics(ParallaxGenDirectory& pgd, ParallaxGenD3D& pgd3d) -> void
{
    Patcher::s_pgd = &pgd;
    Patcher::s_pgd3d = &pgd3d;
}

Patcher::Patcher(string patcherName, const bool& triggerSave)
    : m_patcherName(std::move(patcherName))
    , m_triggerSave(triggerSave)
{
}

auto Patcher::getPatcherName() const -> string { return m_patcherName; }
auto Patcher::triggerSave() const -> bool { return m_triggerSave; }

auto Patcher::getPGD() -> ParallaxGenDirectory* { return s_pgd; }
auto Patcher::getPGD3D() -> ParallaxGenD3D* { return s_pgd3d; }
