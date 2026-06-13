// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    /**
     * @brief Base class for an effect that contains shader programs and render-state parameters.
     */
    class Effect : public GraphicsResource
    {
    public:
        /**
         * @brief Constructs an Effect for the given graphics device.
         *
         * @param device The graphics device that will own this effect.
         */
        explicit Effect(GraphicsDevice& device);

        /** @brief Destroys the effect and releases its GPU resources. */
        ~Effect() override;

        /** @brief Copying is not allowed. */
        Effect(const Effect&) = delete;

        /** @brief Copy-assignment is not allowed. */
        Effect& operator=(const Effect&) = delete;

        /**
         * @brief Gets the currently active technique.
         *
         * @return Pointer to the active EffectTechnique, or nullptr if none is set.
         */
        [[nodiscard]] EffectTechnique* getCurrentTechniqueProperty() const;

        /**
         * @brief Sets the currently active technique.
         *
         * @param value Pointer to the technique to activate.
         */
        void setCurrentTechniqueProperty(EffectTechnique* value);

        /**
         * @brief Gets the collection of effect parameters (mutable overload).
         *
         * @return Reference to the parameter collection.
         */
        [[nodiscard]] EffectParameterCollection& getParametersProperty();

        /**
         * @brief Gets the collection of effect parameters (const overload).
         *
         * @return Const reference to the parameter collection.
         */
        [[nodiscard]] const EffectParameterCollection& getParametersProperty() const;

        /**
         * @brief Gets the collection of techniques defined in this effect (mutable overload).
         *
         * @return Reference to the technique collection.
         */
        [[nodiscard]] EffectTechniqueCollection& getTechniquesProperty();

        /**
         * @brief Gets the collection of techniques defined in this effect (const overload).
         *
         * @return Const reference to the technique collection.
         */
        [[nodiscard]] const EffectTechniqueCollection& getTechniquesProperty() const;

        /**
         * @brief Applies the effect state to the graphics device ready for rendering.
         *
         * Calls OnApply() on the active technique's current pass.
         */
        void Apply();

        /**
         * @brief Returns the fully-qualified .NET type name of this object.
         *
         * @return The string "Microsoft.Xna.Framework.Graphics.Effect".
         */
        [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Returns the graphics device that owns this effect.
         *
         * @return Reference to the owning GraphicsDevice.
         */
        NOXNA [[nodiscard]] GraphicsDevice& getGraphicsDeviceInternal() const;

    protected:
        /**
         * @brief Derived classes override this to upload shader parameters to the GPU before drawing.
         */
        virtual void OnApply() = 0;

        /**
         * @brief Releases managed and unmanaged resources held by this effect.
         *
         * @param disposing True if called from Dispose(); false if called from a finalizer.
         */
        void Dispose(bool disposing) override;

        /** @brief The graphics device that owns this effect. */
        GraphicsDevice* device_;

    private:
        EffectParameterCollection parameters_;
        EffectTechniqueCollection techniques_;
        EffectTechnique* currentTechnique_ = nullptr;
    };
}
