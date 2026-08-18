// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include <string>

/**
 * @brief Revision of the `CNA::Graphics` engine-layer API compiled into this build.
 *
 * plan_modern.md `MOD-8`. An integer that starts at 1 and increases whenever the engine layer's
 * public shape changes in a way a consumer could notice. It is a macro as well as a function
 * because the two answer different questions: the macro is what the *header* a translation unit
 * compiled against said, the function is what the *library* it ended up linked to says. When those
 * disagree, something was rebuilt and something else was not — which is the failure this exists to
 * make visible.
 *
 * **Not an ABI guarantee.** `CNAEXT.md` §9 says the engine layer may change until it stabilizes,
 * and this number does not soften that: it records which revision you have, it does not promise
 * that two revisions are compatible.
 */
#define CNA_CNAEXT_ENGINE_VERSION 1

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Returns the engine-layer revision the linked library was built with.
     *
     * @return The revision number; 1 for the first published shape of the layer.
     */
    [[nodiscard]] int getEngineLayerVersion();

    /**
     * @brief Returns the engine-layer revision as text, for logs and about-boxes.
     *
     * @return The revision in the form `"CNA engine layer 1"`.
     */
    [[nodiscard]] std::string getEngineLayerVersionString();

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
