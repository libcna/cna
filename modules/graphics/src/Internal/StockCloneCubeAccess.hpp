// SPDX-License-Identifier: MS-PL
#pragma once
#include <atomic>

namespace Microsoft::Xna::Framework::Graphics
{
    class EffectParameter;
    class Texture;
    class TextureCube;
}

namespace CNA::Internal::Graphics
{
    // Link boundary (docs/link-size-architecture.md): Effect.cpp is linked by every program with a
    // SpriteBatch, so it must not name TextureCube's type information, which links the whole
    // class. A clone of a stock effect samples a cube only when its source is an
    // EnvironmentMapEffect (HND-TQ-25 D9), so EnvironmentMapEffect.cpp installs the two
    // operations that do, before any such clone can exist.
    struct StockCloneCubeAccess
    {
        // The EnvironmentMap parameter's value (EffectParameter::GetValueTextureCube).
        Microsoft::Xna::Framework::Graphics::TextureCube* (*environmentMap)(
            const Microsoft::Xna::Framework::Graphics::EffectParameter& parameter);
        // The texture as a TextureCube, or null for any other kind.
        Microsoft::Xna::Framework::Graphics::TextureCube* (*asCube)(
            Microsoft::Xna::Framework::Graphics::Texture* texture);
    };

    inline std::atomic<const StockCloneCubeAccess*> stockCloneCubeAccess{nullptr};
}
