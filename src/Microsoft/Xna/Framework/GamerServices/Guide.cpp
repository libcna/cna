// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include <SDL3/SDL_log.h>

#include "CNA/Logger.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    void Guide::Show(const PlayerIndex& playerIndex)
    {
        CNA::Logger::Warn("The Market Place is not yet implemented.");
    }

    IMPL_PROP(bool, IsTrialMode, getter1, setter0, member1, static1, constret1, ref1, constmet0, Guide, false)
}
