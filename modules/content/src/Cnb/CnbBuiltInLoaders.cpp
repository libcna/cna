// SPDX-License-Identifier: MS-PL
// CnbLoaderRegistry::RegisterBuiltIns(): the Curve and AnimationClip loaders.
//
// Link boundary: apart from the registry itself, which every .cnb load uses, so that resolving a
// texture's loader does not link the curve and animation codecs. ContentManager registers each
// built-in loader with its asset family (detail::BuiltinContent); this is the explicit
// all-of-these entry point.
#include "CNA/Content/Cnb/CnbLoaderRegistry.hpp"
#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"

#include <any>

namespace CNA::Content
{
    void CnbLoaderRegistry::RegisterBuiltIns()
    {
        // Curve and AnimationClip need nothing but their own codecs -- no GraphicsDevice, no
        // external references -- so they are registered here. The other eight built-in loaders
        // each construct a runtime object needing a GraphicsDevice or the ContentManager itself,
        // so they register from ContentManager::RegisterBuiltinLoaders() where those helpers live.
        // This function is therefore NOT "every built-in", and its documentation says so
        // (CNBF-119).
        RegisterBuiltIn(Cnb::CnbAssetTypeId::Curve, "Microsoft.Xna.Framework.Curve",
                        [](const Cnb::CnbDocument& document,
                           Microsoft::Xna::Framework::Content::ContentManager&,
                           const std::string&) -> std::any
                        { return std::any(Cnb::DecodeCurveFromCnb(document)); });

        RegisterBuiltIn(Cnb::CnbAssetTypeId::AnimationClip,
                        "Microsoft.Xna.Framework.Graphics.AnimationClipEXT",
                        [](const Cnb::CnbDocument& document,
                           Microsoft::Xna::Framework::Content::ContentManager&,
                           const std::string&) -> std::any
                        { return std::any(Cnb::DecodeAnimationClipFromCnb(document)); });
    }
}
