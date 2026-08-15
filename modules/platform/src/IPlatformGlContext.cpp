// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/IPlatformGlContext.hpp"

namespace CNA::Platform {

    const std::string& ToString(const GlProfile profile)
    {
        static const std::string core = "Core";
        static const std::string compatibility = "Compatibility";
        static const std::string es = "Es";

        switch (profile)
        {
            case GlProfile::Core:          return core;
            case GlProfile::Compatibility: return compatibility;
            case GlProfile::Es:            return es;
        }
        return core;
    }

} // namespace CNA::Platform
