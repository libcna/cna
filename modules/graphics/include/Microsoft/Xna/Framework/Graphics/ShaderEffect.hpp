// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/ShaderDiagnosticEXT.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"

#include <memory>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers { class IEffectRenderer; }
#ifdef CNA_CNAEXT
namespace CNA::Graphics {
    class ShaderCodeEXT;
    class ShaderPackageEXT;
    class Texture2DArray;
    class StorageTexture2D;
}
#endif

namespace Microsoft::Xna::Framework::Graphics
{
    class Texture2D;
    class Texture3D;
    class TextureCube;

    /**
     * @brief GLSL-source-based effect loaded from vertex and fragment shader strings.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. CNA extension.
     */
    CNAEXT class ShaderEffect : public Effect, public IEffectMatrices
    {
    public:
        /**
         * @brief Constructs a ShaderEffect from GLSL source strings.
         *
         * @param device   GraphicsDevice that owns this effect.
         * @param vertSrc  Contents of the GLSL vertex shader source (not a file path).
         * @param fragSrc  Contents of the GLSL fragment shader source (not a file path).
         */
        CNAEXT ShaderEffect(GraphicsDevice& device,
                           const std::string& vertSrc,
                           const std::string& fragSrc);

#ifdef CNA_CNAEXT
        /**
         * @brief Compiles an explicitly identified vertex/fragment pair through this effect.
         * @param device The live device used to validate both exact language/stage pairs.
         * @param vertexCode Vertex payload.
         * @param fragmentCode Fragment payload in the same language as @p vertexCode.
         * @throws std::invalid_argument If stages, languages or entry points are incompatible.
         * @throws CNA::ShaderCompilationExceptionEXT If the live renderer refuses the exact
         *         language.
         */
        CNAEXT ShaderEffect(
            GraphicsDevice& device, const CNA::Graphics::ShaderCodeEXT& vertexCode,
            const CNA::Graphics::ShaderCodeEXT& fragmentCode);

        /**
         * @brief Selects and compiles one vertex/fragment variant from a shader package.
         * @param device The live device used once for deterministic package selection.
         * @param package Package whose required stages must be exactly vertex and fragment.
         * @throws std::invalid_argument If the package stage contract or selected entry points are
         *         incompatible with the existing effect path.
         * @throws CNA::ShaderCompilationExceptionEXT If no package variant is usable.
         */
        CNAEXT ShaderEffect(
            GraphicsDevice& device, const CNA::Graphics::ShaderPackageEXT& package);
#endif

        // Destructor defined in .cpp to avoid incomplete-type error on IEffectRenderer.
        ~ShaderEffect();

        /** @brief Returns true if the renderer compiled the shader program successfully. */
        CNAEXT [[nodiscard]] bool IsEffectValid() const;

        /** @brief Returns true while the native compiled-program renderer is still alive. */
        CNAEXT [[nodiscard]] bool HasRenderer() const { return effectRenderer_ != nullptr; }

        /**
         * @brief Returns the compiler/linker log from a failed compile, empty when it succeeded.
         *
         * plans/plan_modern.md `MOD-219`. `IsEffectValid()` says *that* a shader did not compile; this
         * says why. The two are separate on purpose: a failed compile is not an exception here,
         * because on several renderers `GraphicsCapability::CustomEffects` is true while GLSL source
         * is never compiled at all (Vulkan takes SPIR-V; SOFTWARE and HEADLESS accept and ignore),
         * and throwing would turn a documented capability boundary into a crash. Callers that need
         * to *report* the failure — the engine-layer passes do, naming themselves — read it here.
         *
         * @return The renderer's log, or an empty string when the program linked or the renderer
         *         keeps no log.
         */
        CNAEXT [[nodiscard]] std::string GetCompileErrorEXT() const;

        /**
         * @brief Returns owned structured compiler diagnostics for the last failed compile.
         * @return Ordered diagnostics with stage, source label and available source location;
         *         empty while this effect is valid.
         */
        CNAEXT [[nodiscard]] std::vector<CNA::ShaderDiagnosticEXT>
            GetShaderDiagnosticsEXT() const;

        /**
         * @brief Returns the explicit selected language, or `Unknown` for the string constructor.
         * @return Language retained by a code/package constructor for this effect's lifetime.
         */
        CNAEXT [[nodiscard]] CNA::ShaderLanguageEXT GetSelectedShaderLanguageEXT() const noexcept;

        /** @brief Sets a column-major 4×4 matrix uniform by name. */
        /**
         * @brief Declares the std140 uniform block this effect's parameters live in. CNAEXT.
         *
         * Required on a source-compiling renderer whose shading dialect has no loose (non-block)
         * uniforms -- today IGL's Vulkan backend. Harmlessly ignored everywhere else, so the same
         * call can sit unconditionally beside the effect's construction.
         *
         * Ask @ref GraphicsDevice::GetShaderDialectEXT which payload dialect the active renderer
         * wants; an application that needs both generally supplies two shader payload pairs and
         * one of these declarations describing the source-compiled Vulkan one.
         *
         * @param blockSizeBytes Size of the whole block in bytes, std140-padded.
         * @param names          Member names, `count` of them.
         * @param offsets        Each member's byte offset from the start of the block.
         * @param count          Number of members; zero clears any previous declaration.
         */
        CNAEXT void DeclareUniformBlockEXT(int blockSizeBytes, const char* const* names,
                                           const int* offsets, int count);

        CNAEXT void SetUniformMat4(const char* name, const float* matrix);
        /** @brief Sets a vec4 uniform by name (x, y, z, w). */
        CNAEXT void SetUniformVec4(const char* name, float x, float y, float z, float w);
        /** @brief Sets a vec3 uniform by name (x, y, z). */
        CNAEXT void SetUniformVec3(const char* name, float x, float y, float z);
        /** @brief Sets a vec2 uniform by name (x, y). */
        CNAEXT void SetUniformVec2(const char* name, float x, float y);
        /** @brief Sets a scalar float uniform by name. */
        CNAEXT void SetUniformFloat(const char* name, float value);
        /** @brief Sets a scalar int uniform by name. */
        CNAEXT void SetUniformInt(const char* name, int value);
        /** @brief Sets a float array uniform by name. `count` is the number of scalar elements. */
        CNAEXT void SetUniformFloatArray(const char* name, const float* values, int count);
        /**
         * @brief Sets a vec2 array uniform by name.
         *
         * @param count Number of vec2 elements (`values` holds `count * 2` floats).
         */
        CNAEXT void SetUniformVec2Array(const char* name, const float* values, int count);

        /**
         * @brief Sets a `vec3` array uniform.
         *
         * Distinct from SetUniformFloatArray for a reason that costs an afternoon to rediscover:
         * GL rejects filling a `vec3[]` from a float array as a type mismatch and leaves the
         * uniform at its default, without an error the caller sees.
         *
         * @param name   The uniform's name in the shader.
         * @param values Pointer to @p count * 3 floats.
         * @param count  Number of vec3 elements.
         */
        CNAEXT void SetUniformVec3Array(const char* name, const float* values, int count);

        /**
         * @brief Sets a `mat4` array uniform, e.g. a skinning palette.
         *
         * Distinct from SetUniformMat4, which uploads exactly one matrix whatever the uniform's
         * declared size -- filling a palette with it leaves every element past the first at its
         * default.
         *
         * @param name     The uniform's name in the shader; the `[0]` spelling GLSL uses for the
         *                 first element is tried too, so either form works.
         * @param matrices Pointer to @p count * 16 floats, column-major.
         * @param count    Number of matrices.
         */
        CNAEXT void SetUniformMat4Array(const char* name, const float* matrices, int count);
        /**
         * @brief Binds a texture to an additional sampler unit for this effect's shader.
         *
         * Unit 0 is normally driven by the caller (e.g. SpriteBatch's own texture parameter);
         * use this for extra units a custom shader samples directly (e.g. a second
         * blend-source texture, matching real XNA's `GraphicsDevice.Textures[unit] = tex`).
         *
         * @param unit    0-based sampler unit.
         * @param texture Texture to bind.
         */
        CNAEXT void SetTexture(int unit, Texture2D& texture);

        /**
         * @brief Task 1081: binds a cube texture to an additional sampler unit, for a custom
         * shader that declares a `samplerCube` uniform (e.g. a reflection/environment map).
         *
         * @param unit    0-based sampler unit.
         * @param texture Cube texture to bind.
         */
        CNAEXT void SetTexture(int unit, TextureCube& texture);

        /**
         * @brief plans/plan_graphics.md Task 863: binds a volume (3D) texture to an additional sampler
         * unit, for a custom shader that declares a `sampler3D` uniform.
         *
         * @param unit    0-based sampler unit.
         * @param texture Volume texture to bind.
         */
        CNAEXT void SetTexture(int unit, Texture3D& texture);

#ifdef CNA_CNAEXT
        /**
         * @brief Binds a sampled two-dimensional texture array to a custom shader.
         *
         * Texture-array bindings occupy their own renderer-defined descriptor range; they never
         * alias a `Texture2D`, cube or volume binding with an incompatible native view type.
         *
         * @param unit Zero-based texture-array sampler unit.
         * @param texture Live texture-array resource to bind.
         * @throws System::ObjectDisposedException If @p texture has been disposed.
         * @throws System::NotSupportedException If the active renderer does not implement array
         *         sampling or refuses @p unit.
         */
        CNAEXT void SetTextureArrayEXT(int unit, CNA::Graphics::Texture2DArray& texture);

        /**
         * @brief Clears a previously bound texture array from a custom shader.
         *
         * @param unit Zero-based texture-array sampler unit.
         * @throws System::NotSupportedException If the active renderer does not implement array
         *         sampling or refuses @p unit.
         */
        CNAEXT void ClearTextureArrayEXT(int unit);

        /**
         * @brief Binds a sampled storage texture to an ordinary two-dimensional sampler unit.
         * @param unit Zero-based 2D sampler unit.
         * @param texture Live storage texture with `Sampled` usage declared.
         * @throws System::ObjectDisposedException If @p texture is disposed.
         * @throws std::invalid_argument If the texture belongs to another graphics device or was
         *         not created with sampled usage.
         * @throws System::NotSupportedException If the renderer refuses the sampled binding.
         */
        CNAEXT void SetStorageTextureEXT(
            int unit, CNA::Graphics::StorageTexture2D& texture);

        /**
         * @brief Clears a sampled storage texture from a two-dimensional sampler unit.
         * @param unit Zero-based 2D sampler unit.
         * @throws System::NotSupportedException If the renderer refuses the clear operation.
         */
        CNAEXT void ClearStorageTextureEXT(int unit);
#endif

        /**
         * @brief Task 1079: enables a `ShaderEffect` to drive a real 3D `GraphicsDevice::
         * DrawIndexedPrimitives`/`DrawUserPrimitives` call (not just `SpriteBatch`), by
         * implementing `IEffectMatrices` the same way every stock effect
         * (`BasicEffect`/`AlphaTestEffect`/…) does — `World`/`View`/`Projection` set here are
         * extracted by `GraphicsDevice::ExtractMatrices()` and forwarded to the renderer's draw
         * call, which binds them as `World`/`View`/`Projection` uniforms on the compiled GLSL
         * program (matching the uniform names every original XNA sample's own `.fx` source
         * already uses) when this effect is the one currently applied.
         */
        CNAEXT [[nodiscard]] Matrix getWorldProperty() const override { return world_; }
        /** @brief See getWorldProperty(). */
        CNAEXT void setWorldProperty(const Matrix& value) override { world_ = value; }
        /** @brief See getWorldProperty(). */
        CNAEXT [[nodiscard]] Matrix getViewProperty() const override { return view_; }
        /** @brief See getWorldProperty(). */
        CNAEXT void setViewProperty(const Matrix& value) override { view_ = value; }
        /** @brief See getWorldProperty(). */
        CNAEXT [[nodiscard]] Matrix getProjectionProperty() const override { return projection_; }
        /** @brief See getWorldProperty(). */
        CNAEXT void setProjectionProperty(const Matrix& value) override { projection_ = value; }

        /**
         * @brief Gets the GLSL vertex shader source string.
         *
         * @return The vertex shader source.
         */
        CNAEXT [[nodiscard]] const std::string& getVertexSourceProperty() const;

        /**
         * @brief Gets the GLSL fragment shader source string.
         *
         * @return The fragment shader source.
         */
        CNAEXT [[nodiscard]] const std::string& getFragmentSourceProperty() const;

        /**
         * @brief Returns the GLSL vertex shader source (Effect base override).
         *
         * Allows renderers to access source without depending on the ShaderEffect type.
         */
        CNAEXT [[nodiscard]] const std::string& GetVertexSource() const override;

        /**
         * @brief Returns the GLSL fragment shader source (Effect base override).
         *
         * Allows renderers to access source without depending on the ShaderEffect type.
         */
        CNAEXT [[nodiscard]] const std::string& GetFragmentSource() const override;

        /** @brief Returns the fully qualified CNA type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Creates a clone of this effect.
         *
         * Recompiles a new renderer program from the same GLSL source strings rather than
         * sharing the original's compiled program object — deliberate deviation from the other
         * concrete Effect subclasses' Clone() (which share GPU state implicitly, since CNA's
         * stock-effect pipelines are cached globally by state, not per-instance):
         * ShaderEffect uniquely owns a per-instance compiled program
         * (`std::unique_ptr<IEffectRenderer>`), so genuine sharing would need a reference-counted
         * renderer-ownership model, out of scope for this CNAEXT extension.
         *
         * @return Pointer to the cloned Effect.
         */
        [[nodiscard]] Effect* Clone() override;

        /**
         * @brief Returns the renderer-specific compiled program for this effect (CNA extension).
         *
         * Lets a renderer (e.g. SpriteBatch) bind the same compiled program this ShaderEffect
         * uses, instead of maintaining a redundant separate copy.
         */
        CNAEXT [[nodiscard]] CNA::Internal::Renderers::IEffectRenderer* GetEffectRendererPtr() const override;

        /**
         * @brief Task 1079: marks `GpuDrawParams::customEffectRenderer` so a renderer's 3D draw
         * path binds this effect's own compiled program instead of one of its built-in
         * stride-dispatched shaders. Every other field is left at its default — unlike the stock
         * effects, a `ShaderEffect`'s own uniforms are set directly by the caller via
         * `SetUniformXxx()`/`SetTexture()`, not translated from XNA-shaped effect properties.
         */
        CNAEXT void FillGpuDrawParams(CNA::Internal::Renderers::GpuDrawParams& params) const override;

    protected:
        /**
         * @brief Applies the GLSL shaders to the graphics device before drawing.
         */
        void OnApply() override;

        /**
         * @brief Releases the compiled renderer program before the base class marks this resource
         * disposed (plans/plan_sokol.md SOKOL-42).
         *
         * `Effect::Dispose(bool)` only reaches `GraphicsResource::Dispose(bool)`, which never
         * touches `effectRenderer_`. `GraphicsDevice::Dispose()` disposes tracked resources before
         * tearing down the renderer device/GL context, so without this override effectRenderer_ is
         * only destroyed whenever this ShaderEffect's own C++ destructor happens to run -- possibly
         * long after that teardown -- e.g. a raw `glDeleteProgram` call after `sg_shutdown()` and
         * SDL GL-context destruction on the Sokol renderer.
         *
         * @param disposing True when called from Dispose(); false when called from the finalizer.
         */
        void Dispose(bool disposing) override;

    private:
#ifdef CNA_CNAEXT
        struct PreparedPortablePayload
        {
            std::string vertexSource;
            std::string fragmentSource;
            std::string vertexLabel;
            std::string fragmentLabel;
            CNA::ShaderLanguageEXT language;
        };

        ShaderEffect(GraphicsDevice& device, PreparedPortablePayload payload);
        static PreparedPortablePayload PreparePortablePayload(
            GraphicsDevice& device, const CNA::Graphics::ShaderCodeEXT& vertexCode,
            const CNA::Graphics::ShaderCodeEXT& fragmentCode);
        static PreparedPortablePayload PreparePortablePayload(
            GraphicsDevice& device, const CNA::Graphics::ShaderPackageEXT& package);
#endif

        std::string vertSrc_;
        std::string fragSrc_;
        std::string selectedVertexLabelEXT_;
        std::string selectedFragmentLabelEXT_;
        CNA::ShaderLanguageEXT selectedShaderLanguageEXT_ = CNA::ShaderLanguageEXT::Unknown;
        std::unique_ptr<CNA::Internal::Renderers::IEffectRenderer> effectRenderer_;
        Matrix world_      = Matrix::getIdentityProperty();
        Matrix view_       = Matrix::getIdentityProperty();
        Matrix projection_ = Matrix::getIdentityProperty();
    };
}
