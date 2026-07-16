// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"

#include <string>
#include <memory>

namespace CNA::Internal::Backends { class IEffectBackend; }

namespace Microsoft::Xna::Framework::Graphics
{
    class Texture2D;
    class TextureCube;

    /**
     * @brief GLSL-source-based effect loaded from vertex and fragment shader strings.
     *
     * @note NOXNA — not part of the XNA 4.0 API. CNA extension.
     */
    NOXNA class ShaderEffect : public Effect, public IEffectMatrices
    {
    public:
        /**
         * @brief Constructs a ShaderEffect from GLSL source strings.
         *
         * @param device   GraphicsDevice that owns this effect.
         * @param vertSrc  Contents of the GLSL vertex shader source (not a file path).
         * @param fragSrc  Contents of the GLSL fragment shader source (not a file path).
         */
        NOXNA ShaderEffect(GraphicsDevice& device,
                           const std::string& vertSrc,
                           const std::string& fragSrc);

        // Destructor defined in .cpp to avoid incomplete-type error on IEffectBackend.
        ~ShaderEffect();

        /** @brief Returns true if the backend compiled the shader program successfully. */
        NOXNA [[nodiscard]] bool IsEffectValid() const;

        /** @brief Sets a column-major 4×4 matrix uniform by name. */
        NOXNA void SetUniformMat4(const char* name, const float* matrix);
        /** @brief Sets a vec4 uniform by name (x, y, z, w). */
        NOXNA void SetUniformVec4(const char* name, float x, float y, float z, float w);
        /** @brief Sets a vec3 uniform by name (x, y, z). */
        NOXNA void SetUniformVec3(const char* name, float x, float y, float z);
        /** @brief Sets a vec2 uniform by name (x, y). */
        NOXNA void SetUniformVec2(const char* name, float x, float y);
        /** @brief Sets a scalar float uniform by name. */
        NOXNA void SetUniformFloat(const char* name, float value);
        /** @brief Sets a scalar int uniform by name. */
        NOXNA void SetUniformInt(const char* name, int value);
        /** @brief Sets a float array uniform by name. `count` is the number of scalar elements. */
        NOXNA void SetUniformFloatArray(const char* name, const float* values, int count);
        /**
         * @brief Sets a vec2 array uniform by name.
         *
         * @param count Number of vec2 elements (`values` holds `count * 2` floats).
         */
        NOXNA void SetUniformVec2Array(const char* name, const float* values, int count);
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
        NOXNA void SetTexture(int unit, Texture2D& texture);

        /**
         * @brief Task 1081: binds a cube texture to an additional sampler unit, for a custom
         * shader that declares a `samplerCube` uniform (e.g. a reflection/environment map).
         *
         * @param unit    0-based sampler unit.
         * @param texture Cube texture to bind.
         */
        NOXNA void SetTexture(int unit, TextureCube& texture);

        /**
         * @brief Task 1079: enables a `ShaderEffect` to drive a real 3D `GraphicsDevice::
         * DrawIndexedPrimitives`/`DrawUserPrimitives` call (not just `SpriteBatch`), by
         * implementing `IEffectMatrices` the same way every stock effect
         * (`BasicEffect`/`AlphaTestEffect`/…) does — `World`/`View`/`Projection` set here are
         * extracted by `GraphicsDevice::ExtractMatrices()` and forwarded to the backend's draw
         * call, which binds them as `World`/`View`/`Projection` uniforms on the compiled GLSL
         * program (matching the uniform names every original XNA sample's own `.fx` source
         * already uses) when this effect is the one currently applied.
         */
        NOXNA [[nodiscard]] Matrix getWorldProperty() const override { return world_; }
        /** @brief See getWorldProperty(). */
        NOXNA void setWorldProperty(const Matrix& value) override { world_ = value; }
        /** @brief See getWorldProperty(). */
        NOXNA [[nodiscard]] Matrix getViewProperty() const override { return view_; }
        /** @brief See getWorldProperty(). */
        NOXNA void setViewProperty(const Matrix& value) override { view_ = value; }
        /** @brief See getWorldProperty(). */
        NOXNA [[nodiscard]] Matrix getProjectionProperty() const override { return projection_; }
        /** @brief See getWorldProperty(). */
        NOXNA void setProjectionProperty(const Matrix& value) override { projection_ = value; }

        /**
         * @brief Gets the GLSL vertex shader source string.
         *
         * @return The vertex shader source.
         */
        NOXNA [[nodiscard]] const std::string& getVertexSourceProperty() const;

        /**
         * @brief Gets the GLSL fragment shader source string.
         *
         * @return The fragment shader source.
         */
        NOXNA [[nodiscard]] const std::string& getFragmentSourceProperty() const;

        /**
         * @brief Returns the GLSL vertex shader source (Effect base override).
         *
         * Allows backends to access source without depending on the ShaderEffect type.
         */
        NOXNA [[nodiscard]] const std::string& GetVertexSource() const override;

        /**
         * @brief Returns the GLSL fragment shader source (Effect base override).
         *
         * Allows backends to access source without depending on the ShaderEffect type.
         */
        NOXNA [[nodiscard]] const std::string& GetFragmentSource() const override;

        /** @brief Returns the fully qualified CNA type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Creates a clone of this effect.
         *
         * Recompiles a new backend program from the same GLSL source strings rather than
         * sharing the original's compiled program object — deliberate deviation from the other
         * concrete Effect subclasses' Clone() (which share GPU state implicitly, since CNA's
         * stock-effect pipelines are cached globally by state, not per-instance):
         * ShaderEffect uniquely owns a per-instance compiled program
         * (`std::unique_ptr<IEffectBackend>`), so genuine sharing would need a reference-counted
         * backend-ownership model, out of scope for this NOXNA extension.
         *
         * @return Pointer to the cloned Effect.
         */
        [[nodiscard]] Effect* Clone() override;

        /**
         * @brief Returns the backend-specific compiled program for this effect (CNA extension).
         *
         * Lets a backend (e.g. SpriteBatch) bind the same compiled program this ShaderEffect
         * uses, instead of maintaining a redundant separate copy.
         */
        NOXNA [[nodiscard]] CNA::Internal::Backends::IEffectBackend* GetEffectBackendPtr() const override;

        /**
         * @brief Task 1079: marks `GpuDrawParams::customEffectBackend` so a backend's 3D draw
         * path binds this effect's own compiled program instead of one of its built-in
         * stride-dispatched shaders. Every other field is left at its default — unlike the stock
         * effects, a `ShaderEffect`'s own uniforms are set directly by the caller via
         * `SetUniformXxx()`/`SetTexture()`, not translated from XNA-shaped effect properties.
         */
        NOXNA void FillGpuDrawParams(CNA::Internal::Backends::GpuDrawParams& params) const override;

    protected:
        /**
         * @brief Applies the GLSL shaders to the graphics device before drawing.
         */
        void OnApply() override;

    private:
        std::string vertSrc_;
        std::string fragSrc_;
        std::unique_ptr<CNA::Internal::Backends::IEffectBackend> effectBackend_;
        Matrix world_      = Matrix::getIdentityProperty();
        Matrix view_       = Matrix::getIdentityProperty();
        Matrix projection_ = Matrix::getIdentityProperty();
    };
}
