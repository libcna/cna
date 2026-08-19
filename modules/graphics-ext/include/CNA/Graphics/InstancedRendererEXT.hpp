// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <memory>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class DynamicVertexBuffer;
    class Effect;
    class GraphicsDevice;
    class ModelMeshPart;
    class VertexDeclaration;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Draws one mesh part many times in a single draw call.
     *
     * plan_modern.md `MOD-1400`–`MOD-1405`. Convenience over `GraphicsDevice::SetVertexBuffers`
     * and `DrawInstancedPrimitives`, which already exist: what this owns is the per-instance
     * transform stream, its declaration, and the decision of what to do on a renderer that cannot
     * instance at all.
     *
     * ```
     * CNA::Graphics::InstancedRendererEXT renderer(device, part);
     * renderer.setInstances(worldMatrices);      // once per frame, or once ever
     * renderer.draw(effect);                     // one draw call
     * ```
     *
     * The stock lit shaders read the per-instance matrix from attribute locations 12..15 and
     * multiply it into the world transform, so an ordinary `BasicEffect` or `PbrEffect` works
     * unchanged -- the effect's own `World` still applies, with the instance transform on top.
     */
    class InstancedRendererEXT
    {
    public:
        /**
         * @brief Constructs a renderer for one mesh part.
         *
         * @param device The device to allocate the instance stream on.
         * @param part   The part to draw; must have a vertex buffer, an index buffer and at least
         *               one primitive, and must outlive this renderer.
         * @throws std::invalid_argument If @p part is null or is not drawable.
         */
        InstancedRendererEXT(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                             Microsoft::Xna::Framework::Graphics::ModelMeshPart* part);

        /** @brief Destroys the renderer and the instance stream it owns. */
        ~InstancedRendererEXT();

        InstancedRendererEXT(const InstancedRendererEXT&)            = delete;
        InstancedRendererEXT& operator=(const InstancedRendererEXT&) = delete;

        /**
         * @brief The declaration of the per-instance transform stream.
         *
         * plan_modern.md `MOD-1402`. Four `Vector4` elements at `TextureCoordinate` usage indices
         * 1 through 4, 64 bytes in total -- the layout CNA's renderers already expect from an
         * instance stream, and the one the stock shaders bind to locations 12..15. It is a static
         * because a caller building its own instance buffer needs to describe it identically.
         *
         * @return The shared declaration.
         */
        [[nodiscard]] static const Microsoft::Xna::Framework::Graphics::VertexDeclaration&
        getInstanceDeclaration();

        /**
         * @brief The declaration of the optional per-instance tint stream.
         *
         * One `Color` element at `Color` usage index 1. See @ref setTintsEnabled for why this is
         * not something an ordinary effect can consume.
         *
         * @return The shared declaration.
         */
        [[nodiscard]] static const Microsoft::Xna::Framework::Graphics::VertexDeclaration&
        getTintDeclaration();

        /**
         * @brief Uploads the per-instance world transforms.
         *
         * plan_modern.md `MOD-1401`. The buffer grows when it has to and is otherwise reused, so
         * a game re-uploading the same number of instances every frame allocates nothing after the
         * first upload -- which @ref getInstanceCapacity makes assertable.
         *
         * @param transforms One world matrix per instance; an empty list draws nothing.
         */
        void setInstances(const std::vector<Microsoft::Xna::Framework::Matrix>& transforms);

        /**
         * @brief Uploads a per-instance tint, when the tint stream is enabled.
         *
         * plan_modern.md `MOD-1404`. Ignored while the stream is disabled, so a game can keep the
         * colours it has and switch the stream on and off without re-uploading them.
         *
         * @param tints One colour per instance; a shorter list leaves the remainder white.
         */
        void setInstanceTints(const std::vector<Microsoft::Xna::Framework::Color>& tints);

        /**
         * @brief Enables the per-instance tint stream. Off by default.
         *
         * **This needs a shader that declares it.** The stock lit shaders occupy attribute
         * locations 0..15 -- XNA's own ceiling, and GL ES 3's guaranteed minimum -- with the mesh
         * declaration and the instance matrix, so there is no location left for a tint. An effect
         * that wants one must be a `ShaderEffect` whose vertex input declares it, and whose mesh
         * declaration is small enough to leave room. Bound to a stock effect the extra stream has
         * nowhere to go and the draw is refused, which is exactly why this is opt-in.
         *
         * @param enabled True to bind the tint stream on the next draw.
         */
        void setTintsEnabled(bool enabled);

        /** @brief Returns whether the tint stream is bound. */
        [[nodiscard]] bool isTintsEnabled() const;

        /**
         * @brief Draws every instance.
         *
         * One `DrawInstancedPrimitives` call where the renderer supports instancing. Where it does
         * not, the behaviour depends on `setFallbackEnabled()`: either nothing is drawn and the
         * caller can see why, or the instances are drawn one at a time.
         *
         * @param effect The effect to draw with; its `Apply` is called once per draw call.
         * @throws std::logic_error If instancing is unsupported and no fallback is enabled.
         */
        void draw(Microsoft::Xna::Framework::Graphics::Effect& effect);

        /**
         * @brief Returns whether the device can draw this renderer's instanced path.
         *
         * plan_modern.md `MOD-1621`. **Two capabilities, not one**, because the instanced path
         * binds the per-instance transforms as a *second* vertex stream: a renderer that reports
         * `Instancing` but not `MultiStreamVertexInput` cannot run it. SDL_GPU is exactly that
         * renderer -- `Instancing` is `true` by base-class default while
         * `DrawInstancedPrimitives` refuses -- and asking only the first is how `draw()` threw
         * where it should have taken the per-instance fallback.
         *
         * @return True when `GraphicsCapability::Instancing` **and**
         *         `GraphicsCapability::MultiStreamVertexInput` are both supported.
         */
        [[nodiscard]] bool isInstancingSupported() const;

        /**
         * @brief Enables the single-draw-per-instance fallback. Off by default.
         *
         * plan_modern.md `MOD-1405`. Deliberately opt-in rather than automatic: the fallback costs
         * one draw call per instance, which for the ten thousand instances this class exists to
         * make cheap is not a fallback but a different program. A game that would rather draw
         * slowly than not at all says so; one that would rather know says nothing and gets an
         * exception it can catch.
         *
         * The fallback needs the effect to implement `IEffectMatrices`, because a per-instance
         * transform has nowhere else to go -- it multiplies the effect's own `World`, which is
         * restored afterwards.
         *
         * @param enabled True to draw one instance at a time where instancing is unavailable.
         */
        void setFallbackEnabled(bool enabled);

        /** @brief Returns whether the per-instance fallback is enabled. */
        [[nodiscard]] bool isFallbackEnabled() const;

        /** @brief Returns how many instances the last @ref setInstances uploaded. */
        [[nodiscard]] int getInstanceCount() const;

        /** @brief Returns how many instances the stream can hold without reallocating. */
        [[nodiscard]] int getInstanceCapacity() const;

        /** @brief Returns how many draw calls the last @ref draw issued. */
        [[nodiscard]] int getLastDrawCallCount() const;

        /** @brief Returns whether the last @ref draw used hardware instancing. */
        [[nodiscard]] bool didLastDrawInstance() const;

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        Microsoft::Xna::Framework::Graphics::ModelMeshPart* part_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer> instanceBuffer_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer> tintBuffer_;
        /// Kept CPU-side as well as uploaded: the fallback replays them one draw at a time, and
        /// CNA has no raw read-back from a vertex buffer to recover them from the GPU copy.
        std::vector<Microsoft::Xna::Framework::Matrix> transforms_;
        std::vector<Microsoft::Xna::Framework::Color> tints_;
        int  instanceCount_ = 0;
        int  instanceCapacity_ = 0;
        int  tintCapacity_ = 0;
        bool tintsEnabled_ = false;
        bool fallbackEnabled_ = false;
        int  lastDrawCallCount_ = 0;
        bool lastDrawInstanced_ = false;

        void uploadTints();
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
