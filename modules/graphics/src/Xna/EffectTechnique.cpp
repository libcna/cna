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
        : EffectTechnique(owner, std::move(name), 0, true)
    {
    }

    EffectTechnique::EffectTechnique(Effect* owner, std::string name,
                                     std::uint32_t techniqueIndex, bool addDefaultPass)
        : name_(std::move(name)), techniqueIndex_(techniqueIndex)
    {
        if (addDefaultPass)
        {
            passes_.Add(EffectPass(owner, "P0", id_, 0));
        }
    }

    const std::string& EffectTechnique::getNameProperty() const { return name_; }

    EffectPassCollection& EffectTechnique::getPassesProperty() { return passes_; }
    const EffectPassCollection& EffectTechnique::getPassesProperty() const { return passes_; }

    EffectAnnotationCollection& EffectTechnique::getAnnotationsProperty() { return annotations_; }
    const EffectAnnotationCollection& EffectTechnique::getAnnotationsProperty() const { return annotations_; }

    std::uint64_t EffectTechnique::getIdInternal() const { return id_; }
    std::uint32_t EffectTechnique::getIndexInternal() const { return techniqueIndex_; }
}
