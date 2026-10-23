// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/ENetLibrary.hpp"
#include "Microsoft/Xna/Framework/GamerServices/NetworkNotAvailableException.hpp"

#include <enet/enet.h>

namespace CNA::Internal::Net
{
    namespace
    {
        struct InitGuard
        {
            InitGuard()
            {
                if (enet_initialize() != 0)
                {
                    throw Microsoft::Xna::Framework::GamerServices::NetworkNotAvailableException("Failed to initialize ENet.");
                }
            }
        };
    }

    void ENetLibrary::EnsureInitialized()
    {
        static InitGuard guard;
        (void) guard;
    }
}
