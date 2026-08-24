// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

// plans/plan_xnb.md XNB-25 (Phase D3): Texture3DReader -- see Texture2DContentTypeReader.hpp's own note
// on why this lives in CNA::Internal::Xnb (FNA's Texture3DReader is `internal class`, never
// subclassed by game code). Same shape as Texture2DReader (XNB-23/24), extra depth-slice handling.

namespace CNA::Internal::Xnb
{
    /**
     * @brief FNA's real `Microsoft.Xna.Framework.Content.Texture3DReader`
     *        (`src/Content/ContentReaders/Texture3DReader.cs`), implemented strictly against
     *        CNA's renderer-neutral `Texture3D`/`GraphicsDevice` API -- never against any one
     *        renderer's internals directly.
     *
     * Targets `std::shared_ptr<Texture3D>` rather than a bare `Texture3D`: Texture2D and
     * TextureCube are copy-constructible reference-resource wrappers, while Texture3D had no move
     * path at all before this reader needed one to return a freshly-decoded texture by value in the
     * first place -- see `Texture3D`'s own new move constructor/assignment for why that gap is now
     * closed, independent of this reader's own erasure choice.
     *
     * **Current coverage** (matching Texture2DReader's own scope exactly): `SurfaceFormat.Color`
     * uploads raw bytes directly; `Dxt1`/`Dxt3`/`Dxt5` are always software-decompressed to `Color`
     * via the existing `CNA::Internal::Graphics::DxtUtil`. Every other `SurfaceFormat` throws a
     * clear `Microsoft::Xna::Framework::Content::ContentLoadException` naming the format.
     */
    class Texture3DReader
        : public Microsoft::Xna::Framework::Content::ContentTypeReader<
              std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture3D>>
    {
    public:
        Texture3DReader()
            : Microsoft::Xna::Framework::Content::ContentTypeReader<
                  std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture3D>>(
                  "Microsoft.Xna.Framework.Graphics.Texture3D") {}

    protected:
        std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture3D> Read(
            Microsoft::Xna::Framework::Content::ContentReader& input,
            std::optional<std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture3D>> existingInstance) override;
    };

    /** @brief Registers Texture3DReader under its real FNA canonical name. Idempotent. */
    void RegisterTexture3DXnbReader();
}
