#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;

    /**
     * @brief XNA 4.0 `EffectTechnique`.
     *
     * Holds an ordered list of `EffectPass` objects. The CNA `BasicEffect`
     * always exposes exactly one technique with one pass.
     *
     * @note Status: PARTIAL. Multi-technique / multi-pass effects are not
     *       implemented yet; the type exists for API-shape compatibility.
     */
    class EffectTechnique
    {
    public:
        EffectTechnique() = default;

        EffectTechnique(Effect* owner, std::string name)
            : name_(std::move(name))
        {
            // Default: a single "Default" pass that re-applies the owning
            // effect's parameters on the device. Matches what XNA's
            // BasicEffect ships with for the colored / textured passes.
            passes_.emplace_back(owner, "P0");
        }

        /** Technique name (XNA: `EffectTechnique.Name`). */
        [[nodiscard]] const std::string& Name() const { return name_; }

        /** XNA: `EffectTechnique.Passes`. */
        [[nodiscard]] std::vector<EffectPass>& Passes() { return passes_; }
        [[nodiscard]] const std::vector<EffectPass>& Passes() const { return passes_; }

    private:
        std::string name_;
        std::vector<EffectPass> passes_;
    };
}
