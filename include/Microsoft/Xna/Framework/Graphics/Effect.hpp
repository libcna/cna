#pragma once

#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    class GraphicsDevice;

    /**
     * @brief XNA 4.0 `Effect` base class.
     *
     * Owns a `GraphicsDevice` reference and one or more techniques. The
     * derived class is responsible for exposing typed parameters (e.g.
     * `BasicEffect::World`) and for the actual on-device activation logic
     * inside `OnApply()`.
     *
     * @note Status: PARTIAL. Only the minimum surface needed by
     *       `BasicEffect` and the cube3d demo is implemented: a single
     *       `CurrentTechnique` containing a single `EffectPass`.
     */
    class Effect {
    public:
        explicit Effect(GraphicsDevice& device)
            : device_(&device),
              currentTechnique_(this, "Default") {}

        virtual ~Effect() = default;

        Effect(const Effect&) = delete;
        Effect& operator=(const Effect&) = delete;

        /** XNA: `Effect.GraphicsDevice`. */
        [[nodiscard]] GraphicsDevice& GraphicsDeviceRef() const { return *device_; }

        /** XNA: `Effect.CurrentTechnique`. */
        [[nodiscard]] EffectTechnique& CurrentTechnique() { return currentTechnique_; }
        [[nodiscard]] const EffectTechnique& CurrentTechnique() const { return currentTechnique_; }

        /**
         * @brief XNA-style entry point that activates the effect on its
         *        device for the next draw call.
         *
         * Equivalent to calling `CurrentTechnique.Passes[0].Apply()`.
         */
        void Apply() { OnApply(); }

    protected:
        /**
         * @brief Subclass hook that performs the actual on-device binding.
         */
        virtual void OnApply() = 0;

        GraphicsDevice* device_;

    private:
        EffectTechnique currentTechnique_;
    };
}
