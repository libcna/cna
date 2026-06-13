// SPDX-License-Identifier: MS-PL

#pragma once

#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief Provides data for the Game.Exiting event. */
    class ExitingEventArgs : public System::EventArgs
    {
    public:
        /** @brief Creates default ExitingEventArgs. */
        ExitingEventArgs() = default;
    };
}
