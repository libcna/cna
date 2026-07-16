// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>

#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"

// plan_xnb.md XNB-32: stock-effect .xnb readers -- see PrimitiveContentTypeReaders.hpp's own note
// on why these live in CNA::Internal::Xnb (FNA's own equivalents are all `internal`/default-
// visibility classes, never subclassed by game code).
//
// FNA's own T (BasicEffect, etc.) is a reference type; CNA's own C++ classes have private
// clone-only copy constructors and no move constructor, so a bare-value ContentTypeReader<T>
// cannot even return from Read() (no accessible copy or move to elide to on every compiler).
// Matching an existing CNA convention for GC-tracked XNA reference types passed through the
// content pipeline (ContentManager.cpp's own EffectTypeReader for .cnb custom effects returns
// std::shared_ptr<Effect>), these readers target std::shared_ptr<T> instead of a bare T.
//
// Each stock effect's texture field(s) are read via ContentReader::ReadExternalReference<T>()
// (plan_xnb.md XNB-35) and given to the effect via SetOwnedTexture()/SetOwnedTexture2()/
// SetOwnedEnvironmentMap() -- NOXNA additions that let the effect keep its own texture reference
// alive (matching XNA's real GC-tracked Effect.Texture), since a standalone content-loaded effect
// has no external owner the way a Model's shared texture pool does.

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReader;
    using Microsoft::Xna::Framework::Graphics::AlphaTestEffect;
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::DualTextureEffect;
    using Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect;
    using Microsoft::Xna::Framework::Graphics::SkinnedEffect;

    /** @brief FNA's real `Microsoft.Xna.Framework.Content.BasicEffectReader`. */
    class BasicEffectReader : public ContentTypeReader<std::shared_ptr<BasicEffect>>
    {
    public:
        BasicEffectReader()
            : ContentTypeReader<std::shared_ptr<BasicEffect>>("Microsoft.Xna.Framework.Graphics.BasicEffect") {}

    protected:
        std::shared_ptr<BasicEffect> Read(
            ContentReader& input, std::optional<std::shared_ptr<BasicEffect>> existingInstance) override;
    };

    /** @brief FNA's real `Microsoft.Xna.Framework.Content.AlphaTestEffectReader`. */
    class AlphaTestEffectReader : public ContentTypeReader<std::shared_ptr<AlphaTestEffect>>
    {
    public:
        AlphaTestEffectReader()
            : ContentTypeReader<std::shared_ptr<AlphaTestEffect>>("Microsoft.Xna.Framework.Graphics.AlphaTestEffect") {}

    protected:
        std::shared_ptr<AlphaTestEffect> Read(
            ContentReader& input, std::optional<std::shared_ptr<AlphaTestEffect>> existingInstance) override;
    };

    /** @brief FNA's real `Microsoft.Xna.Framework.Content.DualTextureEffectReader`. */
    class DualTextureEffectReader : public ContentTypeReader<std::shared_ptr<DualTextureEffect>>
    {
    public:
        DualTextureEffectReader()
            : ContentTypeReader<std::shared_ptr<DualTextureEffect>>("Microsoft.Xna.Framework.Graphics.DualTextureEffect") {}

    protected:
        std::shared_ptr<DualTextureEffect> Read(
            ContentReader& input, std::optional<std::shared_ptr<DualTextureEffect>> existingInstance) override;
    };

    /** @brief FNA's real `Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader`. */
    class EnvironmentMapEffectReader : public ContentTypeReader<std::shared_ptr<EnvironmentMapEffect>>
    {
    public:
        EnvironmentMapEffectReader()
            : ContentTypeReader<std::shared_ptr<EnvironmentMapEffect>>("Microsoft.Xna.Framework.Graphics.EnvironmentMapEffect") {}

    protected:
        std::shared_ptr<EnvironmentMapEffect> Read(
            ContentReader& input, std::optional<std::shared_ptr<EnvironmentMapEffect>> existingInstance) override;
    };

    /** @brief FNA's real `Microsoft.Xna.Framework.Content.SkinnedEffectReader`. */
    class SkinnedEffectReader : public ContentTypeReader<std::shared_ptr<SkinnedEffect>>
    {
    public:
        SkinnedEffectReader()
            : ContentTypeReader<std::shared_ptr<SkinnedEffect>>("Microsoft.Xna.Framework.Graphics.SkinnedEffect") {}

    protected:
        std::shared_ptr<SkinnedEffect> Read(
            ContentReader& input, std::optional<std::shared_ptr<SkinnedEffect>> existingInstance) override;
    };

    /** @brief Registers all 5 stock-effect readers above under their real FNA canonical names. Idempotent. */
    void RegisterStockEffectXnbReaders();
}
