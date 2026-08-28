// SPDX-License-Identifier: MS-PL
#pragma once

#include "CnaCApiDetail.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/AsciiPostProcessEffect.hpp"

#include <memory>

namespace CNA::C::Detail {

/**
 * @brief The runtime resource behind a `CNA_AsciiPostProcessEffectHandle`.
 *
 * `plans/plan_binding.md` `CBIND-089D`. This lived in an anonymous namespace inside
 * `CnaCApiGraphicsExt.cpp` until the ASCII **pass** needed to hand out a borrowed effect from a
 * different translation unit. Two units that create or read the same `ObjectKind` must agree on
 * the resource type it was registered with: declaring a second, locally-defined struct would
 * compile, register under the same kind, and then be `static_pointer_cast` to the wrong type by
 * whichever unit did not define it -- undefined behaviour that no test would reliably catch.
 * So the declaration is shared rather than duplicated.
 */
struct AsciiEffectResource final {
    /** @brief The effect itself, owned or aliased onto its owner. */
    std::shared_ptr<CNA::Graphics::AsciiPostProcessEffect> value;

    /** @brief The game the effect's device belongs to, for lifetime accounting. */
    CNA_Handle parentGame;
};

} // namespace CNA::C::Detail

#endif // CNA_CNAEXT
