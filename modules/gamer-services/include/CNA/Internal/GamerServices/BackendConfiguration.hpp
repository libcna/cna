// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/GamerServices/Configuration.hpp"

namespace CNA::Internal::GamerServices {
class IGamerServicesBackend;
/** @brief Copies the immutable deployment authority selected when a real backend was created.
 * @param backend Originating service backend, retained by the caller during this call.
 * @return Original endpoint/title/trust configuration; refuses fake or unconfigured backends. */
CNA::GamerServices::Configuration configurationForBackend(const IGamerServicesBackend& backend);
}
