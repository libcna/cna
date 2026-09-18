// SPDX-License-Identifier: MS-PL

#pragma once

namespace CNA::Internal::Design {

/** @brief Registers the XNA value-type converter metadata with sharp-runtime. */
void EnsureFrameworkDesignConvertersRegistered();

/** @brief Ensures converter registration when a Framework.Design header is used. */
inline const bool FrameworkDesignConvertersRegistered = [] {
    EnsureFrameworkDesignConvertersRegistered();
    return true;
}();

} // namespace CNA::Internal::Design
