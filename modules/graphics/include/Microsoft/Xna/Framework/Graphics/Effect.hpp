// SPDX-License-Identifier: MS-PL
#pragma once

#include <vector>

#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace CNA::Internal::Renderers
{
    struct GpuDrawParams;
    struct CompiledEffectParameterDescription;
    class IEffectRenderer;
    class ICompiledEffectRuntime;
}

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
         * @throws System::ArgumentException If the bytecode is empty, oversized, malformed, or
         *         not an XNA/FNA Direct3D 9 Effect Framework binary.
         * @throws System::NotSupportedException If the active renderer does not advertise
         *         CNA::GraphicsCapability::CompiledEffects, or if @p effectCode is an MGFX
         *         container. FNA3D is the first supported backend; other renderers reject the
         *         format explicitly instead of substituting a stock or source-based shader.
         */
        Effect(GraphicsDevice& device, const std::vector<SharpRuntime::bytecs>& effectCode);

        /** @brief Destroys the effect and releases its GPU resources. */
        CNAEXT ~Effect() override;

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
         * @note CNAEXT — FNA has no public Effect::Apply(); it only exposes
         * EffectPass::Apply() (which internally calls Effect.OnApply()).
         */
        CNAEXT void Apply();

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
         * @note CNAEXT return-type deviation — FNA's Clone() returns a GC-managed Effect
         * reference; CNA has no garbage collector, so ownership is transferred to the caller
         * via a raw owning pointer instead, matching this codebase's established pattern for
         * factory-shaped methods that hand off a new heap object.
         */
        [[nodiscard]] virtual Effect* Clone();

        /**
         * @brief Returns the fully-qualified .NET type name of this object.
         *
         * @return The string "Microsoft.Xna.Framework.Graphics.Effect".
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Returns the graphics device that owns this effect.
         *
         * @return Reference to the owning GraphicsDevice.
         */
        CNAEXT [[nodiscard]] GraphicsDevice& getGraphicsDeviceInternal() const;

        /**
         * @brief Returns the GLSL vertex shader source if this is a source-based effect, or empty string.
         *
         * Overridden by ShaderEffect. Used by renderers to compile custom programs without
         * a dependency on the concrete ShaderEffect type.
         */
        CNAEXT [[nodiscard]] virtual const std::string& GetVertexSource() const;

        /**
         * @brief Returns the GLSL fragment shader source if this is a source-based effect, or empty string.
         *
         * Overridden by ShaderEffect. Used by renderers to compile custom programs without
         * a dependency on the concrete ShaderEffect type.
         */
        CNAEXT [[nodiscard]] virtual const std::string& GetFragmentSource() const;

        /**
         * @brief Returns the renderer-specific compiled program for this effect, if it is a
         * source-based effect (e.g. ShaderEffect), or nullptr.
         *
         * Overridden by ShaderEffect. Lets a renderer (e.g. SpriteBatch) bind the SAME compiled
         * program the effect itself uses, instead of maintaining a redundant separate copy that
         * a caller's SetUniformXxx() calls would never actually reach.
         */
        CNAEXT [[nodiscard]] virtual CNA::Internal::Renderers::IEffectRenderer* GetEffectRendererPtr() const;

        /**
         * @brief Returns this effect's compiled XNA Effect Framework runtime, or nullptr if this is
         * not a compiled effect.
         *
         * plans/plan_fx.md FX-071: lets a renderer's SpriteBatch integration recognize and bind a
         * compiled effect the same way GpuDrawParams::compiledEffectRuntime already does for
         * ordinary 3D draws, without every SpriteBatch renderer needing its own friend access to
         * the private compiledRuntime_ member. Not virtual: every compiled effect shares this exact
         * base-class storage regardless of which renderer created its runtime.
         *
         * @return The compiled runtime, or nullptr.
         */
        CNAEXT [[nodiscard]] CNA::Internal::Renderers::ICompiledEffectRuntime* GetCompiledRuntimePtr() const
        {
            return compiledRuntime_.get();
        }

        /**
         * @brief Returns true only for an exact stock SpriteEffect instance.
         *
         * Renderer code can recognize the stock sprite alias without linking directly against the
         * concrete SpriteEffect RTTI. The base answer is false; SpriteEffect's override also
         * rejects derived runtime types so custom OnApply behavior is never silently discarded.
         */
        CNAEXT [[nodiscard]] virtual bool IsExactStockSpriteEffectEXT() const noexcept
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
        CNAEXT virtual void FillGpuDrawParams(CNA::Internal::Renderers::GpuDrawParams& params) const;

    protected:
        /**
         * @brief Derived classes override this to upload shader parameters to the GPU before drawing.
         */
        virtual void OnApply();

        /**
         * @brief Releases managed and unmanaged resources held by this effect.
         *
         * @param disposing True if called from Dispose(); false if called from a finalizer.
         */
        void Dispose(bool disposing) override;

        /** @brief The graphics device that owns this effect. */
        GraphicsDevice* device_;

    private:
        Effect(GraphicsDevice& device,
               std::unique_ptr<CNA::Internal::Renderers::ICompiledEffectRuntime> runtime,
               const Effect* cloneSource);

        void BuildCompiledObjectGraph();
        [[nodiscard]] EffectParameter BuildCompiledParameter(
            const CNA::Internal::Renderers::CompiledEffectParameterDescription& description);
        void ApplyPassInternal(std::uint32_t passIndex);
        void ApplyCompiledPassState(std::uint32_t passIndex);
        void SyncCompiledParameters();

        EffectParameterCollection parameters_;
        EffectTechniqueCollection techniques_;
        EffectTechnique* currentTechnique_ = nullptr;
        std::unique_ptr<CNA::Internal::Renderers::ICompiledEffectRuntime> compiledRuntime_;

        friend class EffectPass;
    };
}
