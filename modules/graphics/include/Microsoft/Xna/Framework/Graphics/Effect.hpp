// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <memory>
#include <string>
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

namespace CNA::Internal::Graphics
{
    struct StockCloneParameterSnapshot;
}

namespace CNA::TestSupport
{
    struct CompiledEffectTestAccess;
}

namespace Microsoft::Xna::Framework
{
    struct Matrix;
}

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class Texture;

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
         * @throws System::ObjectDisposedException If this effect has been disposed.
         * @throws System::ArgumentNullException If @p value is null.
         * @throws System::InvalidOperationException If @p value belongs to another effect.
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
         * @return Owning pointer to the cloned effect. Caller takes ownership.
         * @throws System::ObjectDisposedException If this effect has been disposed.
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
         * FX-071: lets a renderer's SpriteBatch integration recognize and bind a
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
         * @brief Whether this is one of XNA's five stock effects -- BasicEffect, AlphaTestEffect,
         *        DualTextureEffect, EnvironmentMapEffect, SkinnedEffect -- or derives from one,
         *        or is a base-Effect or EffectMaterial clone of one or of SpriteEffect.
         *
         * SpriteBatch asks it of a custom effect: a stock effect's vertex shader places sprites
         * with World*View*Projection. A virtual rather than dynamic_cast for the same reason as
         * IsExactStockSpriteEffectEXT(): a dynamic_cast to each stock type made every SpriteBatch
         * user link all five of them. XNA's clone of a stock effect runs the cloned stock shaders
         * (HND-TQ-25 D9), so its vertex shader places sprites the same way, with its own raw
         * WorldViewProj; FNA's clone of its SpriteEffect places them with its raw MatrixTransform.
         *
         * @return True for a clone of a stock effect here; true in the five stock effects.
         */
        CNAEXT [[nodiscard]] virtual bool IsStockEffectEXT() const noexcept
        {
            return IsStockCloneInternal();
        }

        /**
         * @brief The world, view and projection a draw through a stock-effect clone hands its
         *        renderer.
         *
         * XNA's base-Effect or EffectMaterial clone of a stock effect draws with the cloned stock
         * shaders, which place a vertex with the raw WorldViewProj parameter (HND-TQ-25 D9); a
         * clone of SpriteEffect uses MatrixTransform the same way. The renderer receives
         * identity, identity and that matrix -- the value its last Apply captured, or the current
         * value before any Apply. A stock effect itself answers the World, View and Projection
         * its last Apply latched (KI-GFX-STOCK-LATCH) and false before its first Apply.
         *
         * @param world Receives the world matrix.
         * @param view Receives the view matrix.
         * @param projection Receives the projection matrix.
         * @return True when this effect is such a clone, or a stock effect that has been applied,
         *         and the matrices were written; false, leaving them untouched, otherwise.
         */
        CNAEXT [[nodiscard]] bool GetStockCloneDrawMatricesInternal(
            Microsoft::Xna::Framework::Matrix& world,
            Microsoft::Xna::Framework::Matrix& view,
            Microsoft::Xna::Framework::Matrix& projection) const;

        /**
         * @brief Fills a GpuDrawParams struct with this effect's current render parameters.
         *
         * Called by GraphicsDevice before every draw call so the renderer can select the
         * correct shader variant and upload uniforms. The base implementation forwards a
         * compiled effect's runtime, and for a clone of a stock effect the stock shader variant
         * and raw parameter values its last Apply captured; concrete effect classes override it
         * to populate texture, color, lighting, and other renderer-relevant fields.
         *
         * @param params Output struct to populate.
         */
        CNAEXT virtual void FillGpuDrawParams(CNA::Internal::Renderers::GpuDrawParams& params) const;

    protected:
        /**
         * @brief Constructs a stock effect with its exact reflected technique name.
         *
         * XNA's stock effects are backed by embedded compiled blobs, so their technique names are
         * observable through the ordinary Effect API even when CNA implements their draw path
         * directly. This constructor preserves that metadata without exposing a mutable-name API:
         * one technique with the given name, holding the single unnamed ("") pass of the
         * embedded effect.
         *
         * @param device The graphics device that owns the stock effect.
         * @param stockTechniqueName The exact technique name exposed by XNA's embedded effect.
         */
        CNAEXT Effect(GraphicsDevice& device, const std::string& stockTechniqueName);

        /**
         * @brief Adds a stock parameter with the reflected-effect getter contract.
         * @param parameters Collection receiving the parameter.
         * @param name Parameter name.
         * @param rows Reflected row count.
         * @param columns Reflected column count.
         * @param parameterClass Reflected value class.
         * @param parameterType Reflected value type.
         */
        CNAEXT static void AddStockParameterInternal(EffectParameterCollection& parameters,
            const std::string& name, int rows, int columns,
            EffectParameterClass parameterClass, EffectParameterType parameterType);

        /**
         * @brief Adds a fixed-capacity stock matrix array with shared element views.
         * @param name Parameter name.
         * @param rows Matrix row count.
         * @param columns Matrix column count.
         * @param count Number of matrices.
         */
        CNAEXT void AddStockMatrixArrayInternal(const std::string& name,
            int rows, int columns, int count);

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

        /**
         * @brief Throws when this effect cannot be used as a clone source because it is disposed.
         *
         * @throws System::ObjectDisposedException If this effect has been disposed.
         */
        CNAEXT void ThrowIfDisposedForCloneInternal() const;

        /**
         * @brief Binds a stock effect's texture to a device sampler slot, as XNA's Apply does.
         *
         * XNA's stock effects are compiled effects whose samplers name their texture
         * parameters, so Apply sets each one on GraphicsDevice.Textures -- a null one too --
         * and a draw samples what the slot holds by then: a texture a game sets on the
         * device after Apply is the one drawn.
         *
         * @param slot The sampler slot.
         * @param texture The effect's texture, or null.
         */
        CNAEXT void BindStockTextureInternal(int slot, Texture* texture);

        /**
         * @brief CNAEXT: the texture a parameter holds, whichever SetValue overload stored it.
         *
         * @param parameter The parameter.
         * @return Its texture, or null.
         */
        CNAEXT [[nodiscard]] static Texture* ParameterTextureInternal(const EffectParameter& parameter) noexcept;

        /**
         * @brief CNAEXT: copies the texture reference one parameter holds into another, as a clone
         *        copies it -- without the checks of a new assignment.
         *
         * @param target The parameter receiving the reference.
         * @param source The parameter whose reference is copied.
         */
        CNAEXT static void CopyParameterTextureInternal(EffectParameter& target,
                                                        const EffectParameter& source) noexcept;

        /**
         * @brief The texture a draw with this stock effect samples at a slot.
         *
         * The device's binding while this effect is the applied one; otherwise, as when
         * draw parameters are read without an Apply, the effect's own texture.
         *
         * @param slot The sampler slot.
         * @param own The effect's own texture for the slot.
         * @return The texture to sample, or null.
         */
        CNAEXT [[nodiscard]] Texture* SampledStockTextureInternal(int slot, Texture* own) const;

        /**
         * @brief Hands a draw what this stock effect's last Apply latched.
         *
         * XNA's OnApply writes a stock effect's derived parameters and its native pass latches them
         * when it begins (EffectPass.Apply IL_003b, IL_009f): a property set after Apply reaches the
         * draws only through the next Apply. The samplers still read what the device's slots hold at
         * the draw, so the textures are taken from those slots then.
         *
         * @param params Receives the latched draw parameters.
         * @return True when the effect has been applied and @p params was written; false otherwise.
         */
        CNAEXT [[nodiscard]] bool FillLatchedStockDrawParamsInternal(
            CNA::Internal::Renderers::GpuDrawParams& params) const;

        /** @brief Which of XNA's five classic stock effects, or FNA's SpriteEffect, an effect is, if any. */
        enum class CNAEXT StockEffectKindInternal : std::uint8_t
        {
            /** @brief Not a stock effect. */
            None,
            /** @brief BasicEffect. */
            Basic,
            /** @brief AlphaTestEffect. */
            AlphaTest,
            /** @brief DualTextureEffect. */
            DualTexture,
            /** @brief EnvironmentMapEffect. */
            EnvironmentMap,
            /** @brief SkinnedEffect. */
            Skinned,
            /** @brief SpriteEffect, FNA's (XNA 4.0 has none). */
            Sprite,
        };

        /**
         * @brief The stock effect class this object itself is.
         *
         * Each of the five stock classes answers its own kind, so a base-Effect or EffectMaterial
         * clone of it can remember whose stock shaders it runs (HND-TQ-25 D9). The stock classes
         * draw through their own FillGpuDrawParams, so the answer also tells the base Effect that
         * it is not itself such a clone.
         *
         * @return StockEffectKindInternal::None here.
         */
        CNAEXT [[nodiscard]] virtual StockEffectKindInternal GetStockEffectKindInternal() const noexcept
        {
            return StockEffectKindInternal::None;
        }

        /**
         * @brief Constructs an effect that is a clone of @p cloneSource.
         *
         * XNA's `protected Effect(Effect cloneSource)`: the constructor a subclass such as
         * EffectMaterial uses to become its own instance of an already-loaded effect. A
         * compiled effect's renderer runtime is cloned, its object graph rebuilt, and the
         * source's mutable parameter values carried across. A source with no compiled runtime --
         * a stock effect, for one -- clones to the same techniques, passes and parameters, each
         * holding the source's current value in storage of the clone's own. The clone selects its
         * first technique, as XNA does. A clone of a stock effect keeps drawing with that stock
         * effect's shaders, fed its own raw parameter values (HND-TQ-25 D9).
         *
         * Protected rather than public, exactly as in XNA: an Effect is a reference type
         * there and games never copy one, but subclasses must be able to.
         *
         * @param cloneSource The effect to clone.
         */
        Effect(const Effect& cloneSource);

        /** @brief The graphics device that owns this effect. */
        GraphicsDevice* device_;

    private:
        Effect(GraphicsDevice& device,
               std::unique_ptr<CNA::Internal::Renderers::ICompiledEffectRuntime> runtime,
               const Effect* cloneSource);

        void BuildCompiledObjectGraph();
        void AdoptCloneState(const Effect& cloneSource);
        // The clone constructor's path for a source with no compiled runtime.
        void CopyUncompiledObjectGraphInternal(const Effect& source);
        [[nodiscard]] EffectParameter CloneUncompiledParameterInternal(
            const EffectParameter& source);
        [[nodiscard]] EffectParameter BuildCompiledParameter(
            const CNA::Internal::Renderers::CompiledEffectParameterDescription& description);
        void ApplyPassInternal(std::uint32_t passIndex);
        void ApplyCompiledPassState(std::uint32_t passIndex);
        void SyncCompiledParameters();

        // HND-TQ-25 D9: a base-Effect/EffectMaterial clone of a stock effect (not a stock class).
        [[nodiscard]] bool IsStockCloneInternal() const noexcept
        {
            return stockCloneKind_ != StockEffectKindInternal::None &&
                   GetStockEffectKindInternal() == StockEffectKindInternal::None;
        }
        // Reads the clone's raw parameter values, refusing a ShaderIndex no stock shader answers.
        [[nodiscard]] CNA::Internal::Graphics::StockCloneParameterSnapshot
            ReadStockCloneParametersInternal() const;
        // Apply's native-pass part for such a clone: binds the textures its shaders sample and
        // captures the parameter values the next draws use.
        void ApplyStockClonePassInternal();
        // KI-GFX-STOCK-LATCH: a stock class's Apply keeps the draw parameters and matrices its draws
        // use until the next Apply.
        void LatchStockClassDrawInternal();
        // Hands the renderer the captured values in its stock-path vocabulary.
        void FillStockCloneDrawParamsInternal(CNA::Internal::Renderers::GpuDrawParams& params) const;

        /**
         * @brief False: XNA never raises Disposing for an Effect or any type derived from it.
         *
         * '!Effect' IL_0008-000a sets isDisposed before Effect::Dispose(bool) reaches
         * GraphicsResource.Dispose(true) (IL_000b-000d); the stock effects do not override it.
         *
         * @return false.
         */
        [[nodiscard]] bool RaisesDisposingEventInternal() const noexcept override;

        EffectParameterCollection parameters_;
        EffectTechniqueCollection techniques_;
        EffectTechnique* currentTechnique_ = nullptr;
        std::unique_ptr<CNA::Internal::Renderers::ICompiledEffectRuntime> compiledRuntime_;
        // HND-TQ-25 D9: whose stock shaders this clone runs, and what its last Apply captured.
        StockEffectKindInternal stockCloneKind_ = StockEffectKindInternal::None;
        std::unique_ptr<CNA::Internal::Graphics::StockCloneParameterSnapshot> stockCloneApplied_;

        friend class EffectPass;
        /** @brief Allows conformance tests to exercise staged runtimes before capability enablement. */
        CNAEXT friend struct CNA::TestSupport::CompiledEffectTestAccess;
    };
}
