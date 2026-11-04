// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/IServiceProvider.hpp"

namespace CNA::C::Detail
{
    /**
     * @brief Gets the C resource adapters' empty service provider.
     * @return A process-lifetime provider that has no registered services.
     */
    System::IServiceProvider* EmptyContentServices();
}
