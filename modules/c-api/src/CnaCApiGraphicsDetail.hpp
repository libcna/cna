// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_GRAPHICS_DETAIL_HPP
#define CNA_C_API_GRAPHICS_DETAIL_HPP

#include "CNA/C/abi.h"

#include <cstdint>
#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
class RenderTargetCube;
class Texture;
class Texture2D;
class Texture3D;
class TextureCube;
class VertexBuffer;
}

namespace CNA::C::Detail {

struct Texture2DResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> value;
    CNA_Handle parentGame;
    uint64_t activeBatchReferenceCount;
    uint64_t activeFontReferenceCount;
};

struct RenderTargetCubeResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::RenderTargetCube> value;
    CNA_Handle parentGame;
};

struct Texture3DResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture3D> value;
    CNA_Handle parentGame;
};

struct TextureCubeResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> value;
    CNA_Handle parentGame;
};

struct VertexBufferResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> value;
    CNA_Handle parentGame;
    bool dynamic;
};

struct TextureResourceView final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture> value;
    CNA_Handle parentGame;
};

struct TextureCubeResourceView final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> value;
    CNA_Handle parentGame;
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
