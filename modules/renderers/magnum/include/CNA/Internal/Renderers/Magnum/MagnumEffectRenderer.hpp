// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "MagnumProgram.hpp"

#include <array>
#include <string>

namespace CNA::Internal::Renderers::Magnum
{
    /** @brief Magnum implementation of `IEffectRenderer`: a `ShaderEffect`'s own linked program. */
    class MagnumEffectRenderer : public IEffectRenderer
    {
    public:
        /** @brief Creates an unlinked effect; call `CompileProgram` before binding it. */
        MagnumEffectRenderer() = default;
        ~MagnumEffectRenderer() override = default;

        /**
         * @brief Compiles and links the caller's GLSL into this effect's own program.
         *
         * @param vertSrc Complete GLSL vertex shader source.
         * @param fragSrc Complete GLSL fragment shader source.
         * @return True if the program compiled and linked.
         */
        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;
        /** @brief Makes this effect's program current, so uniform writes and draws reach it. */
        void Bind() override;
        /** @brief Releases this effect's program from the pipeline. */
        void Unbind() override;
        /** @brief True once `CompileProgram` has succeeded. */
        [[nodiscard]] bool IsValid() const override;
        /** @brief Compile/link diagnostics from the last `CompileProgram` call; empty on success. */
        [[nodiscard]] std::string GetCompileError() const override;

        /**
         * @brief Assigns a float uniform by name.
         *
         * @param name  Uniform name as declared in the effect's GLSL.
         * @param value Value to assign.
         */
        void SetUniformFloat(const char* name, float value) override;
        /**
         * @brief Assigns an int uniform by name.
         *
         * @param name  Uniform name as declared in the effect's GLSL.
         * @param value Value to assign.
         */
        void SetUniformInt(const char* name, int value) override;
        /**
         * @brief Assigns a vec2 uniform by name.
         *
         * @param name Uniform name as declared in the effect's GLSL.
         * @param x    First component.
         * @param y    Second component.
         */
        void SetUniformVec2(const char* name, float x, float y) override;
        /**
         * @brief Assigns a vec3 uniform by name.
         *
         * @param name Uniform name as declared in the effect's GLSL.
         * @param x    First component.
         * @param y    Second component.
         * @param z    Third component.
         */
        void SetUniformVec3(const char* name, float x, float y, float z) override;
        /**
         * @brief Assigns a vec4 uniform by name.
         *
         * @param name Uniform name as declared in the effect's GLSL.
         * @param x    First component.
         * @param y    Second component.
         * @param z    Third component.
         * @param w    Fourth component.
         */
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;
        /**
         * @brief Assigns a mat4 uniform by name from 16 column-major floats.
         *
         * @param name   Uniform name as declared in the effect's GLSL.
         * @param matrix Pointer to 16 floats in column-major order.
         */
        void SetUniformMat4(const char* name, const float* matrix) override;
        /**
         * @brief Assigns a float array uniform by name.
         *
         * @param name   Uniform name as declared in the effect's GLSL.
         * @param values Pointer to @p count floats.
         * @param count  Number of array elements.
         */
        void SetUniformFloatArray(const char* name, const float* values, int count) override;
        /**
         * @brief Assigns a vec2 array uniform by name.
         *
         * @param name   Uniform name as declared in the effect's GLSL.
         * @param values Pointer to `count * 2` floats.
         * @param count  Number of vec2 elements.
         */
        void SetUniformVec2Array(const char* name, const float* values, int count) override;
        /**
         * @brief Binds a 2D texture to a texture unit and points the matching sampler at it.
         *
         * @param unit    Texture unit index.
         * @param texture Texture to bind, or nullptr to leave the unit untouched.
         */
        void BindTexture(int unit, ITextureRenderer* texture) override;
        /**
         * @brief Binds a cube map to a texture unit and points the matching sampler at it.
         *
         * @param unit    Texture unit index.
         * @param texture Cube map to bind, or nullptr to leave the unit untouched.
         */
        void BindTextureCube(int unit, ITextureCubeRenderer* texture) override;
        /**
         * @brief Binds a volume texture to a texture unit and points the matching sampler at it.
         *
         * @param unit    Texture unit index.
         * @param texture Volume texture to bind, or nullptr to leave the unit untouched.
         */
        void BindTexture3D(int unit, ITexture3DRenderer* texture) override;

        /**
         * @brief The program this effect owns, so a draw path binds the same one its uniform
         *        writes reach rather than a second compiled copy of the same source.
         */
        [[nodiscard]] MagnumProgram& GetProgram() { return program_; }

    private:
        void UploadRenderTargetFlips();

        MagnumProgram program_;
        std::string compileError_;
        /// Per-unit "the bound source is a render target" flags, mirrored into the effect's own
        /// `uRtFlipV` when its GLSL opts in by declaring that uniform.
        std::array<float, 4> renderTargetFlips_{0.0f, 0.0f, 0.0f, 0.0f};
    };
}
