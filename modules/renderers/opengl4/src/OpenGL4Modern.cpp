// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md Workstream B: storage and constant buffers, compute
// programs, dispatch ordering and the limits that describe them, over desktop OpenGL 4.3+.

#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Modern.hpp"

#include "CNA/GraphicsImageAccess.hpp"
#include "CNA/GraphicsMemoryBarrier.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Renderer.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Resources.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
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

    bool OpenGL4ComputeShaderRenderer::BindStorageTexture2DEXT(
        const int unit, std::shared_ptr<IStorageTexture2DRenderer> texture, const int accessMode)
    {
        if (unit < 0) return false;
        {
            const auto ownContext = EnterOwnContext();
            if (!ownContext) return false;
            if (unit >= std::min(QueryInteger(GL_MAX_COMPUTE_IMAGE_UNIFORMS),
                                 QueryInteger(GL_MAX_IMAGE_UNITS)))
                return false;
        }
        const OpenGL4StorageTexture2DRenderer* native = nullptr;
        if (texture != nullptr)
        {
            native = dynamic_cast<const OpenGL4StorageTexture2DRenderer*>(texture.get());
            if (native == nullptr) return false;
        }
        images_.erase(std::remove_if(images_.begin(), images_.end(),
                                     [unit](const ImageBinding& entry) {
                                         return entry.unit == unit;
                                     }),
                      images_.end());
        if (native != nullptr)
            images_.push_back({unit, texture, native->GLHandle(), ToGlImageAccess(accessMode),
                               native->InternalFormat()});
        return true;
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
    // OpenGL4StorageTexture2DRenderer (GL4-0030)
    // ------------------------------------------------------------------------------------

    bool MapStorageImageFormat(const int surfaceFormat, OpenGL4StorageImageFormat& out)
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Color:
                out = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4}; return true;
            case SurfaceFormat::NormalizedByte2:
                out = {GL_RG8_SNORM, GL_RG, GL_BYTE, 2}; return true;
            case SurfaceFormat::NormalizedByte4:
                out = {GL_RGBA8_SNORM, GL_RGBA, GL_BYTE, 4}; return true;
            case SurfaceFormat::Rgba1010102:
                // XNA packs R in the low ten bits and A in the top two: GL's _REV layout.
                out = {GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, 4}; return true;
            case SurfaceFormat::Rg32:
                out = {GL_RG16, GL_RG, GL_UNSIGNED_SHORT, 4}; return true;
            case SurfaceFormat::Rgba64:
                out = {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT, 8}; return true;
            case SurfaceFormat::Single:
                out = {GL_R32F, GL_RED, GL_FLOAT, 4}; return true;
            case SurfaceFormat::Vector2:
                out = {GL_RG32F, GL_RG, GL_FLOAT, 8}; return true;
            case SurfaceFormat::Vector4:
                out = {GL_RGBA32F, GL_RGBA, GL_FLOAT, 16}; return true;
            case SurfaceFormat::HalfSingle:
                out = {GL_R16F, GL_RED, GL_HALF_FLOAT, 2}; return true;
            case SurfaceFormat::HalfVector2:
                out = {GL_RG16F, GL_RG, GL_HALF_FLOAT, 4}; return true;
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                out = {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8}; return true;
            case SurfaceFormat::ByteEXT:
                out = {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1}; return true;
            case SurfaceFormat::UShortEXT:
                out = {GL_R16, GL_RED, GL_UNSIGNED_SHORT, 2}; return true;
            default:
                out = {};
                return false;
        }
    }

    OpenGL4StorageTexture2DRenderer::OpenGL4StorageTexture2DRenderer(
        const int width, const int height, const int mipLevelCount,
        const OpenGL4StorageImageFormat& format)
        : width_(width), height_(height), levels_(std::max(1, mipLevelCount)), format_(format)
    {
        glGenTextures(1, &texture_);
        const Detail::ScopedTextureBinding scope(GL_TEXTURE_2D, texture_);
        const Detail::ScopedUnpackState unpack(1);
        int levelWidth = width_;
        int levelHeight = height_;
        for (int level = 0; level < levels_; ++level)
        {
            glTexImage2D(GL_TEXTURE_2D, level, static_cast<GLint>(format_.internalFormat),
                         levelWidth, levelHeight, 0, format_.pixelFormat, format_.pixelType,
                         nullptr);
            levelWidth = std::max(1, levelWidth / 2);
            levelHeight = std::max(1, levelHeight / 2);
        }
        // Every declared level exists and no other does: the texture is complete, which an image
        // unit requires, whatever filter a later draw samples it with.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels_ - 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        levels_ > 1 ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    OpenGL4StorageTexture2DRenderer::~OpenGL4StorageTexture2DRenderer()
    {
        const auto ownContext = EnterOwnContext();   // GL4-0021
        if (!ownContext || texture_ == 0) return;
        glDeleteTextures(1, &texture_);
    }

    bool OpenGL4StorageTexture2DRenderer::ValidRegion(const int mipLevel, const int x, const int y,
                                                      const int width, const int height,
                                                      const std::size_t byteCount,
                                                      int& levelWidth, int& levelHeight) const
    {
        if (mipLevel < 0 || mipLevel >= levels_) return false;
        levelWidth = std::max(1, width_ >> mipLevel);
        levelHeight = std::max(1, height_ >> mipLevel);
        if (x < 0 || y < 0 || width <= 0 || height <= 0 || x > levelWidth - width ||
            y > levelHeight - height)
            return false;
        return byteCount == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                                static_cast<std::size_t>(format_.bytesPerTexel);
    }

    bool OpenGL4StorageTexture2DRenderer::SetData(const int mipLevel, const int x, const int y,
                                                  const int width, const int height,
                                                  const void* data, const std::size_t byteCount)
    {
        int levelWidth = 0;
        int levelHeight = 0;
        if (data == nullptr ||
            !ValidRegion(mipLevel, x, y, width, height, byteCount, levelWidth, levelHeight))
            return false;
        const auto ownContext = EnterOwnContext();
        if (!ownContext) return false;
        DrainGlErrors();
        const Detail::ScopedTextureBinding scope(GL_TEXTURE_2D, texture_);
        const Detail::ScopedUnpackState unpack(1);
        glTexSubImage2D(GL_TEXTURE_2D, mipLevel, x, y, width, height, format_.pixelFormat,
                        format_.pixelType, data);
        return GlOperationSucceeded();
    }

    bool OpenGL4StorageTexture2DRenderer::GetData(const int mipLevel, const int x, const int y,
                                                  const int width, const int height, void* data,
                                                  const std::size_t byteCount) const
    {
        int levelWidth = 0;
        int levelHeight = 0;
        if (data == nullptr ||
            !ValidRegion(mipLevel, x, y, width, height, byteCount, levelWidth, levelHeight))
            return false;
        const auto ownContext = EnterOwnContext();
        if (!ownContext) return false;
        const std::size_t texel = static_cast<std::size_t>(format_.bytesPerTexel);
        std::vector<std::uint8_t> level(static_cast<std::size_t>(levelWidth) *
                                        static_cast<std::size_t>(levelHeight) * texel);
        DrainGlErrors();
        {
            const Detail::ScopedTextureBinding scope(GL_TEXTURE_2D, texture_);
            const Detail::ScopedPackState pack(1);
            glGetTexImage(GL_TEXTURE_2D, mipLevel, format_.pixelFormat, format_.pixelType,
                          level.data());
        }
        if (!GlOperationSucceeded()) return false;
        auto* out = static_cast<std::uint8_t*>(data);
        const std::size_t rowBytes = static_cast<std::size_t>(width) * texel;
        for (int row = 0; row < height; ++row)
        {
            const std::size_t source = (static_cast<std::size_t>(y + row) *
                                            static_cast<std::size_t>(levelWidth) +
                                        static_cast<std::size_t>(x)) * texel;
            std::memcpy(out + static_cast<std::size_t>(row) * rowBytes, level.data() + source,
                        rowBytes);
        }
        return true;
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4Texture2DArrayRenderer (GL4-0037)
    // ------------------------------------------------------------------------------------

    OpenGL4Texture2DArrayRenderer::OpenGL4Texture2DArrayRenderer(
        const int width, const int height, const int layerCount, const int mipLevelCount,
        const int surfaceFormat, const bool filterable)
        : width_(width), height_(height), layers_(layerCount),
          levels_(std::max(1, mipLevelCount)), surfaceFormat_(surfaceFormat),
          compressed_(Detail::IsDxtFormat(surfaceFormat)), filterable_(filterable)
    {
        if (!compressed_ && !Detail::MapTextureTransferFormat(surfaceFormat_, true, transfer_))
            throw std::runtime_error("OpenGL4: no Texture2D storage for SurfaceFormat ordinal " +
                                     std::to_string(surfaceFormat_));
        glGenTextures(1, &texture_);
        const Detail::ScopedTextureBinding scope(GL_TEXTURE_2D_ARRAY, texture_);
        const Detail::ScopedUnpackState unpack(1);
        for (int level = 0; level < levels_; ++level)
        {
            const int levelWidth = std::max(1, width_ >> level);
            const int levelHeight = std::max(1, height_ >> level);
            if (compressed_)
            {
                // Zero blocks, not an undefined allocation: a region never written reads back
                // as zeros, as a TextureCube's does.
                const std::size_t bytes =
                    Detail::DxtImageBytes(surfaceFormat_, levelWidth, levelHeight) *
                    static_cast<std::size_t>(layers_);
                const std::vector<std::uint8_t> zero(bytes, 0u);
                gl4_glCompressedTexImage3D(GL_TEXTURE_2D_ARRAY, level,
                                           Detail::DxtInternalFormat(surfaceFormat_), levelWidth,
                                           levelHeight, layers_, 0,
                                           static_cast<GLsizei>(bytes), zero.data());
                continue;
            }
            // Zero declared-format texels, stored the way a Texture2D stores them (a widened
            // Single still samples as (0, 1, 1, 1), Direct3D 9's expansion).
            const std::size_t texels = static_cast<std::size_t>(levelWidth) *
                                       static_cast<std::size_t>(levelHeight) *
                                       static_cast<std::size_t>(layers_);
            const std::vector<std::uint8_t> zero(
                texels * static_cast<std::size_t>(
                             Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(
                                 static_cast<Microsoft::Xna::Framework::Graphics::SurfaceFormat>(
                                     surfaceFormat_))),
                0u);
            std::vector<std::uint8_t> scratch;
            const void* upload =
                Detail::ExpandTexelsForTransfer(surfaceFormat_, zero.data(), texels, scratch);
            gl4_glTexImage3D(GL_TEXTURE_2D_ARRAY, level,
                             static_cast<GLint>(transfer_.internalFormat), levelWidth,
                             levelHeight, layers_, 0, transfer_.pixelFormat, transfer_.pixelType,
                             upload);
        }
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, levels_ - 1);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    OpenGL4Texture2DArrayRenderer::~OpenGL4Texture2DArrayRenderer()
    {
        const auto ownContext = EnterOwnContext();   // GL4-0021
        if (!ownContext || texture_ == 0) return;
        glDeleteTextures(1, &texture_);
    }

    std::size_t OpenGL4Texture2DArrayRenderer::RegionBytes(const int width,
                                                           const int height) const
    {
        if (compressed_)
            return static_cast<std::size_t>((width + 3) / 4) *
                   static_cast<std::size_t>((height + 3) / 4) *
                   Detail::DxtBlockBytes(surfaceFormat_);
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
               static_cast<std::size_t>(Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(
                   static_cast<Microsoft::Xna::Framework::Graphics::SurfaceFormat>(surfaceFormat_)));
    }

    bool OpenGL4Texture2DArrayRenderer::ValidRegion(const int layer, const int mipLevel,
                                                    const int x, const int y, const int width,
                                                    const int height, const std::size_t byteCount,
                                                    int& levelWidth, int& levelHeight) const
    {
        if (layer < 0 || layer >= layers_ || mipLevel < 0 || mipLevel >= levels_) return false;
        levelWidth = std::max(1, width_ >> mipLevel);
        levelHeight = std::max(1, height_ >> mipLevel);
        if (x < 0 || y < 0 || width <= 0 || height <= 0 || x > levelWidth - width ||
            y > levelHeight - height)
            return false;
        if (compressed_ &&
            ((x % 4) != 0 || (y % 4) != 0 ||
             ((width % 4) != 0 && x + width != levelWidth) ||
             ((height % 4) != 0 && y + height != levelHeight)))
            return false;
        return byteCount == RegionBytes(width, height);
    }

    bool OpenGL4Texture2DArrayRenderer::SetData(const int layer, const int mipLevel, const int x,
                                                const int y, const int width, const int height,
                                                const void* data, const std::size_t byteCount)
    {
        int levelWidth = 0;
        int levelHeight = 0;
        if (data == nullptr || !ValidRegion(layer, mipLevel, x, y, width, height, byteCount,
                                            levelWidth, levelHeight))
            return false;
        const auto ownContext = EnterOwnContext();
        if (!ownContext) return false;
        DrainGlErrors();
        const Detail::ScopedTextureBinding scope(GL_TEXTURE_2D_ARRAY, texture_);
        const Detail::ScopedUnpackState unpack(1);
        if (compressed_)
        {
            gl4_glCompressedTexSubImage3D(GL_TEXTURE_2D_ARRAY, mipLevel, x, y, layer, width,
                                          height, 1, Detail::DxtInternalFormat(surfaceFormat_),
                                          static_cast<GLsizei>(byteCount), data);
        }
        else
        {
            std::vector<std::uint8_t> scratch;
            const void* upload = Detail::ExpandTexelsForTransfer(
                surfaceFormat_, data,
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height), scratch);
            gl4_glTexSubImage3D(GL_TEXTURE_2D_ARRAY, mipLevel, x, y, layer, width, height, 1,
                                transfer_.pixelFormat, transfer_.pixelType, upload);
        }
        return GlOperationSucceeded();
    }

    bool OpenGL4Texture2DArrayRenderer::GetData(const int layer, const int mipLevel, const int x,
                                                const int y, const int width, const int height,
                                                void* data, const std::size_t byteCount) const
    {
        int levelWidth = 0;
        int levelHeight = 0;
        if (data == nullptr || !ValidRegion(layer, mipLevel, x, y, width, height, byteCount,
                                            levelWidth, levelHeight))
            return false;
        const auto ownContext = EnterOwnContext();
        if (!ownContext) return false;
        auto* out = static_cast<std::uint8_t*>(data);
        DrainGlErrors();
        const Detail::ScopedTextureBinding scope(GL_TEXTURE_2D_ARRAY, texture_);
        const Detail::ScopedPackState pack(1);
        if (compressed_)
        {
            const std::size_t blockBytes = Detail::DxtBlockBytes(surfaceFormat_);
            const std::size_t layerBytes =
                Detail::DxtImageBytes(surfaceFormat_, levelWidth, levelHeight);
            std::vector<std::uint8_t> level(layerBytes * static_cast<std::size_t>(layers_));
            gl4_glGetCompressedTexImage(GL_TEXTURE_2D_ARRAY, mipLevel, level.data());
            if (!GlOperationSucceeded()) return false;
            const std::size_t columns = static_cast<std::size_t>((levelWidth + 3) / 4);
            const std::size_t copyColumns = static_cast<std::size_t>((width + 3) / 4);
            const std::size_t copyRows = static_cast<std::size_t>((height + 3) / 4);
            const std::uint8_t* slice = level.data() + layerBytes * static_cast<std::size_t>(layer);
            for (std::size_t row = 0; row < copyRows; ++row)
            {
                const std::size_t source =
                    ((static_cast<std::size_t>(y / 4) + row) * columns +
                     static_cast<std::size_t>(x / 4)) * blockBytes;
                std::memcpy(out + row * copyColumns * blockBytes, slice + source,
                            copyColumns * blockBytes);
            }
            return true;
        }

        const std::size_t transferTexel =
            static_cast<std::size_t>(transfer_.transferBytesPerTexel);
        const std::size_t declaredTexel =
            static_cast<std::size_t>(Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(
                static_cast<Microsoft::Xna::Framework::Graphics::SurfaceFormat>(surfaceFormat_)));
        const std::size_t layerTexels =
            static_cast<std::size_t>(levelWidth) * static_cast<std::size_t>(levelHeight);
        std::vector<std::uint8_t> level(layerTexels * static_cast<std::size_t>(layers_) *
                                        transferTexel);
        glGetTexImage(GL_TEXTURE_2D_ARRAY, mipLevel, transfer_.pixelFormat, transfer_.pixelType,
                      level.data());
        if (!GlOperationSucceeded()) return false;
        const std::uint8_t* slice =
            level.data() + layerTexels * transferTexel * static_cast<std::size_t>(layer);
        for (int row = 0; row < height; ++row)
        {
            const std::size_t source = (static_cast<std::size_t>(y + row) *
                                            static_cast<std::size_t>(levelWidth) +
                                        static_cast<std::size_t>(x)) * transferTexel;
            Detail::CollapseTransferTexels(
                surfaceFormat_, slice + source, static_cast<std::size_t>(width),
                out + static_cast<std::size_t>(row) * static_cast<std::size_t>(width) *
                          declaredTexel);
        }
        return true;
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4GpuTimerRenderer
    // ------------------------------------------------------------------------------------

    OpenGL4GpuTimerRenderer::OpenGL4GpuTimerRenderer()
    {
        gl4_glGenQueries(2, queries_);
    }

    OpenGL4GpuTimerRenderer::~OpenGL4GpuTimerRenderer()
    {
        const auto ownContext = EnterOwnContext();   // GL4-0021
        if (!ownContext) return;
        gl4_glDeleteQueries(2, queries_);
    }

    void OpenGL4GpuTimerRenderer::Begin()
    {
        if (open_) return;
        const auto ownContext = EnterOwnContext();
        if (!ownContext) return;
        gl4_glQueryCounter(queries_[0], GL_TIMESTAMP);
        open_ = true;
        closed_ = false;
        cached_ = false;
        nanoseconds_ = 0;
    }

    void OpenGL4GpuTimerRenderer::End()
    {
        if (!open_) return;
        const auto ownContext = EnterOwnContext();
        if (!ownContext) return;
        gl4_glQueryCounter(queries_[1], GL_TIMESTAMP);
        open_ = false;
        closed_ = true;
    }

    bool OpenGL4GpuTimerRenderer::IsResultAvailable() const
    {
        if (!closed_) return false;
        if (cached_) return true;
        const auto ownContext = EnterOwnContext();
        if (!ownContext) return false;
        // Timestamps complete in command order, so the closing one arriving means both have; both
        // are still asked, because nothing is gained by trusting that.
        GLuint available[2] = {0, 0};
        gl4_glGetQueryObjectuiv(queries_[1], GL_QUERY_RESULT_AVAILABLE, &available[1]);
        if (available[1] == 0) return false;
        gl4_glGetQueryObjectuiv(queries_[0], GL_QUERY_RESULT_AVAILABLE, &available[0]);
        if (available[0] == 0) return false;
        std::uint64_t begin = 0;
        std::uint64_t end = 0;
        gl4_glGetQueryObjectui64v(queries_[0], GL_QUERY_RESULT, &begin);
        gl4_glGetQueryObjectui64v(queries_[1], GL_QUERY_RESULT, &end);
        nanoseconds_ = end >= begin ? end - begin : 0;
        cached_ = true;
        return true;
    }

    std::uint64_t OpenGL4GpuTimerRenderer::ElapsedNanoseconds() const
    {
        return IsResultAvailable() ? nanoseconds_ : 0;
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
        // A region a capture tool shows, around the only work this renderer does on its own
        // initiative rather than as one XNA call; only when debug output is on.
        const bool group = debugOutputEnabled_ && gl4_glPushDebugGroup != nullptr &&
                           gl4_glPopDebugGroup != nullptr;
        if (group)
            gl4_glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "CNA compute dispatch");
        native->DispatchEXT(static_cast<unsigned int>(groupsX), static_cast<unsigned int>(groupsY),
                            static_cast<unsigned int>(groupsZ), computeSampler_);
        if (group) gl4_glPopDebugGroup();
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

    std::unique_ptr<ITexture2DArrayRenderer> OpenGL4Renderer::CreateTexture2DArrayEXT(
        const int width, const int height, const int layerCount, const int mipLevelCount,
        const int surfaceFormat, const std::uint32_t usage)
    {
        EnsureCallingThreadContext();
        if (!modernCapabilities_.textureArraysNative) return nullptr;
        // Exactly the Texture2D formats of this renderer, each stored the way a Texture2D is; a
        // Dxt array needs the blocks natively, because readback returns them.
        if (ClassifySurfaceFormatEXT(surfaceFormat) != RendererFormatVerdict::Supported)
            return nullptr;
        if (Detail::IsDxtFormat(surfaceFormat) && !Detail::ContextHasS3tc()) return nullptr;
        if (width <= 0 || height <= 0 || layerCount <= 0 || mipLevelCount <= 0 ||
            width > GetMaxTextureDimension() || height > GetMaxTextureDimension() ||
            layerCount > GetMaxTextureArrayLayersEXT())
            return nullptr;
        constexpr std::uint32_t kFilterable = UINT32_C(1) << 1;
        auto texture = std::make_unique<OpenGL4Texture2DArrayRenderer>(
            width, height, layerCount, mipLevelCount, surfaceFormat, (usage & kFilterable) != 0);
        texture->AttachOwningContext(platformContext_);
        return texture;
    }

    void OpenGL4Renderer::RequireFilterableTextureArraysEXT(const IEffectRenderer& effect) const
    {
        const auto* native = dynamic_cast<const OpenGL4EffectRenderer*>(&effect);
        if (native == nullptr) return;
        for (int unit = 0; unit < kMaxSamplerSlots && unit < 16; ++unit)
        {
            const OpenGL4Texture2DArrayRenderer* array = native->TextureArrayAtEXT(unit);
            // XNA TextureFilter.Point (1) is the one ordinal with neither linear nor mip filtering.
            if (array != nullptr && !array->IsFilterableEXT() &&
                samplerFilters_[static_cast<std::size_t>(unit)] != 1)
            {
                throw System::NotSupportedException(
                    "OpenGL4: a Texture2DArray without Filterable usage was paired with a linear, "
                    "mip or anisotropic sampler in unit " + std::to_string(unit) +
                    "; declare Texture2DArrayUsage::Filterable or sample it with Point.");
            }
        }
    }

    int OpenGL4Renderer::GetMaxTextureArrayLayersEXT() const
    {
        if (!modernCapabilities_.textureArraysNative) return 0;
        return static_cast<int>(QueryInteger(GL_MAX_ARRAY_TEXTURE_LAYERS));
    }

    std::unique_ptr<IStorageTexture2DRenderer> OpenGL4Renderer::CreateStorageTexture2DEXT(
        const int width, const int height, const int mipLevelCount, const int surfaceFormat,
        const std::uint32_t usage)
    {
        EnsureCallingThreadContext();
        (void)usage;   // every storage texture here is readable, writable, sampled and copyable
        OpenGL4StorageImageFormat format;
        if (!SupportsComputeImageBindingEXT() || !MapStorageImageFormat(surfaceFormat, format))
            return nullptr;
        if (width <= 0 || height <= 0 || width > GetMaxTextureDimension() ||
            height > GetMaxTextureDimension() || mipLevelCount <= 0)
            return nullptr;
        auto texture = std::make_unique<OpenGL4StorageTexture2DRenderer>(width, height,
                                                                         mipLevelCount, format);
        texture->AttachOwningContext(platformContext_);
        return texture;
    }

    CNA::RendererFormatSupport OpenGL4Renderer::GetSurfaceFormatUsageSupportEXT(
        const int surfaceFormat) const
    {
        using CNA::RendererFormatUsage;
        if (surfaceFormat < 0 || surfaceFormat >= static_cast<int>(formatUsageCache_.size()))
            return {};
        auto& cached = formatUsageCache_[static_cast<std::size_t>(surfaceFormat)];
        if (cached.has_value()) return *cached;

        // Asked of the driver, never assumed; where glGetInternalformativ is missing (a 4.1
        // context without GL_ARB_internalformat_query2) the answer stays unknown rather than a
        // guess. TextureStorage, RenderTarget and ColorTransfer are GraphicsDevice's own
        // classification of this renderer's verdicts and are not repeated here.
        CNA::RendererFormatSupport support;
        if (!modernCapabilities_.internalFormatQueriesNative || gl4_glGetInternalformativ == nullptr)
        {
            cached = support;
            return support;
        }
        const auto supported = [](const GLenum target, const GLenum internalFormat,
                                  const GLenum name) {
            GLint value = GL_NONE;
            gl4_glGetInternalformativ(target, internalFormat, name, 1, &value);
            return value == GL_FULL_SUPPORT || value == GL_CAVEAT_SUPPORT;
        };
        const auto mark = [&support](const RendererFormatUsage usage, const bool yes) {
            support.knownUsages |= static_cast<std::uint32_t>(usage);
            if (yes) support.supportedUsages |= static_cast<std::uint32_t>(usage);
        };

        // Sampling, filtering, transfers and mip chains: of the storage a Texture2D in this
        // format actually gets.
        const bool texture =
            ClassifySurfaceFormatEXT(surfaceFormat) == RendererFormatVerdict::Supported;
        Detail::TextureTransferFormat textureFormat;
        const bool textureMapped =
            texture && !Detail::IsDxtFormat(surfaceFormat) &&
            Detail::MapTextureTransferFormat(surfaceFormat, true, textureFormat);
        const GLenum sampledFormat = textureMapped ? textureFormat.internalFormat
            : (texture && Detail::IsDxtFormat(surfaceFormat) && Detail::ContextHasS3tc()
                   ? Detail::DxtInternalFormat(surfaceFormat)
                   : GL_RGBA8);
        mark(RendererFormatUsage::Sampled,
             texture && supported(GL_TEXTURE_2D, sampledFormat, GL_FRAGMENT_TEXTURE));
        mark(RendererFormatUsage::Filterable,
             texture && supported(GL_TEXTURE_2D, sampledFormat, GL_FILTER));
        mark(RendererFormatUsage::TransferSource, texture);
        mark(RendererFormatUsage::TransferDestination, texture);
        mark(RendererFormatUsage::Mipmapped, texture);

        // Blending and multisampling: of the storage a render target in this format gets.
        Detail::RenderTargetColorStorage target;
        const bool renderable =
            ClassifyRenderTargetFormatEXT(surfaceFormat) == RendererFormatVerdict::Supported &&
            Detail::MapRenderTargetColorFormat(surfaceFormat, target);
        mark(RendererFormatUsage::Blendable,
             renderable && supported(GL_TEXTURE_2D, target.internalFormat, GL_FRAMEBUFFER_BLEND));
        GLint maxSamples = 0;
        if (renderable)
            gl4_glGetInternalformativ(GL_RENDERBUFFER, target.internalFormat, GL_SAMPLES, 1,
                                      &maxSamples);
        mark(RendererFormatUsage::Multisample, renderable && maxSamples > 1);

        // Storage images: the StorageTexture2D mapping, and only where compute can bind one.
        OpenGL4StorageImageFormat image;
        const bool storage = SupportsComputeImageBindingEXT() &&
                             MapStorageImageFormat(surfaceFormat, image);
        mark(RendererFormatUsage::StorageRead,
             storage && supported(GL_TEXTURE_2D, image.internalFormat, GL_SHADER_IMAGE_LOAD));
        mark(RendererFormatUsage::StorageWrite,
             storage && supported(GL_TEXTURE_2D, image.internalFormat, GL_SHADER_IMAGE_STORE));
        // GLSL's image atomics operate on r32i/r32ui images (and exchange on r32f) only; none of
        // CNA's formats is one, whatever GL_SHADER_IMAGE_ATOMIC says of the storage itself --
        // Mesa answers "full support" for rgba8, where no imageAtomic* call can compile.
        mark(RendererFormatUsage::StorageAtomic, false);

        cached = support;
        return support;
    }

    bool OpenGL4Renderer::SupportsGpuTimerEXT() const
    {
        // Timestamp queries are core 3.3, but GL lets an implementation report a zero-bit counter
        // for GL_TIMESTAMP, which would make every difference zero. Asked once, not assumed.
        if (!modernCapabilities_.gpuTimersNative || gl4_glGetQueryiv == nullptr) return false;
        if (timestampCounterBits_ < 0)
        {
            GLint bits = 0;
            gl4_glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
            timestampCounterBits_ = bits;
        }
        return timestampCounterBits_ > 0;
    }

    std::unique_ptr<IGpuTimerRenderer> OpenGL4Renderer::CreateGpuTimerEXT()
    {
        EnsureCallingThreadContext();
        if (!SupportsGpuTimerEXT()) return nullptr;
        auto timer = std::make_unique<OpenGL4GpuTimerRenderer>();
        timer->AttachOwningContext(platformContext_);
        return timer;
    }

    std::uint64_t OpenGL4Renderer::GetTimestampPeriodPicosecondsEXT() const
    {
        // GL_TIMESTAMP counts nanoseconds by definition, whatever the hardware clock underneath.
        return SupportsGpuTimerEXT() ? UINT64_C(1000) : 0;
    }

    void OpenGL4Renderer::SetStringMarkerEXT(const char* marker)
    {
        if (marker == nullptr || gl4_glDebugMessageInsert == nullptr) return;
        EnsureCallingThreadContext();
        // A point in the command stream that capture tools (RenderDoc, apitrace) and the debug
        // callback both see. The public API has no scoped region, so none is opened here.
        gl4_glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 0,
                                 GL_DEBUG_SEVERITY_NOTIFICATION, -1, marker);
    }

    bool OpenGL4Renderer::SupportsIndirectDrawEXT() const
    {
        return modernCapabilities_.indirectDrawingNative;
    }

    bool OpenGL4Renderer::SupportsBaseInstanceDrawingEXT() const
    {
        return modernCapabilities_.baseInstanceDrawingNative;
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
