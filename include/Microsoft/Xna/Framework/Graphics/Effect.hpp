// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    /// Base class for an effect that contains shader programs and parameters.
    class Effect : public GraphicsResource
    {
    public:
        /// Constructs an Effect for the given graphics device.
        explicit Effect(GraphicsDevice& device);
        /// Destroys the effect and releases its resources.
        ~Effect() override;

        /// Copying is not allowed.
        Effect(const Effect&) = delete;
        /// Copy-assignment is not allowed.
        Effect& operator=(const Effect&) = delete;

        /// Gets the currently active technique.
        [[nodiscard]] EffectTechnique* getCurrentTechniqueProperty() const;
        /// Sets the currently active technique.
        void setCurrentTechniqueProperty(EffectTechnique* value);

        /// Gets the collection of effect parameters.
        [[nodiscard]] EffectParameterCollection& getParametersProperty();
        /// Gets the collection of effect parameters (const overload).
        [[nodiscard]] const EffectParameterCollection& getParametersProperty() const;

        /// Gets the collection of techniques defined in this effect.
        [[nodiscard]] EffectTechniqueCollection& getTechniquesProperty();
        /// Gets the collection of techniques defined in this effect (const overload).
        [[nodiscard]] const EffectTechniqueCollection& getTechniquesProperty() const;

        /// Applies the effect state to the graphics device ready for rendering.
        void Apply();

        /// Returns the fully-qualified .NET type name of this object.
        [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        /// Derived classes override this to upload shader parameters to the GPU.
        virtual void OnApply() = 0;
        /// Releases managed and unmanaged resources held by this effect.
        void Dispose(bool disposing) override;

        GraphicsDevice* device_;

    private:
        EffectParameterCollection parameters_;
        EffectTechniqueCollection techniques_;
        EffectTechnique* currentTechnique_ = nullptr;
    };
}
