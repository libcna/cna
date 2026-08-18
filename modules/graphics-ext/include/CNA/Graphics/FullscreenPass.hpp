// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
    class Effect;
    class GraphicsDevice;
    class RenderTarget2D;
    class SpriteBatch;
    class Texture2D;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Draws one screen-covering rectangle with a given effect: the mechanism every
     *        post-process pass is built on.
     *
     * Implemented over `SpriteBatch` with a custom `Effect`, which is the route CNA's renderers
     * already implement and verify -- including the part that is easiest to get wrong by hand, the
     * texture-coordinate origin, which differs between GL and D3D and is resolved once inside
     * SpriteBatch rather than separately in every pass shader.
     *
     * The alternative considered was an oversized triangle through `DrawUserPrimitives`, which
     * saves one state block per pass. It was not chosen: it would require every pass shader to
     * match a hand-rolled vertex declaration and to re-derive the UV convention per renderer, for
     * a saving that is invisible next to the fullscreen fill itself.
     */
    class FullscreenPass
    {
    public:
        /**
         * @brief Creates the drawer for one device.
         *
         * @param device The device whose SpriteBatch is used.
         */
        explicit FullscreenPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the drawer and its SpriteBatch. */
        ~FullscreenPass();

        FullscreenPass(const FullscreenPass&)            = delete;
        FullscreenPass& operator=(const FullscreenPass&) = delete;

        /**
         * @brief Binds @p destination, draws @p source across it with @p effect, and restores the
         *        previously bound target.
         *
         * The destination is not cleared: the draw covers every pixel, and clearing first would
         * cost a full-screen write for nothing.
         *
         * @param source      The image to sample. Must not be null.
         * @param destination Where to write, or null for the back buffer.
         * @param effect      The effect to draw with, or null for a plain copy.
         * @param width       Destination width in pixels.
         * @param height      Destination height in pixels.
         */
        void draw(Microsoft::Xna::Framework::Graphics::Texture2D* source,
                  Microsoft::Xna::Framework::Graphics::RenderTarget2D* destination,
                  Microsoft::Xna::Framework::Graphics::Effect* effect,
                  int width, int height);

        /**
         * @brief Draws across whatever target is already bound, without rebinding anything.
         *
         * The skybox needs this and the post-process passes do not: a pass knows both its source
         * and its destination, while the sky is drawn into "the frame", which is the pipeline's
         * scene target inside a frame and the back buffer outside one. Passing the bound target
         * back in would mean the caller had to know which of the two it currently was.
         *
         * @param source The image to sample. Must not be null.
         * @param effect The effect to draw with, or null for a plain copy.
         * @param width  Target width in pixels.
         * @param height Target height in pixels.
         */
        void drawOverCurrentTarget(Microsoft::Xna::Framework::Graphics::Texture2D* source,
                                   Microsoft::Xna::Framework::Graphics::Effect* effect,
                                   int width, int height);

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
