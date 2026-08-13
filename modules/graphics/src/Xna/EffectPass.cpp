// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "System/InvalidOperationException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    EffectPass::EffectPass(Effect* owner, std::string name, std::uint64_t techniqueId,
                           std::uint32_t passIndex)
        : owner_(owner), name_(std::move(name)), techniqueId_(techniqueId), passIndex_(passIndex)
    {
    }

    const std::string& EffectPass::getNameProperty() const { return name_; }

    EffectAnnotationCollection& EffectPass::getAnnotationsProperty() { return annotations_; }
    const EffectAnnotationCollection& EffectPass::getAnnotationsProperty() const { return annotations_; }

    void EffectPass::Apply()
    {
        if (!owner_) return;

        const EffectTechnique* current = owner_->getCurrentTechniqueProperty();
        if (current == nullptr || current->getIdInternal() != techniqueId_)
        {
            throw System::InvalidOperationException("Applied a pass not in the current technique!");
        }

        owner_->ApplyPassInternal(passIndex_);
    }
}
