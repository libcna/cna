// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/EngineLayerVersion.hpp"

#ifdef CNA_CNAEXT

namespace CNA::Graphics {

    int getEngineLayerVersion()
    {
        // Deliberately reads the macro here, in the library's own translation unit, so the value
        // returned is the library's and not the caller's. A caller comparing the two is exactly
        // the mismatch check the header describes.
        return CNA_CNAEXT_ENGINE_VERSION;
    }

    std::string getEngineLayerVersionString()
    {
        return "CNA engine layer " + std::to_string(getEngineLayerVersion());
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
