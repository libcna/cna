#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

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
     * Inherits from `Effect`, so the standard XNA flow is also supported:
     * @code
     *   foreach (var pass in effect.CurrentTechnique.Passes) {
     *       pass.Apply();
     *       // ...
     *   }
     * @endcode
     *
     * @note Status: PARTIAL. Lighting, fog, textures and full multi-pass
     *       technique infrastructure are intentionally not provided. The
     *       single default technique exposes one pass which simply
     *       re-binds this effect on the device.
     */
    class BasicEffect : public Effect {
    public:
        explicit BasicEffect(GraphicsDevice& device);

        /** World transform (model -> world). */
        Matrix World      = Matrix::getIdentityProperty();
        /** View transform (world -> camera). */
        Matrix View       = Matrix::getIdentityProperty();
        /** Projection transform (camera -> clip). */
        Matrix Projection = Matrix::getIdentityProperty();

        /**
         * @brief When true, the per-vertex color channel is used for output.
         *
         * The CNA 3D pipeline currently always uses vertex colors when
         * drawing `VertexPositionColor`; this flag is honored only as a
         * source-level XNA compatibility hint.
         */
        bool VertexColorEnabled = true;

    protected:
        /**
         * @brief Activates this effect on its `GraphicsDevice` so the next
         *        `DrawPrimitives` / `DrawIndexedPrimitives` call uses its
         *        `World`/`View`/`Projection` matrices.
         *
         * Invoked through `Effect::Apply()` and through
         * `CurrentTechnique.Passes[i].Apply()`.
         */
        void OnApply() override;
    };
}
