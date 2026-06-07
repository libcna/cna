#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"

#include <algorithm>

namespace Microsoft::Xna::Framework::Graphics
{
    Effect* ModelEffectCollection::operator[](int index) const
    {
        return effects_[static_cast<std::size_t>(index)];
    }

    int ModelEffectCollection::getCountProperty() const
    {
        return static_cast<int>(effects_.size());
    }

    bool ModelEffectCollection::Contains(const Effect* effect) const
    {
        return std::find(effects_.begin(), effects_.end(), effect) != effects_.end();
    }

    void ModelEffectCollection::Add(Effect* effect)
    {
        effects_.push_back(effect);
    }

    void ModelEffectCollection::Remove(const Effect* effect)
    {
        auto it = std::find(effects_.begin(), effects_.end(), effect);
        if (it != effects_.end())
            effects_.erase(it);
    }

    ModelEffectCollection::iterator ModelEffectCollection::begin() { return effects_.begin(); }
    ModelEffectCollection::iterator ModelEffectCollection::end()   { return effects_.end(); }
    ModelEffectCollection::const_iterator ModelEffectCollection::begin() const { return effects_.begin(); }
    ModelEffectCollection::const_iterator ModelEffectCollection::end()   const { return effects_.end(); }
}
