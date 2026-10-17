#include "PGRunObserver.hpp"

#include "common/BethesdaDirectory.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

void PGRunObserver::recordTextureType([[maybe_unused]] const std::filesystem::path& path,
                                      [[maybe_unused]] const PGEnums::TextureType& type)
{
}

void PGRunObserver::recordTextureAttribute([[maybe_unused]] const std::filesystem::path& path,
                                           [[maybe_unused]] const PGEnums::TextureAttribute& attribute,
                                           [[maybe_unused]] bool hasAttribute)
{
}

void PGRunObserver::recordTextureAttributes(
    [[maybe_unused]] const std::filesystem::path& path,
    [[maybe_unused]] const std::unordered_set<PGEnums::TextureAttribute>& attributes)
{
}

void PGRunObserver::recordTexMatch([[maybe_unused]] const std::wstring& base,
                                   [[maybe_unused]] const PGEnums::TextureType& type,
                                   [[maybe_unused]] const std::vector<PGTypes::PGTexture>& results)
{
}

void PGRunObserver::recordModOfFile([[maybe_unused]] const std::filesystem::path& path,
                                    [[maybe_unused]] const std::wstring& modName)
{
}

void PGRunObserver::recordMatches([[maybe_unused]] const PGTypes::TextureSet& slots,
                                  [[maybe_unused]] bool isSinglepassMATO,
                                  [[maybe_unused]] const PGPlugin::ModelRecordType& recType,
                                  [[maybe_unused]] uint64_t digest)
{
}

void PGRunObserver::recordOutputFile([[maybe_unused]] const std::filesystem::path& relPath,
                                     [[maybe_unused]] uint64_t size)
{
}

void PGRunObserver::suspendRecording() { }

void PGRunObserver::resumeRecording() { }

void PGRunObserver::recordHookRegistration([[maybe_unused]] const HookKind& kind,
                                           [[maybe_unused]] const std::filesystem::path& texPath)
{
}

bool PGRunObserver::tryReuseHookOutput([[maybe_unused]] const HookKind& kind,
                                       [[maybe_unused]] const std::filesystem::path& texPath)
{
    return false;
}

void PGRunObserver::recordHookOutput([[maybe_unused]] const HookKind& kind,
                                     [[maybe_unused]] const std::filesystem::path& source,
                                     [[maybe_unused]] const std::filesystem::path& output)
{
}

void PGRunObserver::recordTextureReadFailure([[maybe_unused]] const std::filesystem::path& path) { }

bool PGRunObserver::tryGetCachedMeshVotes([[maybe_unused]] const std::filesystem::path& nifPath,
                                          [[maybe_unused]] const BethesdaDirectory::FileIdentity& identity,
                                          [[maybe_unused]] std::vector<PGTypes::TextureVote>& votes)
{
    return false;
}

void PGRunObserver::storeMeshVotes([[maybe_unused]] const std::filesystem::path& nifPath,
                                   [[maybe_unused]] const BethesdaDirectory::FileIdentity& identity,
                                   [[maybe_unused]] const std::vector<PGTypes::TextureVote>& votes)
{
}

bool PGRunObserver::tryGetCachedMeshUses([[maybe_unused]] const std::filesystem::path& nifPath,
                                         [[maybe_unused]] MeshUses& uses)
{
    return false;
}

bool PGRunObserver::tryGetCachedCMClassification([[maybe_unused]] const std::filesystem::path& texture,
                                                 [[maybe_unused]] const BethesdaDirectory::FileIdentity& identity,
                                                 [[maybe_unused]] PGTypes::CMClassification& result)
{
    return false;
}

void PGRunObserver::storeCMClassification([[maybe_unused]] const std::filesystem::path& texture,
                                          [[maybe_unused]] const BethesdaDirectory::FileIdentity& identity,
                                          [[maybe_unused]] const PGTypes::CMClassification& result)
{
}

PGRunObserver& PGRunObserver::current()
{
    static PGRunObserver defaultObserver;

    auto* const installed = s_installed.load();
    return installed ? *installed : defaultObserver;
}

void PGRunObserver::install(PGRunObserver* observer) { s_installed.store(observer); }

PGRunObserver::SuspendRecording::SuspendRecording()
    : m_observer(&current())
{
    m_observer->suspendRecording();
}

PGRunObserver::SuspendRecording::~SuspendRecording() { m_observer->resumeRecording(); }
