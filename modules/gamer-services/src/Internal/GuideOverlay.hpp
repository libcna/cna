// SPDX-License-Identifier: MS-PL
#pragma once
#include "System/IServiceProvider.hpp"
#include <string>
namespace CNA::Internal::GamerServices {
/** @brief Installs automatic system Guide presentation in a standard Game service container.
 * @param provider Dispatcher service provider. */
void installGuideOverlay(System::IServiceProvider& provider);
/** @brief Gets non-sensitive sign-in progress text. @return Progress label, or empty. */
std::string guideSignInStatus();
}
