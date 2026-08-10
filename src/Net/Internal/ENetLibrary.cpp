// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/ENetLibrary.hpp"

#include <enet/enet.h>
#include <stdexcept>

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
                    throw std::runtime_error("Failed to initialize ENet.");
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
