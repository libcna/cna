// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file OpenGL4CompiledEffect.hpp
 * @brief plans/plan_opengl4_modern_graphics.md GL4-0020: compiled XNA Effect Framework bytecode on
 *        the OpenGL4 renderer.
 *
 * The same design EasyGL's desktop profile uses (plans/plan_fx.md FX-062): MojoShader's own OpenGL
 * adapter (`mojoshader_opengl.c`) owns translation to GLSL 1.20, shader compilation, program
 * linking and uniform pushing; the renderer-neutral reflection and state translation live in
 * `CNA::Internal::Renderers::MojoShaderEffect`, shared with every other compiled-effect backend.
 * What is left here is this renderer's own share: creating and deleting the native effect against
 * the renderer's one `MOJOSHADER_glContext`, selecting a technique, and applying a pass.
 *
 * OpenGL4 draws immediately, so a compiled-effect draw reads the live register files at
 * `MOJOSHADER_glProgramReady()` time in the same call chain as `ApplyPass()`; the draw route lives
 * in OpenGL4CompiledEffects.cpp (`OpenGL4Renderer::BindCompiledEffectForDrawEXT`).
 *
 * Desktop OpenGL 4 has no context loss, so unlike EasyGL's runtime this one is never released and
 * rebuilt for a replacement context. It is released exactly once, early, when its renderer is
 * destroyed first (see `OpenGL4Renderer::~OpenGL4Renderer`).
 */

#if defined(CNA_OPENGL4_COMPILED_EFFECTS)

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"

#include "mojoshader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers
{
    class ITextureRenderer;
    class ITexture3DRenderer;
    class ITextureCubeRenderer;
}

namespace CNA::Internal::Renderers::OpenGL4
{
    class OpenGL4Renderer;

    namespace Detail
    {
        /**
         * @brief One compiled sampler's texture, resolved to whichever OpenGL4 kind backs it.
         *
         * `ITextureRenderer`, `ITexture3DRenderer` and `ITextureCubeRenderer` are unrelated
         * interfaces rather than a hierarchy, so a compiled sampler's texture cannot be carried as
         * one pointer (plans/plan_fx.md FX-110). Exactly one pointer is non-null when
         * @ref Resolved; the matching owning pointer keeps the native resource alive.
         */
        struct CompiledSamplerTexture
        {
            /** @brief A plain 2D texture or a render target's colour texture, or null. */
            const ITextureRenderer* texture2D = nullptr;
            /** @brief A volume texture, or null. */
            const ITexture3DRenderer* volume = nullptr;
            /** @brief A cube texture or render-target cube, or null. */
            const ITextureCubeRenderer* cube = nullptr;
            /** @brief Keeps @ref texture2D alive when it was resolved from a public texture. */
            std::shared_ptr<ITextureRenderer> ownedTexture2D;
            /** @brief Keeps @ref volume alive. */
            std::shared_ptr<ITexture3DRenderer> ownedVolume;
            /** @brief Keeps @ref cube alive. */
            std::shared_ptr<ITextureCubeRenderer> ownedCube;

            /**
             * @brief Whether a texture of this renderer was found.
             *
             * @return True when one of the three pointers is non-null.
             */
            [[nodiscard]] bool Resolved() const
            {
                return texture2D != nullptr || volume != nullptr || cube != nullptr;
            }

            /**
             * @brief The shader-side sampler dimension this texture can legally serve.
             *
             * @return MOJOSHADER_SAMPLER_CUBE, MOJOSHADER_SAMPLER_VOLUME or MOJOSHADER_SAMPLER_2D.
             */
            [[nodiscard]] MOJOSHADER_samplerType Kind() const
            {
                if (cube != nullptr) return MOJOSHADER_SAMPLER_CUBE;
                if (volume != nullptr) return MOJOSHADER_SAMPLER_VOLUME;
                return MOJOSHADER_SAMPLER_2D;
            }

            /**
             * @brief Binds the texture to @p unit through the renderer that owns it.
             *
             * @param unit Native texture unit; it is left active.
             */
            void BindGL(int unit) const;
        };

        /**
         * @brief Resolves a public texture of any dimension to its OpenGL4 renderer.
         *
         * A `RenderTarget2D` resolves to its colour texture exactly as a `Texture2D` does; its row
         * order is corrected when it is bound (plans/plan_fx.md FX-099).
         *
         * @param texture Public texture, or null.
         * @return The resolution; not @ref CompiledSamplerTexture::Resolved for null or for a
         *         texture another renderer created.
         */
        [[nodiscard]] CompiledSamplerTexture ResolveCompiledSamplerTexture(Texture* texture);
    }

    /**
     * @brief One compiled XNA effect owned by the OpenGL4 renderer.
     */
    class OpenGL4CompiledEffect final : public ICompiledEffectRuntime
    {
    public:
        /**
         * @brief Parses and compiles an Effect Framework binary for the renderer's GL context.
         *
         * @param renderer Owning renderer; supplies the MojoShader context.
         * @param effectCode Compiled effect bytes.
         * @param effectCodeLength Number of bytes in @p effectCode.
         * @throws std::invalid_argument if the buffer is empty or implausibly large.
         * @throws std::runtime_error if the parser rejects the content.
         */
        OpenGL4CompiledEffect(OpenGL4Renderer& renderer,
                              const std::uint8_t* effectCode,
                              std::size_t effectCodeLength);

        /** @brief Releases the native effect and leaves the renderer's registry. */
        ~OpenGL4CompiledEffect() override;

        OpenGL4CompiledEffect(const OpenGL4CompiledEffect&) = delete;
        OpenGL4CompiledEffect& operator=(const OpenGL4CompiledEffect&) = delete;

        /**
         * @brief Creates an independent copy carrying the same current values.
         *
         * @return The copy, owned by the same renderer.
         * @throws std::runtime_error if the owning renderer has already been destroyed.
         */
        [[nodiscard]] std::unique_ptr<ICompiledEffectRuntime> Clone() const override;

        /**
         * @brief Returns the reflection the public Effect object graph is built from.
         *
         * @return The reflected parameters and techniques.
         */
        [[nodiscard]] const CompiledEffectDescription& GetDescription() const override;

        /**
         * @brief Selects a technique by its stable zero-based index.
         *
         * @param techniqueIndex Index into the reflected technique list.
         * @throws std::out_of_range if the index is not a technique of this effect.
         */
        void SetTechnique(std::uint32_t techniqueIndex) override;

        /**
         * @brief Replaces one top-level parameter's padded raw value storage.
         *
         * @param runtimeIndex Reflected parameter index.
         * @param data Padded value bytes.
         * @param dataBytes Number of bytes at @p data.
         * @throws std::out_of_range if the parameter does not exist.
         * @throws std::invalid_argument if the value does not fit the parameter's storage.
         */
        void SetParameterValue(std::uint32_t runtimeIndex,
                               const void* data,
                               std::size_t dataBytes) override;

        /**
         * @brief Associates a texture parameter with a texture this renderer owns.
         *
         * @param runtimeIndex Reflected parameter index.
         * @param texture Texture to bind, or null to clear.
         * @throws std::out_of_range if the parameter does not exist.
         * @throws std::invalid_argument if the parameter is not a texture, or the texture was not
         *         created by this renderer.
         */
        void SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture) override;

        /**
         * @brief Applies one pass of the selected technique and reports the state it assigned.
         *
         * @param passIndex Zero-based pass index inside the selected technique.
         * @param deviceState State groups currently selected on the owning GraphicsDevice.
         * @param changes Receives every state group the pass assigned.
         * @throws std::out_of_range if the pass does not exist.
         * @throws std::runtime_error if the native apply reports implausible state changes.
         * @throws System::NotSupportedException if the pass's complete shader pair failed to link.
         */
        void ApplyPass(std::uint32_t passIndex,
                       const CompiledEffectDeviceState& deviceState,
                       CompiledEffectPassStateChanges& changes) override;

        /**
         * @brief CNAEXT. Returns the texture and sampler state currently bound to one sampler slot.
         *
         * A compiled effect's texture/sampler bindings are arbitrary and do not route through
         * `GraphicsDevice`'s fixed stock-effect slots, and `ApplyPass()` keeps this state
         * persistent across passes and techniques, matching XNA: a pass that reassigns nothing
         * leaves an earlier pass's binding standing (plans/plan_fx.md FX-062).
         *
         * @param slot Sampler register index.
         * @param vertexStage True for a vertex-stage sampler, false for a pixel-stage one.
         * @param texture Receives the bound texture, or null if the slot was never assigned.
         * @param sampler Receives the bound sampler state; only meaningful when @p samplerAssigned
         *        comes back true.
         * @param samplerAssigned Receives whether any applied pass has assigned sampler state to
         *        this slot at all. The draw route pushes an assigned state to the GPU and leaves
         *        the slot exactly as the game selected it otherwise (plans/plan_fx.md FX-083).
         */
        CNAEXT void GetBoundSamplerEXT(std::uint32_t slot, bool vertexStage,
                                       Texture*& texture,
                                       Microsoft::Xna::Framework::Graphics::SamplerState& sampler,
                                       bool& samplerAssigned) const;

    private:
        friend class OpenGL4Renderer;

        static constexpr std::size_t kSlots =
            static_cast<std::size_t>(Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers);

        OpenGL4CompiledEffect(OpenGL4Renderer& renderer, const OpenGL4CompiledEffect& cloneSource);
        void CreateNativeEffect();
        void RequireNativeEffect() const;
        /// Deletes the native effect while the renderer's GL and MojoShader contexts still exist,
        /// and forgets the renderer. Called by a renderer destroyed before this effect.
        void DetachFromRenderer();

        /// The owning renderer, or null once that renderer has been destroyed (DetachFromRenderer).
        OpenGL4Renderer* renderer_ = nullptr;
        MOJOSHADER_effect* effectData_ = nullptr;
        std::shared_ptr<const std::vector<std::uint8_t>> effectCode_;
        std::vector<std::vector<std::uint8_t>> parameterValues_;
        MOJOSHADER_effectStateChanges stateChanges_{};
        CompiledEffectDescription description_;
        std::unordered_map<std::string, std::uint32_t> samplerTextureParameters_;
        std::vector<Texture*> textures_;
        std::uint32_t techniqueIndex_ = 0;
        bool passActive_ = false;
        // Persistent per-slot sampler/texture state, updated from every ApplyPass()'s own
        // CompiledEffectSamplerChange list -- see GetBoundSamplerEXT.
        std::array<Texture*, kSlots> boundTextures_{};
        std::array<Texture*, kSlots> boundVertexTextures_{};
        // XNA texture objects are managed references, while CNA's Texture2D/TextureCube are
        // copyable C++ value wrappers: the wrapper pointer a pass published may stop denoting a
        // live wrapper while the cached native resource is still valid (plans/plan_fx.md FX-114).
        // Each non-null sampler assignment keeps its renderer resource alive and is bound directly.
        std::array<std::shared_ptr<ITextureRenderer>, kSlots> boundTexture2DResources_{};
        std::array<std::shared_ptr<ITexture3DRenderer>, kSlots> boundTexture3DResources_{};
        std::array<std::shared_ptr<ITextureCubeRenderer>, kSlots> boundTextureCubeResources_{};
        std::array<std::shared_ptr<ITextureRenderer>, kSlots> boundVertexTexture2DResources_{};
        std::array<std::shared_ptr<ITexture3DRenderer>, kSlots> boundVertexTexture3DResources_{};
        std::array<std::shared_ptr<ITextureCubeRenderer>, kSlots> boundVertexTextureCubeResources_{};
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState, kSlots> boundSamplers_{};
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState, kSlots> boundVertexSamplers_{};
        // FX-083: which slots an applied pass has actually assigned. A default-constructed
        // SamplerState is a legitimate value, so "assigned" cannot be inferred from the value.
        std::array<bool, kSlots> samplerAssigned_{};
        std::array<bool, kSlots> vertexSamplerAssigned_{};
    };
}

#endif  // CNA_OPENGL4_COMPILED_EFFECTS
