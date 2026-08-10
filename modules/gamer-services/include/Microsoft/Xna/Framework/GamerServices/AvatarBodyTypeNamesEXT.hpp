// SPDX-License-Identifier: MS-PL
#pragma once
#include <string>
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyType.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Returns the ContentManager asset name for the procedurally-generated avatar
     * body matching the given AvatarBodyType.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. CNA extension. Phase 8's faithful
     * AvatarDescription::getBodyTypeProperty() never carries real body-type data — it
     * always lazily defaults to AvatarBodyType::Female, matching the real XNA
     * implementation's undocumented, never-populated 1021-byte description format — so
     * there is no way to derive this mapping from an AvatarDescription automatically.
     * This function is the single, explicit call-site convention a caller uses to pick
     * which of the two procedurally-generated bodies (tools/avatar_builder/ +
     * tools/avatar_asset_pipeline/convert_avatar.py, see
     * docs/avatar-real-rendering-ext.md) to load with
     * `ContentManager::Load<std::shared_ptr<Graphics::SkinnedModelEXT>>`, given an
     * AvatarBodyType obtained by whatever means the caller already has (e.g. a game's own
     * player-selection UI) — not derived from AvatarDescription.
     *
     * @param bodyType The body type to map.
     * @return The ContentManager asset name for that body type's generated content (e.g.
     * `"avatar/male/avatar"`, resolving to `Content/avatar/male/avatar.skinnedmodel.json`).
     * @throws System::ArgumentException if bodyType is not a recognized enumerator value.
     */
    CNAEXT [[nodiscard]] std::string AvatarBodyTypeToContentNameEXT(AvatarBodyType bodyType);
}
