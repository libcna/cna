// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_GRAPHICS_DETAIL_HPP
#define CNA_C_API_GRAPHICS_DETAIL_HPP

#include "CNA/C/abi.h"

#include <cstdint>
#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
class SpriteBatch;
class Effect;
class OcclusionQuery;
class SpriteFont;
class IndexBuffer;
class RenderTargetCube;
class Texture;
class Texture2D;
class Texture3D;
class TextureCube;
class VertexBuffer;
}

namespace CNA::C::Detail {

struct EffectResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::Effect> value;
    CNA_Handle parentGame;
    std::shared_ptr<void> adapterState;
    uint64_t activeModelReferenceCount = 0U;
};

struct OcclusionQueryResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::OcclusionQuery> value;
    CNA_Handle parentGame;
};

struct Texture2DResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> value;
    CNA_Handle parentGame;
    uint64_t activeBatchReferenceCount;
    uint64_t activeFontReferenceCount;
    uint64_t activeEffectReferenceCount;
    uint64_t activeModelReferenceCount;
};

struct SpriteFontResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> value;
    std::shared_ptr<Texture2DResource> texture;
    CNA_Handle parentGame;
};

struct RenderTargetCubeResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::RenderTargetCube> value;
    CNA_Handle parentGame;
    uint64_t activeEffectReferenceCount;
};

struct Texture3DResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture3D> value;
    CNA_Handle parentGame;
    uint64_t activeEffectReferenceCount;
};

struct TextureCubeResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> value;
    CNA_Handle parentGame;
    uint64_t activeEffectReferenceCount;
};

struct VertexBufferResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> value;
    CNA_Handle parentGame;
    bool dynamic;
    uint64_t activeModelReferenceCount = 0U;
};

struct IndexBufferResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> value;
    CNA_Handle parentGame;
    bool dynamic;
    uint64_t activeModelReferenceCount = 0U;
};

struct TextureResourceView final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture> value;
    std::shared_ptr<void> retentionOwner;
    CNA_Handle parentGame;
    uint64_t* activeEffectReferenceCount;
};

struct TextureCubeResourceView final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> value;
    std::shared_ptr<void> retentionOwner;
    CNA_Handle parentGame;
    uint64_t* activeEffectReferenceCount;
};

[[nodiscard]] CNA_Result CreateOwnedTexture2D(
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> texture,
    CNA_Handle parentGame,
    CNA_Handle* outTexture);

[[nodiscard]] CNA_Result CreateStandaloneTexture2D(
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> texture,
    CNA_Handle* outTexture);

[[nodiscard]] CNA_Result CreateOwnedRenderTarget2D(
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> texture,
    CNA_Handle parentGame,
    CNA_Handle* outTexture);

[[nodiscard]] CNA_Result CreateOwnedTextureCube(
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> texture,
    CNA_Handle parentGame,
    CNA_Handle* outTexture);

// The guide draws its own keyboard and message box, so it needs a sprite batch it did not create.
// The batch resource layout stays private to the graphics adapter; this hands back only the canonical
// object, which is all a caller outside that adapter can use.
[[nodiscard]] CNA_Result GetOwnedSpriteBatchValue(
    CNA_Handle handle,
    Microsoft::Xna::Framework::Graphics::SpriteBatch** outSpriteBatch);

[[nodiscard]] CNA_Result GetOwnedSpriteFont(
    CNA_Handle handle,
    std::shared_ptr<SpriteFontResource>* outSpriteFont);

[[nodiscard]] CNA_Result GetOwnedTexture2D(
    CNA_Handle handle,
    std::shared_ptr<Texture2DResource>* outTexture);

[[nodiscard]] bool IsTexture2DFormatSupportedByBuild(uint32_t format) noexcept;

[[nodiscard]] CNA_Result GetOwnedTexture(
    CNA_Handle handle,
    TextureResourceView* outTexture);

[[nodiscard]] CNA_Result GetOwnedTexture3D(
    CNA_Handle handle,
    std::shared_ptr<Texture3DResource>* outTexture);

[[nodiscard]] CNA_Result GetOwnedTextureCube(
    CNA_Handle handle,
    TextureCubeResourceView* outTexture);

} // namespace CNA::C::Detail

#endif
