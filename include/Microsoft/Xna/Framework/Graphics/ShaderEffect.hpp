// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <string>
#include <memory>

namespace CNA::Internal::Backends { class IEffectBackend; }

namespace Microsoft::Xna::Framework::Graphics
{
    class Texture2D;

    /**
     * @brief GLSL-source-based effect loaded from vertex and fragment shader strings.
     *
     * @note NOXNA — not part of the XNA 4.0 API. CNA extension.
     */
    NOXNA class ShaderEffect : public Effect
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

    protected:
        /**
         * @brief Applies the GLSL shaders to the graphics device before drawing.
         */
        void OnApply() override;

    private:
        std::string vertSrc_;
        std::string fragSrc_;
        std::unique_ptr<CNA::Internal::Backends::IEffectBackend> effectBackend_;
    };
}
