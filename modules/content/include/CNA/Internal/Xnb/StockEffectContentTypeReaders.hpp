// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>

#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"

// plans/plan_xnb.md XNB-32: stock-effect .xnb readers -- see PrimitiveContentTypeReaders.hpp's own note
// on why these live in CNA::Internal::Xnb (FNA's own equivalents are all `internal`/default-
// visibility classes, never subclassed by game code).
//
// FNA's own T (BasicEffect, etc.) is a reference type; CNA's own C++ classes have private
// clone-only copy constructors and no move constructor, so a bare-value ContentTypeReader<T>
// cannot even return from Read() (no accessible copy or move to elide to on every compiler).
// Matching an existing CNA convention for GC-tracked XNA reference types passed through the
// content pipeline (ContentManager.cpp's own EffectTypeReader for .cnj custom effects returns
// std::shared_ptr<Effect>), these readers target std::shared_ptr<T> instead of a bare T.
//
// Target the common std::shared_ptr<Effect> base, not std::shared_ptr<ConcreteEffectType>: unlike
// FNA (where ReadSharedResource<Effect>()'s C# cast succeeds via ordinary RTTI regardless of which
// concrete effect type a reader's own generic parameter names), CNA's std::any_cast<T> requires an
// *exact* type match -- plans/plan_xnb.md XNB-40 (a ModelMeshPart's Effect, read via
// ReadSharedResource<std::shared_ptr<Effect>>()) cannot know which of the 5 concrete effect types a
// given file actually used, so every reader must erase to the same base type for that dispatch to
// ever work. Each Read() still constructs the real concrete type internally; the upcast to
// shared_ptr<Effect> happens implicitly on return.
//
// Each stock effect's texture field(s) are read via ContentReader::ReadExternalReference<T>()
// (plans/plan_xnb.md XNB-35) and given to the effect via SetOwnedTexture()/SetOwnedTexture2()/
// SetOwnedEnvironmentMap() -- CNAEXT additions that let the effect keep its own texture reference
// alive (matching XNA's real GC-tracked Effect.Texture), since a standalone content-loaded effect
// has no external owner the way a Model's shared texture pool does.

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReader;
    using Microsoft::Xna::Framework::Graphics::AlphaTestEffect;
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::DualTextureEffect;
    using Microsoft::Xna::Framework::Graphics::Effect;
    using Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect;
    using Microsoft::Xna::Framework::Graphics::SkinnedEffect;

    /** @brief FNA's real `Microsoft.Xna.Framework.Content.BasicEffectReader`. */
    class BasicEffectReader : public ContentTypeReader<std::shared_ptr<Effect>>
    {
    public:
        BasicEffectReader()
            : ContentTypeReader<std::shared_ptr<Effect>>("Microsoft.Xna.Framework.Graphics.BasicEffect") {}

    protected:
        std::shared_ptr<Effect> Read(
            ContentReader& input, std::optional<std::shared_ptr<Effect>> existingInstance) override;
    };

    /** @brief FNA's real `Microsoft.Xna.Framework.Content.AlphaTestEffectReader`. */
    class AlphaTestEffectReader : public ContentTypeReader<std::shared_ptr<Effect>>
    {
    public:
        AlphaTestEffectReader()
            : ContentTypeReader<std::shared_ptr<Effect>>("Microsoft.Xna.Framework.Graphics.AlphaTestEffect") {}

    protected:
        std::shared_ptr<Effect> Read(
            ContentReader& input, std::optional<std::shared_ptr<Effect>> existingInstance) override;
    };

    /** @brief FNA's real `Microsoft.Xna.Framework.Content.DualTextureEffectReader`. */
    class DualTextureEffectReader : public ContentTypeReader<std::shared_ptr<Effect>>
    {
    public:
        DualTextureEffectReader()
            : ContentTypeReader<std::shared_ptr<Effect>>("Microsoft.Xna.Framework.Graphics.DualTextureEffect") {}

    protected:
        std::shared_ptr<Effect> Read(
            ContentReader& input, std::optional<std::shared_ptr<Effect>> existingInstance) override;
    };

    /** @brief FNA's real `Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader`. */
    class EnvironmentMapEffectReader : public ContentTypeReader<std::shared_ptr<Effect>>
    {
    public:
        EnvironmentMapEffectReader()
            : ContentTypeReader<std::shared_ptr<Effect>>("Microsoft.Xna.Framework.Graphics.EnvironmentMapEffect") {}

    protected:
        std::shared_ptr<Effect> Read(
            ContentReader& input, std::optional<std::shared_ptr<Effect>> existingInstance) override;
    };

    /** @brief FNA's real `Microsoft.Xna.Framework.Content.SkinnedEffectReader`. */
    class SkinnedEffectReader : public ContentTypeReader<std::shared_ptr<Effect>>
    {
    public:
        SkinnedEffectReader()
            : ContentTypeReader<std::shared_ptr<Effect>>("Microsoft.Xna.Framework.Graphics.SkinnedEffect") {}

    protected:
        std::shared_ptr<Effect> Read(
            ContentReader& input, std::optional<std::shared_ptr<Effect>> existingInstance) override;
    };

    /** @brief Registers all 5 stock-effect readers above under their real FNA canonical names. Idempotent. */
    void RegisterStockEffectXnbReaders();
}
