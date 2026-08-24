// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

// plans/plan_xnb.md XNB-25 (Phase D3): TextureCubeReader -- see Texture2DContentTypeReader.hpp's own
// note on why this lives in CNA::Internal::Xnb (FNA's TextureCubeReader is `internal class`,
// never subclassed by game code). Same shape as Texture2DReader (XNB-23/24), extra per-face
// handling (6 faces, each with its own mip chain).

namespace CNA::Internal::Xnb
{
    /**
     * @brief FNA's real `Microsoft.Xna.Framework.Content.TextureCubeReader`
     *        (`src/Content/ContentReaders/TextureCubeReader.cs`), implemented strictly against
     *        CNA's renderer-neutral `TextureCube`/`GraphicsDevice` API -- never against any one
     *        renderer's internals directly.
     *
     * This reader targets the same bare, copyable `TextureCube` value wrapper as
     * `ContentManager::Load<TextureCube>`. Copies share the underlying renderer resource, which
     * lets the ordinary strong ContentManager cache preserve XNA's load-once behavior.
     *
     * **Current coverage** (matching Texture2DReader's own scope exactly): `SurfaceFormat.Color`
     * uploads raw bytes directly; `Dxt1`/`Dxt3`/`Dxt5` are always software-decompressed to `Color`
     * via the existing `CNA::Internal::Graphics::DxtUtil`. Every other `SurfaceFormat` throws a
     * clear `Microsoft::Xna::Framework::Content::ContentLoadException` naming the format.
     */
    class TextureCubeReader
        : public Microsoft::Xna::Framework::Content::ContentTypeReader<
              Microsoft::Xna::Framework::Graphics::TextureCube>
    {
    public:
        TextureCubeReader()
            : Microsoft::Xna::Framework::Content::ContentTypeReader<
                  Microsoft::Xna::Framework::Graphics::TextureCube>(
                  "Microsoft.Xna.Framework.Graphics.TextureCube") {}

    protected:
        Microsoft::Xna::Framework::Graphics::TextureCube Read(
            Microsoft::Xna::Framework::Content::ContentReader& input,
            std::optional<Microsoft::Xna::Framework::Graphics::TextureCube> existingInstance) override;
    };

    /** @brief Registers TextureCubeReader under its real FNA canonical name. Idempotent. */
    void RegisterTextureCubeXnbReader();
}
