// SPDX-License-Identifier: MS-PL
#pragma once

#include <Magnum/GL/AbstractShaderProgram.h>
#include <Magnum/GL/Version.h>
#include <Magnum/Math/Matrix4.h>
#include <Magnum/Math/Vector4.h>

#include <string>
#include <unordered_map>

namespace CNA::Internal::Renderers::Magnum
{
    namespace Mg = ::Magnum;

    /**
     * @brief Removes a leading `#version` directive from GLSL source.
     *
     * `GL::Shader` emits its own `#version` line from the version handed to its constructor and
     * then prefixes every added source with a `#line` directive, so a source that declares one
     * itself makes the directive land on line 2 and the compile fails outright. Only a directive
     * at the very start of the source (after leading whitespace) is removed.
     *
     * @param source Complete GLSL source.
     * @return The source with its leading `#version` line removed, or unchanged if it has none.
     */
    [[nodiscard]] std::string StripVersionDirective(const std::string& source);

    /**
     * @brief Recovers the GLSL version a source declares, so `GL::Shader` can re-emit it.
     *
     * @param source Complete GLSL source.
     * @return The declared version, or `GL330` when the directive is absent or unrecognized.
     */
    [[nodiscard]] Mg::GL::Version DeclaredVersion(const std::string& source);

    /**
     * @brief A shader program built from GLSL source at runtime.
     *
     * `GL::AbstractShaderProgram` is designed to be subclassed once per compile-time-known shader,
     * with `uniformLocation()`/`setUniform()` kept protected so a program's uniforms are reached
     * through named methods. CNA's shader set is not compile-time known -- stock effects are
     * selected by vertex stride and draw parameters, and `ShaderEffect` compiles arbitrary caller
     * GLSL -- so this subclass exposes those two operations by name and caches the locations it
     * has already looked up.
     */
    class MagnumProgram : public Mg::GL::AbstractShaderProgram
    {
    public:
        /** @brief Creates an unlinked program; call `CompileAndLink` before using it. */
        MagnumProgram() = default;

        /**
         * @brief Compiles both stages and links them into this program.
         *
         * On failure the program stays invalid and `GetLog()` holds the driver's own compile or
         * link diagnostics.
         *
         * @param vertexSource   Complete GLSL vertex shader source, including its `#version` line.
         * @param fragmentSource Complete GLSL fragment shader source, including its `#version` line.
         * @return True if both stages compiled and the program linked.
         */
        bool CompileAndLink(const std::string& vertexSource, const std::string& fragmentSource);

        /** @brief True once `CompileAndLink` has succeeded. */
        [[nodiscard]] bool IsValid() const { return linked_; }

        /** @brief Compile/link diagnostics from the last `CompileAndLink` call; empty on success. */
        [[nodiscard]] const std::string& GetLog() const { return log_; }

        /**
         * @brief Location of a uniform, looked up once and then cached.
         *
         * @param name Uniform name as declared in the GLSL source.
         * @return The uniform location, or -1 when the program has no such active uniform.
         */
        [[nodiscard]] int LocationOf(const std::string& name);

        /**
         * @brief Assigns a float uniform by location.
         *
         * @param location Uniform location; negative locations are ignored.
         * @param value    Value to assign.
         */
        void SetFloat(int location, float value);

        /**
         * @brief Assigns an int uniform by location.
         *
         * @param location Uniform location; negative locations are ignored.
         * @param value    Value to assign.
         */
        void SetInt(int location, int value);

        /**
         * @brief Assigns a vec2 uniform by location.
         *
         * @param location Uniform location; negative locations are ignored.
         * @param value    Value to assign.
         */
        void SetVector2(int location, const Mg::Vector2& value);

        /**
         * @brief Assigns a vec3 uniform by location.
         *
         * @param location Uniform location; negative locations are ignored.
         * @param value    Value to assign.
         */
        void SetVector3(int location, const Mg::Vector3& value);

        /**
         * @brief Assigns a vec4 uniform by location.
         *
         * @param location Uniform location; negative locations are ignored.
         * @param value    Value to assign.
         */
        void SetVector4(int location, const Mg::Vector4& value);

        /**
         * @brief Assigns a mat3 uniform by location from 9 column-major floats.
         *
         * @param location        Uniform location; negative locations are ignored.
         * @param columnMajorData Pointer to 9 floats in column-major order.
         */
        void SetMatrix3(int location, const float* columnMajorData);

        /**
         * @brief Assigns a mat4 uniform by location from 16 column-major floats.
         *
         * @param location        Uniform location; negative locations are ignored.
         * @param columnMajorData Pointer to 16 floats in column-major order.
         */
        void SetMatrix4(int location, const float* columnMajorData);

        /**
         * @brief Assigns a float array uniform by location.
         *
         * @param location Uniform location; negative locations are ignored.
         * @param values   Pointer to @p count floats.
         * @param count    Number of array elements.
         */
        void SetFloatArray(int location, const float* values, int count);

        /**
         * @brief Assigns a vec2 array uniform by location.
         *
         * @param location Uniform location; negative locations are ignored.
         * @param values   Pointer to `count * 2` floats.
         * @param count    Number of vec2 elements.
         */
        void SetVector2Array(int location, const float* values, int count);

        /**
         * @brief Assigns a mat4 array uniform by location.
         *
         * @param location        Uniform location; negative locations are ignored.
         * @param columnMajorData Pointer to `count * 16` floats in column-major order.
         * @param count           Number of matrices.
         */
        void SetMatrix4Array(int location, const float* columnMajorData, int count);

    private:
        bool linked_ = false;
        std::string log_;
        std::unordered_map<std::string, int> locations_;
    };
}
