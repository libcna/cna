// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Abstract base class for all texture types. */
    class Texture : public GraphicsResource
    {
    public:
        using GraphicsResource::Dispose;

        /** @brief Returns the surface format of the texture data. */
        [[nodiscard]] SurfaceFormat getFormatProperty() const;

        /** @brief Returns the number of mipmap levels in this texture. */
        [[nodiscard]] int getLevelCountProperty() const;

        /**
         * @brief Gets the width/height (in texels) of one square compression block for the given format.
         *
         * @param format The surface format to query.
         * @return 16 for block-compressed formats (Dxt1/Dxt3/Dxt5/Dxt5SrgbEXT/Bc7EXT/Bc7SrgbEXT,
         *         each using 4x4 texel blocks); 1 for every uncompressed format.
         */
        static int GetBlockSizeSquaredEXT(SurfaceFormat format);

        /**
         * @brief Gets the size in bytes of one compression block (compressed formats) or one texel (uncompressed formats).
         *
         * @param format The surface format to query.
         * @return The size in bytes: e.g. 8 for Dxt1, 16 for Dxt3/Dxt5/Bc7EXT, 4 for Color, 16 for Vector4.
         */
        static int GetFormatSizeEXT(SurfaceFormat format);

        /**
         * @brief Gets the OpenGL pixel-store alignment to use for the given format.
         *
         * @param format The surface format to query.
         * @return `min(8, GetFormatSizeEXT(format))`, matching the OpenGL 2.1 requirement that
         *         `GL_PACK_ALIGNMENT`/`GL_UNPACK_ALIGNMENT` never exceed 8.
         */
        static int GetPixelStoreAlignment(SurfaceFormat format);

        /**
         * @brief Throws if @p elementSizeInBytes does not evenly divide the byte size of @p format.
         *
         * @param format The surface format being read.
         * @param elementSizeInBytes The size, in bytes, of the destination element type.
         */
        static void ValidateGetDataFormat(SurfaceFormat format, int elementSizeInBytes);

    public:
        /**
         * @brief Throws std::runtime_error if @p fmt is not yet implemented.
         *
         * SurfaceFormat::Color is supported by all renderers; individual selected renderers may
         * expose additional exact routes.
         * Texture3D, TextureCube, and renderer-default Texture2D constructors call this so callers
         * get a clear error instead of silent RGBA8 misinterpretation. A selected Texture2D
         * renderer may use a stricter local allow-list for its additional exact routes.
         *
         * @param fmt The SurfaceFormat to validate.
         */
        CNAEXT static void ValidateFormat(SurfaceFormat fmt);

        /**
         * @brief Whether a GraphicsProfile permits this SurfaceFormat for a Texture2D at all.
         *
         * XNA decides texture-format legality by GraphicsProfile; CNA had decided it by renderer
         * capability alone, so a renderer able to carry a HiDef-only format offered it to a Reach
         * game too (REMED-GFX-242). This is the profile half of the answer and says nothing about
         * whether the active renderer can carry the format -- both must agree.
         *
         * The tables are measured on the real XNA 4.0 runtime rather than transcribed from
         * documentation: see spikes/xna-pixel-center-spike/, leg LEG-F, run at both profiles.
         * Reach accepts nine formats and refuses eleven with NotSupportedException; HiDef refuses
         * none of them.
         *
         * @param profile The profile the device was created with.
         * @param fmt The SurfaceFormat to test.
         * @return True when the profile permits the format, false when it refuses it.
         */
        CNAEXT [[nodiscard]] static bool IsFormatAllowedByProfileEXT(GraphicsProfile profile,
                                                                     SurfaceFormat fmt) noexcept;

        /**
         * @brief Whether a GraphicsProfile permits this SurfaceFormat for a TextureCube.
         *
         * The cube list is the Texture2D list minus the two signed-normalized byte formats, which
         * XNA refuses for a cube at BOTH profiles -- measured, not derived: HiDef still refuses
         * them on a device that carries them happily as a Texture2D. See
         * spikes/xna-pixel-center-spike/ leg LEG-G, and REMED-GFX-245.
         *
         * @param profile The profile the device was created with.
         * @param fmt The SurfaceFormat to test.
         * @return True when the profile permits the format for a cube.
         */
        CNAEXT [[nodiscard]] static bool IsCubeFormatAllowedByProfileEXT(GraphicsProfile profile,
                                                                         SurfaceFormat fmt) noexcept;

        /**
         * @brief Whether a GraphicsProfile permits this SurfaceFormat for a render target.
         *
         * The render-target list is the Texture2D list minus the three block-compressed formats,
         * which nothing can render into. Note what this does NOT say: XNA does not REFUSE a render
         * target whose format the profile excludes, it silently SUBSTITUTES `Color`. Callers that
         * need to know before committing should ask rather than catch. Measured in
         * spikes/xna-pixel-center-spike/ leg LEG-H; see REMED-GFX-245.
         *
         * @param profile The profile the device was created with.
         * @param fmt The SurfaceFormat to test.
         * @return True when the profile permits the format for a render target.
         */
        CNAEXT [[nodiscard]] static bool IsRenderTargetFormatAllowedByProfileEXT(
            GraphicsProfile profile, SurfaceFormat fmt) noexcept;

    protected:
        explicit Texture(GraphicsDevice* device = nullptr);

        /**
         * @brief Removes this texture from all sampler slots before disposal.
         *
         * Matches FNA's Texture.Dispose behaviour: the texture is unbound from
         * GraphicsDevice.Textures and GraphicsDevice.VertexTextures so that
         * callers never hold a dangling bound texture pointer.
         *
         * @param disposing True when called from Dispose(); false from destructor.
         */
        void Dispose(bool disposing) override;

        SurfaceFormat format_ = SurfaceFormat::Color;
        int levelCount_ = 1;
    };
}
