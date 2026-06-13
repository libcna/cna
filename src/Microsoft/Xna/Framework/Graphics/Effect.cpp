// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    Effect::Effect(GraphicsDevice& device)
        : GraphicsResource(&device)
        , device_(&device)
    {
        techniques_.Add(EffectTechnique(this, "Default"));
        currentTechnique_ = &techniques_[0];
    }

    Effect::~Effect()
    {
        Dispose(false);
    }

    void Effect::Dispose(bool disposing)
    {
        GraphicsResource::Dispose(disposing);
    }

    GraphicsDevice& Effect::getGraphicsDeviceInternal() const { return *device_; }

    EffectTechnique* Effect::getCurrentTechniqueProperty() const { return currentTechnique_; }

    void Effect::setCurrentTechniqueProperty(EffectTechnique* value)
    {
        currentTechnique_ = value;
    }

    EffectParameterCollection& Effect::getParametersProperty() { return parameters_; }
    const EffectParameterCollection& Effect::getParametersProperty() const { return parameters_; }

    EffectTechniqueCollection& Effect::getTechniquesProperty() { return techniques_; }
    const EffectTechniqueCollection& Effect::getTechniquesProperty() const { return techniques_; }

    void Effect::Apply() { OnApply(); }

    const std::string& Effect::GetTypeName() const
    {
        static const std::string name = "Effect";
        return name;
    }
}
