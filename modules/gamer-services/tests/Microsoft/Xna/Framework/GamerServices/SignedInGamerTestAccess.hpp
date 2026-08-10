// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    // Test-only accessor for SignedInGamer's private OnSignIn/OnSignOut (Task 7.7 tightened
    // these to match FNA's own `internal` visibility - see the friend declaration in
    // SignedInGamer.hpp), needed to keep directly testing that SignedIn/SignedOut actually fire.
    struct SignedInGamerTestAccess
    {
        static void OnSignIn(SignedInGamer* gamer)
        {
            SignedInGamer::OnSignIn(gamer);
        }

        static void OnSignOut(SignedInGamer* gamer)
        {
            SignedInGamer::OnSignOut(gamer);
        }
    };
}
