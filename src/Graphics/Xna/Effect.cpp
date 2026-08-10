// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "System/NotImplementedException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    Effect::Effect(GraphicsDevice& device)
        : GraphicsResource(&device)
        , device_(&device)
    {
        techniques_.Add(EffectTechnique(this, "Default"));
        currentTechnique_ = &techniques_[0];
    }

    Effect::Effect(GraphicsDevice& device, const std::vector<SharpRuntime::bytecs>& /*effectCode*/)
        : GraphicsResource(&device)
        , device_(&device)
    {
        throw System::NotImplementedException(
            "Effect(GraphicsDevice&, const std::vector<bytecs>&): compiled XNA .fx bytecode "
            "is not yet supported (tracked as Phase 74, see docs/fx-bytecode-support-plan.md). "
            "Use a hand-authored ShaderEffect or one of the built-in stock effects instead.");
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

    void Effect::Apply()
    {
        if (isDisposed_)
            throw System::ObjectDisposedException(getNameProperty());
        OnApply();
        if (device_) device_->SetCurrentEffect(this);
    }

    void Effect::FillGpuDrawParams(CNA::Internal::Backends::GpuDrawParams& /*params*/) const
    {
        // Default: no-op. Concrete subclasses override to populate shader parameters.
    }

    const std::string& Effect::GetVertexSource() const
    {
        static const std::string empty;
        return empty;
    }

    const std::string& Effect::GetFragmentSource() const
    {
        static const std::string empty;
        return empty;
    }

    CNA::Internal::Backends::IEffectBackend* Effect::GetEffectBackendPtr() const
    {
        return nullptr;
    }

    const std::string& Effect::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.Effect";
        return name;
    }
}
