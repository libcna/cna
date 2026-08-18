// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/EffectPass.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <stdexcept>
#include <utility>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::Effect;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    EffectPass::EffectPass(GraphicsDevice& device, Effect* effect, std::string name)
        : device_(device)
        , effect_(effect)
        , fullscreen_(std::make_unique<FullscreenPass>(device))
        , name_(std::move(name))
    {
        if (name_.empty())
            throw std::invalid_argument("CNA::Graphics::EffectPass: the pass needs a name -- it is "
                                        "what a pipeline's statistics and any diagnostic call it");
    }

    EffectPass::EffectPass(GraphicsDevice& device, std::unique_ptr<Effect> effect, std::string name)
        : EffectPass(device, effect.get(), std::move(name))
    {
        ownedEffect_ = std::move(effect);
    }

    EffectPass::~EffectPass() = default;

    void EffectPass::apply(const PostProcessContext& context)
    {
        // A null effect draws the source through unchanged rather than refusing. That is the same
        // fallback every unsupported pass takes, and it keeps a chain runnable while a game is
        // still loading the shader it means to use.
        fullscreen_->draw(context.source, context.destination, effect_, context.width,
                          context.height);
    }

    const std::string& EffectPass::getName() const { return name_; }

    bool EffectPass::isSupported(GraphicsDevice& device) const
    {
        // Deliberately *not* the base class's two-part question. The base asks whether the renderer
        // executes shader *source*, which is right for the passes that carry GLSL of their own; an
        // EffectPass runs whatever Effect it was handed, and a compiled or stock effect is real
        // work on renderers that never compile source (plan_modern.md MOD-1699 draws that line).
        return effect_ != nullptr && device.SupportsCapability(CNA::GraphicsCapability::CustomEffects);
    }

    Effect* EffectPass::getEffect() const { return effect_; }

    void EffectPass::setEffect(Effect* effect)
    {
        ownedEffect_.reset();
        effect_ = effect;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
