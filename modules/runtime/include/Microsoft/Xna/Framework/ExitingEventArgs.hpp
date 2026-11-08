// SPDX-License-Identifier: MS-PL

#pragma once

#include "CNA/CNAHelper.hpp"
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework
{
    /**
     * @brief Provides an empty event-argument type for CNA callers.
     * @note CNA extension. XNA 4.0 defines no ExitingEventArgs type;
     *       Game::Exiting uses System::EventArgs directly.
     */
    class CNAEXT ExitingEventArgs : public System::EventArgs
    {
    public:
        /** @brief Creates default ExitingEventArgs. */
        ExitingEventArgs() = default;
    };
}
