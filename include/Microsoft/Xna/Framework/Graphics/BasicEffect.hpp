#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    class GraphicsDevice;

    /**
     * @brief Minimal subset of XNA's `BasicEffect` for colored vertices.
     *
     * Acts as a small bag of `World`/`View`/`Projection` matrices plus a
     * `VertexColorEnabled` flag. The CNA 3D pipeline reads these values
     * directly when a draw is issued and forwards them to the backend's
     * built-in colored shader.
     *
     * @note Status: PARTIAL. Lighting, fog, textures and the
     *       `CurrentTechnique`/`EffectPass`/`Apply` infrastructure of XNA's
     *       full `Effect` system are intentionally not provided. `Apply()`
     *       is a no-op kept only for source-level compatibility.
     */
    class BasicEffect {
    public:
        explicit BasicEffect(GraphicsDevice& device);

        /** World transform (model -> world). */
        Matrix World      = Matrix::Identity();
        /** View transform (world -> camera). */
        Matrix View       = Matrix::Identity();
        /** Projection transform (camera -> clip). */
        Matrix Projection = Matrix::Identity();

        /**
         * @brief When true, the per-vertex color channel is used for output.
         *
         * The CNA 3D pipeline currently always uses vertex colors when
         * drawing `VertexPositionColor`; this flag is honored only as a
         * source-level XNA compatibility hint.
         */
        bool VertexColorEnabled = true;

        /**
         * @brief Activates this effect on its `GraphicsDevice` so the next
         *        `DrawPrimitives`/`DrawIndexedPrimitives` call uses its
         *        `World`/`View`/`Projection` matrices.
         *
         * In real XNA `BasicEffect.CurrentTechnique.Passes[0].Apply()` is
         * what binds the shader and uniforms. CNA collapses that into a
         * single `BasicEffect::Apply()` shortcut while keeping the
         * underlying intent the same: after `Apply()` returns, this effect
         * is the "current" effect on the device.
         */
        void Apply();

    private:
        GraphicsDevice* device_;
    };
}
