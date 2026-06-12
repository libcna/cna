// SPDX-License-Identifier: MS-PL

#pragma once

#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework
{
    /// Provides data for the Game.Exiting event.
    class ExitingEventArgs : public System::EventArgs
    {
    public:
        /// Creates default ExitingEventArgs.
        ExitingEventArgs() = default;
    };
}
