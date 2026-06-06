#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    EffectPass::EffectPass(Effect* owner, std::string name)
        : owner_(owner), name_(std::move(name))
    {
    }

    const std::string& EffectPass::getNameProperty() const { return name_; }

    EffectAnnotationCollection& EffectPass::getAnnotationsProperty() { return annotations_; }
    const EffectAnnotationCollection& EffectPass::getAnnotationsProperty() const { return annotations_; }

    void EffectPass::Apply()
    {
        if (owner_) owner_->Apply();
    }
}
