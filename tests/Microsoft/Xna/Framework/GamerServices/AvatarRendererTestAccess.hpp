// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    // Test-only accessor for AvatarRenderer's private PartTintEXT (Task 13.1) - the only other
    // coverage goes through DrawRealEXT + real GPU pixel readback, which only ever exercised
    // Hair/Shirt routing.
    struct AvatarRendererTestAccess
    {
        static Microsoft::Xna::Framework::Color PartTintEXT(const AvatarRenderer& renderer, const std::string& partName)
        {
            return renderer.PartTintEXT(partName);
        }
    };
}
