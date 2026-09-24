// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md Workstream B: storage and constant buffers, compute
// programs, dispatch ordering and the limits that describe them, over desktop OpenGL 4.3+.

#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Modern.hpp"

#include "CNA/GraphicsImageAccess.hpp"
#include "CNA/GraphicsMemoryBarrier.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Renderer.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Resources.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Internal::Renderers::OpenGL4
{
    using namespace GL4;

    namespace
    {
        constexpr std::uint32_t kUsageStorage = UINT32_C(1) << 0;
        constexpr std::uint32_t kUsageIndirectArguments = UINT32_C(1) << 3;
        constexpr std::uint32_t kUsageConstant = UINT32_C(1) << 6;
        constexpr std::uint32_t kUsageMask = UINT32_C(0x7F);
        constexpr std::uint32_t kCpuAccessMask = UINT32_C(0x03);

        /// Binds @p name to a generic buffer binding point for one scope and puts back what was
        /// there. The copy points are read by no draw, but a caller may still have set them.
        class ScopedGenericBufferBinding
        {
        public:
            ScopedGenericBufferBinding(GLenum target, GLenum bindingQuery, GLuint name)
                : target_(target)
            {
                glGetIntegerv(bindingQuery, &previous_);
                gl4_glBindBuffer(target_, name);
            }
            ~ScopedGenericBufferBinding()
            {
                gl4_glBindBuffer(target_, static_cast<GLuint>(previous_));
            }
            ScopedGenericBufferBinding(const ScopedGenericBufferBinding&) = delete;
            ScopedGenericBufferBinding& operator=(const ScopedGenericBufferBinding&) = delete;

        private:
            GLenum target_;
            GLint previous_ = 0;
        };

        [[nodiscard]] GLint QueryInteger(GLenum name)
        {
            GLint value = 0;
            glGetIntegerv(name, &value);
            return value;
        }

        [[nodiscard]] GLenum ToGlImageAccess(const int accessMode)
        {
            switch (static_cast<CNA::GraphicsImageAccess>(accessMode))
            {
                case CNA::GraphicsImageAccess::ReadOnly: return GL_READ_ONLY;
                case CNA::GraphicsImageAccess::WriteOnly: return GL_WRITE_ONLY;
                case CNA::GraphicsImageAccess::ReadWrite: return GL_READ_WRITE;
            }
            return GL_READ_WRITE;
        }

        /// Whether @p internalFormat is one of desktop GL 4.3's image load/store formats
        /// (OpenGL 4.6 core, table 8.26), and so can be bound with glBindImageTexture as itself.
        [[nodiscard]] bool IsImageUnitFormat(const GLenum internalFormat)
        {
            switch (internalFormat)
            {
                case GL_RGBA32F: case GL_RGBA16F: case GL_RG32F: case GL_RG16F:
                case GL_R32F: case GL_R16F:
                case GL_RGBA16: case GL_RGB10_A2: case GL_RGBA8: case GL_RG16: case GL_RG8:
                case GL_R16: case GL_R8:
                case GL_RGBA8_SNORM: case GL_RG8_SNORM:
                    return true;
                default:
                    return false;
            }
        }

        /// GL name of the texture storage behind a Texture2D or a RenderTarget2D, or 0.
        [[nodiscard]] GLuint TextureName(const ITextureRenderer* texture)
        {
            if (const auto* plain = dynamic_cast<const OpenGL4TextureRenderer*>(texture))
                return plain->GLHandle();
            if (const auto* target = dynamic_cast<const OpenGL4RenderTargetRenderer*>(texture))
                return target->GetColorGLHandle();
            return 0;
        }
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4StorageBufferRenderer
    // ------------------------------------------------------------------------------------

    OpenGL4StorageBufferRenderer::OpenGL4StorageBufferRenderer(
        const std::size_t byteSize, const std::uint32_t usage, const std::uint32_t cpuAccess)
        : byteSize_(byteSize), usage_(usage), cpuAccess_(cpuAccess)
    {
        if (byteSize_ == 0)
            throw std::invalid_argument("OpenGL4 buffer: byte size must be positive");
        if (usage_ == 0 || (usage_ & ~kUsageMask) != 0)
            throw std::invalid_argument("OpenGL4 buffer: usage mask is invalid");
        if ((cpuAccess_ & ~kCpuAccessMask) != 0)
            throw std::invalid_argument("OpenGL4 buffer: CPU-access mask is invalid");
        gl4_glGenBuffers(1, &buffer_);
        const ScopedGenericBufferBinding scope(GL_COPY_WRITE_BUFFER, GL_COPY_WRITE_BUFFER, buffer_);
        gl4_glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr4>(byteSize_), nullptr,
                         GL_DYNAMIC_DRAW);
    }

    OpenGL4StorageBufferRenderer::~OpenGL4StorageBufferRenderer()
    {
        const auto ownContext = EnterOwnContext();   // GL4-0021
        if (!ownContext || buffer_ == 0) return;
        gl4_glDeleteBuffers(1, &buffer_);
    }

    void OpenGL4StorageBufferRenderer::SetData(const void* data, const std::size_t byteSize)
    {
        (void)SetDataRangeEXT(0, data, byteSize);
    }

    void OpenGL4StorageBufferRenderer::GetData(void* out, const std::size_t byteSize) const
    {
        (void)GetDataRangeEXT(0, out, byteSize);
    }

    bool OpenGL4StorageBufferRenderer::SetDataRangeEXT(
        const std::size_t byteOffset, const void* data, const std::size_t byteSize)
    {
        if (byteOffset > byteSize_ || byteSize > byteSize_ - byteOffset)
            throw std::invalid_argument("OpenGL4 storage-buffer upload range exceeds allocation");
        if (data == nullptr && byteSize != 0)
            throw std::invalid_argument("OpenGL4 storage-buffer upload source is null");
        if (byteSize == 0) return true;
        const auto ownContext = EnterOwnContext();
        if (!ownContext) return false;
        // Ordered after every dispatch issued before it by the barrier each dispatch ends with
        // (OpenGL4ComputeShaderRenderer::DispatchEXT); a dispatch issued later reads these bytes.
        const ScopedGenericBufferBinding scope(GL_COPY_WRITE_BUFFER, GL_COPY_WRITE_BUFFER, buffer_);
        gl4_glBufferSubData(GL_COPY_WRITE_BUFFER, static_cast<GLintptr4>(byteOffset),
                            static_cast<GLsizeiptr4>(byteSize), data);
        return true;
    }

    bool OpenGL4StorageBufferRenderer::GetDataRangeEXT(
        const std::size_t byteOffset, void* out, const std::size_t byteSize) const
    {
        if (byteOffset > byteSize_ || byteSize > byteSize_ - byteOffset)
            throw std::invalid_argument("OpenGL4 storage-buffer read range exceeds allocation");
        if (out == nullptr && byteSize != 0)
            throw std::invalid_argument("OpenGL4 storage-buffer read destination is null");
        if (byteSize == 0) return true;
        const auto ownContext = EnterOwnContext();
        if (!ownContext) return false;
        const ScopedGenericBufferBinding scope(GL_COPY_READ_BUFFER, GL_COPY_READ_BUFFER, buffer_);
        gl4_glGetBufferSubData(GL_COPY_READ_BUFFER, static_cast<GLintptr4>(byteOffset),
                               static_cast<GLsizeiptr4>(byteSize), out);
        return true;
    }

    bool OpenGL4StorageBufferRenderer::CopyToEXT(
        IStorageBufferRenderer& destination, const std::size_t sourceByteOffset,
        const std::size_t destinationByteOffset, const std::size_t byteSize)
    {
        auto* target = dynamic_cast<OpenGL4StorageBufferRenderer*>(&destination);
        if (target == nullptr) return false;
        if (sourceByteOffset > byteSize_ || byteSize > byteSize_ - sourceByteOffset ||
            destinationByteOffset > target->byteSize_ ||
            byteSize > target->byteSize_ - destinationByteOffset)
            throw std::invalid_argument("OpenGL4 storage-buffer copy range exceeds allocation");
        if (target == this && byteSize != 0 &&
            sourceByteOffset < destinationByteOffset + byteSize &&
            destinationByteOffset < sourceByteOffset + byteSize)
            throw std::invalid_argument("OpenGL4 storage-buffer copy ranges overlap");
        if (byteSize == 0) return true;
        const auto ownContext = EnterOwnContext();
        if (!ownContext) return false;
        const ScopedGenericBufferBinding read(GL_COPY_READ_BUFFER, GL_COPY_READ_BUFFER, buffer_);
        const ScopedGenericBufferBinding write(GL_COPY_WRITE_BUFFER, GL_COPY_WRITE_BUFFER,
                                               target->buffer_);
        gl4_glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                                static_cast<GLintptr4>(sourceByteOffset),
                                static_cast<GLintptr4>(destinationByteOffset),
                                static_cast<GLsizeiptr4>(byteSize));
        return true;
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4ComputeShaderRenderer
    // ------------------------------------------------------------------------------------

    OpenGL4ComputeShaderRenderer::~OpenGL4ComputeShaderRenderer()
    {
        const auto ownContext = EnterOwnContext();   // GL4-0021
        if (!ownContext || program_ == 0) return;
        gl4_glDeleteProgram(program_);
    }

    bool OpenGL4ComputeShaderRenderer::CompileProgram(const std::string& computeSrc)
    {
        const auto ownContext = EnterOwnContext();
        compileError_.clear();
        valid_ = false;
        if (!ownContext)
        {
            compileError_ = "CS: the owning OpenGL context no longer exists";
            return false;
        }
        if (program_ != 0)
        {
            gl4_glDeleteProgram(program_);
            program_ = 0;
        }

        const GLuint shader = gl4_glCreateShader(GL_COMPUTE_SHADER);
        const char* source = computeSrc.c_str();
        gl4_glShaderSource(shader, 1, &source, nullptr);
        gl4_glCompileShader(shader);
        GLint compiled = GL_FALSE;
        gl4_glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled != GL_TRUE)
        {
            GLint length = 0;
            gl4_glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            gl4_glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
            log.resize(std::strlen(log.c_str()));
            gl4_glDeleteShader(shader);
            compileError_ = "CS: " + log;
            return false;
        }

        program_ = gl4_glCreateProgram();
        gl4_glAttachShader(program_, shader);
        gl4_glLinkProgram(program_);
        gl4_glDeleteShader(shader);   // flagged; freed with the program
        GLint linked = GL_FALSE;
        gl4_glGetProgramiv(program_, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE)
        {
            GLint length = 0;
            gl4_glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            gl4_glGetProgramInfoLog(program_, static_cast<GLsizei>(log.size()), nullptr,
                                    log.data());
            log.resize(std::strlen(log.c_str()));
            gl4_glDeleteProgram(program_);
            program_ = 0;
            compileError_ = "Link: " + log;
            return false;
        }
        valid_ = true;
        return true;
    }

    int OpenGL4ComputeShaderRenderer::UniformLocation(const char* name) const
    {
        if (!IsValid() || name == nullptr) return -1;
        return gl4_glGetUniformLocation(program_, name);
    }

    void OpenGL4ComputeShaderRenderer::SetUniformInt(const char* name, const int value)
    {
        const auto ownContext = EnterOwnContext();
        if (!ownContext) return;
        const int location = UniformLocation(name);
        if (location >= 0) gl4_glProgramUniform1i(program_, location, value);
    }

    void OpenGL4ComputeShaderRenderer::SetUniformFloat(const char* name, const float value)
    {
        const auto ownContext = EnterOwnContext();
        if (!ownContext) return;
        const int location = UniformLocation(name);
        if (location >= 0) gl4_glProgramUniform1f(program_, location, value);
    }

    void OpenGL4ComputeShaderRenderer::Record(std::vector<BufferBinding>& bindings,
                                              const int binding,
                                              const IStorageBufferRenderer* buffer)
    {
        bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                      [binding](const BufferBinding& entry) {
                                          return entry.binding == binding;
                                      }),
                       bindings.end());
        const auto* native = dynamic_cast<const OpenGL4StorageBufferRenderer*>(buffer);
        if (native == nullptr) return;
        // Held, so a buffer the caller disposes between binding and dispatch still exists when
        // the dispatch names it.
        bindings.push_back({binding, native->weak_from_this().lock(), native->GLHandle()});
    }

    void OpenGL4ComputeShaderRenderer::BindStorageBuffer(const int binding,
                                                         IStorageBufferRenderer* buffer)
    {
        if (binding < 0) return;
        Record(storageBuffers_, binding, buffer);
    }

    bool OpenGL4ComputeShaderRenderer::BindConstantBufferEXT(const int binding,
                                                             IStorageBufferRenderer* buffer)
    {
        if (binding < 0) return false;
        {
            const auto ownContext = EnterOwnContext();
            if (!ownContext) return false;
            if (binding >= QueryInteger(GL_MAX_COMPUTE_UNIFORM_BLOCKS) ||
                binding >= QueryInteger(GL_MAX_UNIFORM_BUFFER_BINDINGS))
                return false;
        }
        if (buffer != nullptr)
        {
            const auto* native = dynamic_cast<const OpenGL4StorageBufferRenderer*>(buffer);
            if (native == nullptr || (native->GetUsageEXT() & kUsageConstant) == 0) return false;
        }
        Record(constantBuffers_, binding, buffer);
        return true;
    }

    void OpenGL4ComputeShaderRenderer::BindTexture(const int unit, ITextureRenderer* texture)
    {
        if (unit < 0) return;
        textures_.erase(std::remove_if(textures_.begin(), textures_.end(),
                                       [unit](const TextureBinding& entry) {
                                           return entry.unit == unit;
                                       }),
                        textures_.end());
        const GLuint name = TextureName(texture);
        if (name == 0) return;
        textures_.push_back({unit, texture->weak_from_this().lock(), name});
    }

    void OpenGL4ComputeShaderRenderer::BindImageTexture(const int unit, ITextureRenderer* texture,
                                                        const int accessMode)
    {
        if (unit < 0) return;
        images_.erase(std::remove_if(images_.begin(), images_.end(),
                                     [unit](const ImageBinding& entry) {
                                         return entry.unit == unit;
                                     }),
                      images_.end());
        const GLuint name = TextureName(texture);
        if (name == 0) return;

        // The image format is the texture's own storage format -- asked of GL, because the
        // texture layer widens some XNA formats to wider GL storage.
        GLint internalFormat = 0;
        {
            const auto ownContext = EnterOwnContext();
            if (!ownContext) return;
            const Detail::ScopedTextureBinding scope(GL_TEXTURE_2D, name);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
        }
        if (!IsImageUnitFormat(static_cast<GLenum>(internalFormat)))
        {
            throw System::NotSupportedException(
                "OpenGL4: this texture's GL storage format (0x" +
                [](GLint v) {
                    char text[16];
                    std::snprintf(text, sizeof(text), "%04X", static_cast<unsigned>(v));
                    return std::string(text);
                }(internalFormat) +
                ") is not an image load/store format, so it cannot be bound as a compute image");
        }
        images_.push_back({unit, texture->weak_from_this().lock(), name,
                           ToGlImageAccess(accessMode), static_cast<GLenum>(internalFormat)});
    }

    void OpenGL4ComputeShaderRenderer::DispatchEXT(const unsigned int groupsX,
                                                   const unsigned int groupsY,
                                                   const unsigned int groupsZ,
                                                   const unsigned int sampler)
    {
        // Everything this dispatch changes, captured first so it can be put back: XNA state set
        // before a compute pass is the state after it (GL4-0025).
        const GLint previousProgram = QueryInteger(GL_CURRENT_PROGRAM);
        const GLint previousActiveTexture = QueryInteger(GL_ACTIVE_TEXTURE);
        const GLint previousGenericStorage = QueryInteger(GL_SHADER_STORAGE_BUFFER_BINDING);
        const GLint previousGenericUniform = QueryInteger(GL_UNIFORM_BUFFER_BINDING);

        struct IndexedRestore
        {
            GLenum target;
            GLuint index;
            GLint previous;
        };
        std::vector<IndexedRestore> indexed;
        indexed.reserve(storageBuffers_.size() + constantBuffers_.size());
        const auto bindIndexed = [&](const GLenum target, const GLenum bindingQuery,
                                     const BufferBinding& entry) {
            GLint previous = 0;
            gl4_glGetIntegeri_v(bindingQuery, static_cast<GLuint>(entry.binding), &previous);
            indexed.push_back({target, static_cast<GLuint>(entry.binding), previous});
            gl4_glBindBufferBase(target, static_cast<GLuint>(entry.binding), entry.name);
        };
        for (const BufferBinding& entry : storageBuffers_)
            bindIndexed(GL_SHADER_STORAGE_BUFFER, GL_SHADER_STORAGE_BUFFER_BINDING, entry);
        for (const BufferBinding& entry : constantBuffers_)
            bindIndexed(GL_UNIFORM_BUFFER, GL_UNIFORM_BUFFER_BINDING, entry);

        struct UnitRestore
        {
            GLuint unit;
            GLint texture;
            GLint sampler;
        };
        std::vector<UnitRestore> units;
        units.reserve(textures_.size());
        for (const TextureBinding& entry : textures_)
        {
            const GLuint unit = static_cast<GLuint>(entry.unit);
            gl4_glActiveTexture(GL_TEXTURE0 + unit);
            units.push_back({unit, QueryInteger(GL_TEXTURE_BINDING_2D),
                             QueryInteger(GL_SAMPLER_BINDING)});
            glBindTexture(GL_TEXTURE_2D, entry.name);
            // The sampler state a draw left on this unit is not the compute program's: sampled
            // compute inputs read through the renderer's own nearest/clamp sampler.
            gl4_glBindSampler(unit, sampler);
        }

        for (const ImageBinding& entry : images_)
            gl4_glBindImageTexture(static_cast<GLuint>(entry.unit), entry.name, 0, GL_FALSE, 0,
                                   entry.access, entry.format);

        gl4_glUseProgram(program_);
        gl4_glDispatchCompute(groupsX, groupsY, groupsZ);
        // ADR 0001: what a dispatch wrote is visible to whatever the device does next -- a
        // following dispatch, a draw reading it as vertices, indices, indirect arguments,
        // uniforms or textures, or a transfer -- without the caller naming a barrier. One GPU-side
        // barrier per dispatch; nothing here waits on the CPU.
        gl4_glMemoryBarrier(GL_ALL_BARRIER_BITS);

        for (const ImageBinding& entry : images_)
            gl4_glBindImageTexture(static_cast<GLuint>(entry.unit), 0, 0, GL_FALSE, 0,
                                   GL_READ_ONLY, GL_RGBA8);
        for (auto it = units.rbegin(); it != units.rend(); ++it)
        {
            gl4_glActiveTexture(GL_TEXTURE0 + it->unit);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(it->texture));
            gl4_glBindSampler(it->unit, static_cast<GLuint>(it->sampler));
        }
        for (auto it = indexed.rbegin(); it != indexed.rend(); ++it)
            gl4_glBindBufferBase(it->target, it->index, static_cast<GLuint>(it->previous));
        gl4_glBindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(previousGenericStorage));
        gl4_glBindBuffer(GL_UNIFORM_BUFFER, static_cast<GLuint>(previousGenericUniform));
        gl4_glActiveTexture(static_cast<GLenum>(previousActiveTexture));
        gl4_glUseProgram(static_cast<GLuint>(previousProgram));
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4Renderer -- the modern entry points
    // ------------------------------------------------------------------------------------

    bool OpenGL4Renderer::SupportsComputeShadersEXT() const
    {
        // The native fact is not enough on its own: every desktop compute payload CNA ships is
        // `#version 430 core`, so a 4.2 context that exposes GL_ARB_compute_shader could dispatch
        // but could not compile one. The promise is made only where both hold.
        const auto& facts = modernCapabilities_;
        const bool core43 = facts.contextMajor > 4 ||
                            (facts.contextMajor == 4 && facts.contextMinor >= 3);
        return core43 && facts.computeShadersNative && facts.shaderStorageBuffersNative;
    }

    bool OpenGL4Renderer::SupportsComputeImageBindingEXT() const
    {
        // Desktop GL binds any texture level as an image; unlike ES 3.1 it does not require the
        // immutable storage CNA's mutable Texture2D path does not allocate.
        return SupportsComputeShadersEXT() && modernCapabilities_.imageLoadStoreNative;
    }

    int OpenGL4Renderer::GetMaxComputeWorkGroupCountEXT(const int axis) const
    {
        if (!SupportsComputeShadersEXT() || axis < 0 || axis > 2) return 0;
        GLint value = 0;
        gl4_glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, static_cast<GLuint>(axis), &value);
        return static_cast<int>(value);
    }

    int OpenGL4Renderer::GetMaxComputeWorkGroupSizeEXT(const int axis) const
    {
        if (!SupportsComputeShadersEXT() || axis < 0 || axis > 2) return 0;
        GLint value = 0;
        gl4_glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, static_cast<GLuint>(axis), &value);
        return static_cast<int>(value);
    }

    int OpenGL4Renderer::GetMaxComputeWorkGroupInvocationsEXT() const
    {
        if (!SupportsComputeShadersEXT()) return 0;
        return static_cast<int>(QueryInteger(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS));
    }

    int OpenGL4Renderer::GetMaxVertexShaderStorageBlocksEXT() const
    {
        if (!SupportsComputeShadersEXT()) return 0;
        return static_cast<int>(QueryInteger(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS));
    }

    std::uint64_t OpenGL4Renderer::GetMaxStorageBufferBytesEXT() const
    {
        if (!SupportsComputeShadersEXT()) return 0;
        std::int64_t value = 0;
        gl4_glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &value);
        return value > 0 ? static_cast<std::uint64_t>(value) : 0;
    }

    std::uint64_t OpenGL4Renderer::GetMaxUniformBufferBytesEXT() const
    {
        // Uniform blocks are core from 3.1, so every context this renderer accepts has them; the
        // only consumer CNA has for them is a compute constant buffer.
        if (gl4_glGetInteger64v == nullptr) return 0;
        std::int64_t value = 0;
        gl4_glGetInteger64v(GL_MAX_UNIFORM_BLOCK_SIZE, &value);
        return value > 0 ? static_cast<std::uint64_t>(value) : 0;
    }

    int OpenGL4Renderer::GetMaxComputeStorageBufferBindingsEXT() const
    {
        if (!SupportsComputeShadersEXT()) return 0;
        return static_cast<int>(std::min(QueryInteger(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS),
                                         QueryInteger(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS)));
    }

    int OpenGL4Renderer::GetMaxSampledTexturesPerShaderStageEXT() const
    {
        // Per stage, and the smallest stage decides: a shader of any stage may rely on it.
        GLint value = std::min(QueryInteger(GL_MAX_TEXTURE_IMAGE_UNITS),
                               QueryInteger(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS));
        if (SupportsComputeShadersEXT())
            value = std::min(value, QueryInteger(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS));
        return static_cast<int>(value);
    }

    int OpenGL4Renderer::GetMaxStorageImagesPerShaderStageEXT() const
    {
        // Compute is the only stage CNA binds images to.
        if (!SupportsComputeImageBindingEXT()) return 0;
        return static_cast<int>(std::min(QueryInteger(GL_MAX_COMPUTE_IMAGE_UNIFORMS),
                                         QueryInteger(GL_MAX_IMAGE_UNITS)));
    }

    int OpenGL4Renderer::GetMaxVertexInputBindingsEXT() const
    {
        // Every per-vertex stream is its own buffer bound at its own attribute locations
        // (GL4-0013), so the bound is the renderer's stream ceiling.
        return GetMaxVertexStreams();
    }

    int OpenGL4Renderer::GetMaxVertexInputAttributesEXT() const
    {
        return static_cast<int>(QueryInteger(GL_MAX_VERTEX_ATTRIBS));
    }

    int OpenGL4Renderer::GetMaxColorAttachmentsEXT() const
    {
        return maxMrtTargets_;
    }

    std::uint64_t OpenGL4Renderer::GetMinStorageBufferOffsetAlignmentEXT() const
    {
        if (!SupportsComputeShadersEXT()) return 0;
        const GLint value = QueryInteger(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT);
        return value > 0 ? static_cast<std::uint64_t>(value) : 0;
    }

    std::uint64_t OpenGL4Renderer::GetMinUniformBufferOffsetAlignmentEXT() const
    {
        if (GetMaxUniformBufferBytesEXT() == 0) return 0;
        const GLint value = QueryInteger(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT);
        return value > 0 ? static_cast<std::uint64_t>(value) : 0;
    }

    std::unique_ptr<IComputeShaderRenderer> OpenGL4Renderer::CreateComputeShader(
        const std::string& computeSrc)
    {
        EnsureCallingThreadContext();
        if (!SupportsComputeShadersEXT()) return nullptr;
        auto shader = std::make_unique<OpenGL4ComputeShaderRenderer>();
        shader->AttachOwningContext(platformContext_);
        // A failed compile still returns the object: its GetCompileError() is the diagnostic.
        (void)shader->CompileProgram(computeSrc);
        return shader;
    }

    std::unique_ptr<IStorageBufferRenderer> OpenGL4Renderer::CreateStorageBuffer(
        const std::size_t byteSize)
    {
        EnsureCallingThreadContext();
        if (!SupportsComputeShadersEXT() || byteSize == 0) return nullptr;
        auto buffer = std::make_unique<OpenGL4StorageBufferRenderer>(
            byteSize, UINT32_C(0x0F), UINT32_C(0x03));
        buffer->AttachOwningContext(platformContext_);
        return buffer;
    }

    std::unique_ptr<IStorageBufferRenderer> OpenGL4Renderer::CreateStorageBufferEXT(
        const std::size_t byteSize, const std::uint32_t usage, const std::uint32_t cpuAccess)
    {
        EnsureCallingThreadContext();
        if (byteSize == 0 || usage == 0 || (usage & ~kUsageMask) != 0 ||
            (cpuAccess & ~kCpuAccessMask) != 0)
            return nullptr;
        if ((usage & kUsageStorage) != 0 && !SupportsComputeShadersEXT()) return nullptr;
        if ((usage & kUsageIndirectArguments) != 0 && !SupportsIndirectDrawEXT()) return nullptr;
        if ((usage & kUsageConstant) != 0)
        {
            const std::uint64_t maximum = GetMaxUniformBufferBytesEXT();
            if (maximum == 0 || static_cast<std::uint64_t>(byteSize) > maximum) return nullptr;
        }
        auto buffer = std::make_unique<OpenGL4StorageBufferRenderer>(byteSize, usage, cpuAccess);
        buffer->AttachOwningContext(platformContext_);
        return buffer;
    }

    void OpenGL4Renderer::DispatchCompute(IComputeShaderRenderer* shader, const int groupsX,
                                          const int groupsY, const int groupsZ)
    {
        auto* native = dynamic_cast<OpenGL4ComputeShaderRenderer*>(shader);
        if (native == nullptr || !native->IsValid()) return;
        if (groupsX <= 0 || groupsY <= 0 || groupsZ <= 0) return;
        EnsureCallingThreadContext();   // GL4-0021
        if (computeSampler_ == 0)
        {
            gl4_glGenSamplers(1, &computeSampler_);
            gl4_glSamplerParameteri(computeSampler_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl4_glSamplerParameteri(computeSampler_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl4_glSamplerParameteri(computeSampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl4_glSamplerParameteri(computeSampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        native->DispatchEXT(static_cast<unsigned int>(groupsX), static_cast<unsigned int>(groupsY),
                            static_cast<unsigned int>(groupsZ), computeSampler_);
    }

    void OpenGL4Renderer::MemoryBarrierEXT(const int barrierBits)
    {
        if (!SupportsComputeShadersEXT() || barrierBits == 0) return;
        EnsureCallingThreadContext();
        using CNA::GraphicsMemoryBarrier;
        const auto requested = static_cast<GraphicsMemoryBarrier>(barrierBits);
        // Translated bit by bit: the ordinals are CNA's own, not GL's.
        GLbitfield native = 0;
        const auto add = [&](const GraphicsMemoryBarrier bit, const GLbitfield gl) {
            if (CNA::HasBarrier(requested, bit)) native |= gl;
        };
        add(GraphicsMemoryBarrier::VertexAttribArray, GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
        add(GraphicsMemoryBarrier::ElementArray, GL_ELEMENT_ARRAY_BARRIER_BIT);
        add(GraphicsMemoryBarrier::Uniform, GL_UNIFORM_BARRIER_BIT);
        add(GraphicsMemoryBarrier::TextureFetch, GL_TEXTURE_FETCH_BARRIER_BIT);
        add(GraphicsMemoryBarrier::ShaderImageAccess, GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        add(GraphicsMemoryBarrier::ShaderStorage, GL_SHADER_STORAGE_BARRIER_BIT);
        add(GraphicsMemoryBarrier::BufferUpdate, GL_BUFFER_UPDATE_BARRIER_BIT);
        add(GraphicsMemoryBarrier::Framebuffer, GL_FRAMEBUFFER_BARRIER_BIT);
        add(GraphicsMemoryBarrier::IndirectCommand, GL_COMMAND_BARRIER_BIT);
        if (native != 0) gl4_glMemoryBarrier(native);
    }

    void OpenGL4Renderer::BindStorageBufferForDrawEXT(const int binding,
                                                      const IStorageBufferRenderer& buffer)
    {
        if (!SupportsComputeShadersEXT() || binding < 0) return;
        const auto* native = dynamic_cast<const OpenGL4StorageBufferRenderer*>(&buffer);
        if (native == nullptr) return;
        EnsureCallingThreadContext();
        // Indexed shader-storage bindings are context state every stage reads, so the next draw's
        // vertex shader sees this buffer; a dispatch in between restores it (DispatchEXT).
        gl4_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(binding),
                             native->GLHandle());
    }
}
