#pragma once

#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;

    /**
     * @brief XNA 4.0 `EffectPass`.
     *
     * Minimal pass object owned by an `EffectTechnique`. `Apply()` activates
     * the parent `Effect` on its `GraphicsDevice` so the next draw call uses
     * its parameters.
     *
     * @note Status: PARTIAL. The CNA effect system currently exposes a
     *       single pass per effect; multi-pass shaders are not implemented.
     */
    class EffectPass
    {
    public:
        EffectPass(Effect* owner, std::string name)
            : owner_(owner), name_(std::move(name))
        {
        }

        /** Pass name (XNA: `EffectPass.Name`). */
        [[nodiscard]] const std::string& Name() const { return name_; }

        /**
         * @brief Activates the owning `Effect` on its device.
         *
         * Equivalent to `effect.CurrentTechnique.Passes[i].Apply()` in XNA.
         */
        void Apply();

    private:
        Effect* owner_;
        std::string name_;
    };
}
