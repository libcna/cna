// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/Magnum/MagnumProgram.hpp"

#include <Corrade/Containers/ArrayView.h>
#include <Corrade/Containers/StringView.h>
#include <Corrade/Utility/DebugStl.h>
#include <Magnum/GL/Shader.h>
#include <Magnum/GL/Version.h>
#include <Magnum/Math/Matrix3.h>
#include <Magnum/Math/Matrix4.h>

#include <sstream>

namespace CNA::Internal::Backends::Magnum
{
    namespace
    {
        /**
         * @brief Removes a leading `#version` directive from GLSL source.
         *
         * `GL::Shader` emits its own `#version` line from the version handed to its constructor and
         * then prefixes every added source with a `#line` directive, so a source that declares one
         * itself makes the directive land on line 2 and the compile fails outright with
         * "#version must appear on the first line". Both source routes reach this: CNA's stock
         * shaders declare their version for readability, and a `ShaderEffect`'s GLSL is ordinary
         * caller-authored code that normally starts with one. Only a directive at the very start
         * (after leading whitespace) is removed -- anywhere else it is not a version declaration
         * this function has any business rewriting.
         */
        std::string StripVersionDirective(const std::string& source)
        {
            const std::size_t start = source.find_first_not_of(" \t\r\n");
            if (start == std::string::npos || source.compare(start, 8, "#version") != 0)
                return source;
            const std::size_t endOfLine = source.find('\n', start);
            if (endOfLine == std::string::npos)
                return {};
            return source.substr(endOfLine + 1);
        }
    }

    bool MagnumProgram::CompileAndLink(const std::string& vertexSource,
                                       const std::string& fragmentSource)
    {
        linked_ = false;
        log_.clear();
        locations_.clear();

        // Magnum reports compile/link diagnostics through Corrade's global Error/Warning streams
        // rather than returning them, so both are redirected into one buffer for the duration of
        // the build. This is the only way to satisfy IEffectBackend::GetCompileError(), which must
        // hand the driver's own message back to the game.
        std::ostringstream diagnostics;
        Corrade::Utility::Error redirectError{&diagnostics};
        Corrade::Utility::Warning redirectWarning{&diagnostics};

        Mg::GL::Shader vertex{Mg::GL::Version::GL330, Mg::GL::Shader::Type::Vertex};
        vertex.addSource(StripVersionDirective(vertexSource));
        Mg::GL::Shader fragment{Mg::GL::Version::GL330, Mg::GL::Shader::Type::Fragment};
        fragment.addSource(StripVersionDirective(fragmentSource));

        vertex.submitCompile();
        fragment.submitCompile();
        const bool vertexCompiled = vertex.checkCompile();
        const bool fragmentCompiled = fragment.checkCompile();
        if (!vertexCompiled || !fragmentCompiled)
        {
            log_ = diagnostics.str();
            if (log_.empty())
                log_ = "Magnum shader compilation failed.";
            return false;
        }

        attachShaders({vertex, fragment});
        submitLink();
        if (!checkLink({vertex, fragment}))
        {
            log_ = diagnostics.str();
            if (log_.empty())
                log_ = "Magnum shader program linking failed.";
            return false;
        }

        linked_ = true;
        return true;
    }

    int MagnumProgram::LocationOf(const std::string& name)
    {
        if (!linked_)
            return -1;
        const auto cached = locations_.find(name);
        if (cached != locations_.end())
            return cached->second;

        // uniformLocation() emits a Warning for a name the program does not declare. That is a
        // normal, expected outcome here -- every stock program is asked for the whole uniform set
        // and only declares the subset its variant uses -- so the warning is swallowed rather than
        // printed once per lookup.
        std::ostringstream discardedWarning;
        Corrade::Utility::Warning redirectWarning{&discardedWarning};
        const int location = uniformLocation(Corrade::Containers::StringView{name});
        locations_.emplace(name, location);
        return location;
    }

    void MagnumProgram::SetFloat(int location, float value)
    {
        if (location < 0) return;
        setUniform(location, value);
    }

    void MagnumProgram::SetInt(int location, int value)
    {
        if (location < 0) return;
        setUniform(location, value);
    }

    void MagnumProgram::SetVector2(int location, const Mg::Vector2& value)
    {
        if (location < 0) return;
        setUniform(location, value);
    }

    void MagnumProgram::SetVector3(int location, const Mg::Vector3& value)
    {
        if (location < 0) return;
        setUniform(location, value);
    }

    void MagnumProgram::SetVector4(int location, const Mg::Vector4& value)
    {
        if (location < 0) return;
        setUniform(location, value);
    }

    void MagnumProgram::SetMatrix3(int location, const float* columnMajorData)
    {
        if (location < 0 || columnMajorData == nullptr) return;
        setUniform(location, Mg::Matrix3x3::from(columnMajorData));
    }

    void MagnumProgram::SetMatrix4(int location, const float* columnMajorData)
    {
        if (location < 0 || columnMajorData == nullptr) return;
        setUniform(location, Mg::Matrix4::from(columnMajorData));
    }

    void MagnumProgram::SetFloatArray(int location, const float* values, int count)
    {
        if (location < 0 || values == nullptr || count <= 0) return;
        setUniform(location, Corrade::Containers::ArrayView<const Mg::Float>{
            values, static_cast<std::size_t>(count)});
    }

    void MagnumProgram::SetVector2Array(int location, const float* values, int count)
    {
        if (location < 0 || values == nullptr || count <= 0) return;
        setUniform(location, Corrade::Containers::ArrayView<const Mg::Vector2>{
            reinterpret_cast<const Mg::Vector2*>(values), static_cast<std::size_t>(count)});
    }

    void MagnumProgram::SetMatrix4Array(int location, const float* columnMajorData, int count)
    {
        if (location < 0 || columnMajorData == nullptr || count <= 0) return;
        setUniform(location, Corrade::Containers::ArrayView<const Mg::Matrix4>{
            reinterpret_cast<const Mg::Matrix4*>(columnMajorData),
            static_cast<std::size_t>(count)});
    }
}
