// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"

#include <atomic>

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        std::atomic<std::uint64_t> g_nextTechniqueId{1};
    }

    std::uint64_t EffectTechnique::NextId() { return g_nextTechniqueId.fetch_add(1, std::memory_order_relaxed); }

    EffectTechnique::EffectTechnique(Effect* owner, std::string name)
        : name_(std::move(name))
    {
        passes_.Add(EffectPass(owner, "P0", id_));
    }

    const std::string& EffectTechnique::getNameProperty() const { return name_; }

    EffectPassCollection& EffectTechnique::getPassesProperty() { return passes_; }
    const EffectPassCollection& EffectTechnique::getPassesProperty() const { return passes_; }

    EffectAnnotationCollection& EffectTechnique::getAnnotationsProperty() { return annotations_; }
    const EffectAnnotationCollection& EffectTechnique::getAnnotationsProperty() const { return annotations_; }

    std::uint64_t EffectTechnique::getIdInternal() const { return id_; }
}
