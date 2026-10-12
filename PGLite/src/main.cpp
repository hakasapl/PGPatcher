#include "PCH.h"

#include "Runtime.hpp"

// The plugin declaration SKSE reads before loading the DLL. The Address Library makes one build serve every supported
// runtime; the version is the one CMake derives from the repository.
SKSEPluginInfo(.Version = REL::Version { PG_VERSION_MAJOR,
                                         PG_VERSION_MINOR,
                                         PG_VERSION_PATCH,
                                         0 },
               .Name = "PGLite",
               .Author = "hakasapl",
               .StructCompatibility = SKSE::StructCompatibility::Independent,
               .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

    SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    // PGLite logs through its own file, see Logging.
    SKSE::Init(skse, { .log = false });

    return PGLite::Runtime::instance().load();
}
