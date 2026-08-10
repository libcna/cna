// SPDX-License-Identifier: MS-PL
#pragma once
#include <string>
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimationPreset.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Returns the clip name used by the real-rendering Avatar extension for a preset.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. CNA extension. The returned string is
     * simply the enumerator's own name (e.g. "Wave" for AvatarAnimationPreset::Wave) and is
     * used to look up a matching Graphics::AnimationClipEXT inside a loaded
     * Graphics::SkinnedModelEXT — the offline asset-conversion tool
     * (tools/avatar_asset_pipeline/) is the single source of truth for producing clips under
     * these exact names.
     *
     * @param preset The preset to name.
     * @return The preset's enumerator name.
     * @throws System::ArgumentException if @p preset is not a recognized enumerator value.
     */
    CNAEXT [[nodiscard]] std::string AvatarAnimationPresetToClipNameEXT(AvatarAnimationPreset preset);
}
