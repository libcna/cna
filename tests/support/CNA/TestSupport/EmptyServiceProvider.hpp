// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/IServiceProvider.hpp"

namespace CNA::TestSupport
{
    // No registered services; unlike a null pointer this satisfies XNA's constructor contract.
    // Process lifetime also covers managers returned by value from fixture factories.
    inline System::IServiceProvider* EmptyServices()
    {
        class EmptyServiceProvider final : public System::IServiceProvider
        {
        public:
            void* GetService(const std::type_info&) const override { return nullptr; }
        };
        static EmptyServiceProvider services;
        return &services;
    }
}
