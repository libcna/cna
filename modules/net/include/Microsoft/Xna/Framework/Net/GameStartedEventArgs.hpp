// SPDX-License-Identifier: MS-PL
#pragma once
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief Contains data for the GameStarted event.
     */
    class GameStartedEventArgs : public System::EventArgs
    {
    public:
        /**
         * @brief Initializes a new instance of GameStartedEventArgs.
         */
        GameStartedEventArgs();
    };
}
