//
// Created by robertvokac on 5/24/26.
//

#ifndef WINDOWSPHONESPEEDYBLUPI_CNAHELPER_HPP
#define WINDOWSPHONESPEEDYBLUPI_CNAHELPER_HPP

/**
 * @brief Marker macro that tags a public API member as a CNA-specific extension.
 *
 * Applied to declarations that are not part of the XNA 4.0 API. In a normal
 * build the macro expands to nothing; its purpose is documentation only.
 *
 * Task VERIFY-003/DEV-API-002: when `CNA_STRICT_XNA_API` is defined, this
 * expands to `[[deprecated]]` instead, so a translation unit compiled with
 * that macro plus `-Werror=deprecated-declarations` fails to build if it
 * actually calls a `CNAEXT`-tagged member — see the `cna_strict_xna_api_check`
 * CMake target and `tests/Microsoft/Devices/StrictXnaApiSurfaceCheck.cpp`.
 * `CNA_STRICT_XNA_API` is never defined in a normal build, so this branch has
 * no effect anywhere else in the codebase.
 */
#ifdef CNA_STRICT_XNA_API
#define CNAEXT [[deprecated("CNAEXT: not part of the XNA 4.0 API surface")]]
#else
#define CNAEXT
#endif

#endif //WINDOWSPHONESPEEDYBLUPI_CNAHELPER_HPP
