// SPDX-License-Identifier: MS-PL
#pragma once

#include <vector>

#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace CNA::Internal::Renderers { struct GpuDrawParams; class IEffectRenderer; }

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    /**
     * @brief Base class for an effect that contains shader programs and render-state parameters.
     */
    class Effect : public GraphicsResource
    {
    public:
        using GraphicsResource::Dispose;

        /**
         * @brief Constructs an Effect for the given graphics device.
         *
         * @param device The graphics device that will own this effect.
         */
        explicit Effect(GraphicsDevice& device);

        /**
         * @brief Constructs an Effect from a compiled XNA effect bytecode blob.
         *
         * @param device     The graphics device that will own this effect.
         * @param effectCode The compiled effect bytecode, as produced by the XNA Content
         *                   Pipeline's EffectProcessor.
         *
         * @throws System::NotImplementedException Always. CNA has no MojoShader-equivalent
         *         bytecode parser/translator yet — full support for compiled `.fx` bytecode
         *         is tracked as Phase 74 (see docs/fx-bytecode-support-plan.md). Until that
         *         lands, use a hand-authored ShaderEffect (custom GLSL/SPIR-V source) or one
         *         of the built-in stock effects (BasicEffect, AlphaTestEffect,
         *         DualTextureEffect, EnvironmentMapEffect, SkinnedEffect, SpriteEffect)
         *         instead.
         */
        Effect(GraphicsDevice& device, const std::vector<SharpRuntime::bytecs>& effectCode);

        /** @brief Destroys the effect and releases its GPU resources. */
        NOXNA ~Effect() override;

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
         *
         * @note NOXNA — FNA has no public Effect::Apply(); it only exposes
         * EffectPass::Apply() (which internally calls Effect.OnApply()).
         */
        NOXNA void Apply();

        /**
         * @brief Creates an independent clone of this effect.
         *
         * The clone gets its own Parameters/Techniques collections (built fresh against the
         * clone's own identity, not copied from the original), with the same current values;
         * mutating a parameter on either the clone or the original never affects the other.
         *
         * @return Owning pointer to the cloned effect, with the same concrete runtime type as
         * this object. Caller takes ownership.
         *
         * @note NOXNA return-type deviation — FNA's Clone() returns a GC-managed Effect
         * reference; CNA has no garbage collector, so ownership is transferred to the caller
         * via a raw owning pointer instead, matching this codebase's established pattern for
         * factory-shaped methods that hand off a new heap object.
         */
        [[nodiscard]] virtual Effect* Clone() = 0;

        /**
         * @brief Returns the fully-qualified .NET type name of this object.
         *
         * @return The string "Microsoft.Xna.Framework.Graphics.Effect".
         */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Returns the graphics device that owns this effect.
         *
         * @return Reference to the owning GraphicsDevice.
         */
        NOXNA [[nodiscard]] GraphicsDevice& getGraphicsDeviceInternal() const;

        /**
         * @brief Returns the GLSL vertex shader source if this is a source-based effect, or empty string.
         *
         * Overridden by ShaderEffect. Used by renderers to compile custom programs without
         * a dependency on the concrete ShaderEffect type.
         */
        NOXNA [[nodiscard]] virtual const std::string& GetVertexSource() const;

        /**
         * @brief Returns the GLSL fragment shader source if this is a source-based effect, or empty string.
         *
         * Overridden by ShaderEffect. Used by renderers to compile custom programs without
         * a dependency on the concrete ShaderEffect type.
         */
        NOXNA [[nodiscard]] virtual const std::string& GetFragmentSource() const;

        /**
         * @brief Returns the renderer-specific compiled program for this effect, if it is a
         * source-based effect (e.g. ShaderEffect), or nullptr.
         *
         * Overridden by ShaderEffect. Lets a renderer (e.g. SpriteBatch) bind the SAME compiled
         * program the effect itself uses, instead of maintaining a redundant separate copy that
         * a caller's SetUniformXxx() calls would never actually reach.
         */
        NOXNA [[nodiscard]] virtual CNA::Internal::Renderers::IEffectRenderer* GetEffectRendererPtr() const;

        /**
         * @brief Returns true only for an exact stock SpriteEffect instance.
         *
         * Renderer code can recognize the stock sprite alias without linking directly against the
         * concrete SpriteEffect RTTI. The base answer is false; SpriteEffect's override also
         * rejects derived runtime types so custom OnApply behavior is never silently discarded.
         */
        NOXNA [[nodiscard]] virtual bool IsExactStockSpriteEffectEXT() const noexcept
        {
            return false;
        }

        /**
         * @brief Fills a GpuDrawParams struct with this effect's current render parameters.
         *
         * Called by GraphicsDevice before every draw call so the renderer can select the
         * correct shader variant and upload uniforms. Default implementation is a no-op;
         * concrete effect classes override it to populate texture, color, lighting, and
         * other renderer-relevant fields.
         *
         * @param params Output struct to populate.
         */
        NOXNA virtual void FillGpuDrawParams(CNA::Internal::Renderers::GpuDrawParams& params) const;

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
