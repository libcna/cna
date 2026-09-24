// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_opengl4_modern_graphics.md Workstream B: the modern CNA/CNAEXT GPU surface over desktop
// OpenGL 4.3+ -- storage and constant buffers, compute programs, storage textures and GPU timers.
// Every class here
// exists only when GL4::DiscoverModernCapabilities found the native feature in the live context;
// the renderer's Supports*EXT answers are what promise it.

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/OpenGL4/GL4Loader.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Common.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::OpenGL4
{
    class OpenGL4Renderer;

    /**
     * @brief One GL buffer object behind a `CNA::Graphics::StorageBuffer` (GL4-0025).
     *
     * GL buffer objects are target-agnostic, so the same name serves as a shader-storage block, a
     * uniform block, an indirect-argument source or a copy endpoint. Transfers go through the copy
     * binding points, which no draw path reads, and restore whatever was bound there.
     */
    class OpenGL4StorageBufferRenderer final : public IStorageBufferRenderer,
                                               public OpenGL4ContextResource
    {
    public:
        /**
         * @brief Allocates @p byteSize bytes of uninitialised storage.
         *
         * @param byteSize Allocation size; must be positive.
         * @param usage Raw `CNA::Graphics::StorageBufferUsage` bits.
         * @param cpuAccess Raw `CNA::Graphics::StorageBufferCpuAccess` bits.
         * @throws std::invalid_argument for a zero size or a mask outside the declared bits.
         */
        OpenGL4StorageBufferRenderer(std::size_t byteSize, std::uint32_t usage,
                                     std::uint32_t cpuAccess);
        /** @brief Deletes the buffer in its own context; issues no GL if that context is gone. */
        ~OpenGL4StorageBufferRenderer() override;

        OpenGL4StorageBufferRenderer(const OpenGL4StorageBufferRenderer&) = delete;
        OpenGL4StorageBufferRenderer& operator=(const OpenGL4StorageBufferRenderer&) = delete;

        /**
         * @brief Uploads @p byteSize bytes at offset zero.
         *
         * @param data Source bytes.
         * @param byteSize Bytes to upload.
         */
        void SetData(const void* data, std::size_t byteSize) override;
        /**
         * @brief Reads @p byteSize bytes from offset zero.
         *
         * @param out Destination bytes.
         * @param byteSize Bytes to read.
         */
        void GetData(void* out, std::size_t byteSize) const override;
        /**
         * @brief Uploads a byte range.
         *
         * @param byteOffset First byte written.
         * @param data Source bytes.
         * @param byteSize Bytes to upload.
         * @return True; a range outside the allocation throws instead.
         * @throws std::invalid_argument for a range outside the allocation or a null source.
         */
        bool SetDataRangeEXT(std::size_t byteOffset, const void* data,
                             std::size_t byteSize) override;
        /**
         * @brief Reads a byte range back, after every GPU write issued before it.
         *
         * @param byteOffset First byte read.
         * @param out Destination bytes.
         * @param byteSize Bytes to read.
         * @return True, or false when the owning context no longer exists.
         * @throws std::invalid_argument for a range outside the allocation or a null destination.
         */
        bool GetDataRangeEXT(std::size_t byteOffset, void* out,
                             std::size_t byteSize) const override;
        /**
         * @brief Copies a byte range into another OpenGL4 buffer on the GPU.
         *
         * @param destination The destination buffer; must belong to this renderer family.
         * @param sourceByteOffset First source byte.
         * @param destinationByteOffset First destination byte.
         * @param byteSize Bytes to copy.
         * @return False for a destination of another renderer family.
         * @throws std::invalid_argument for a range outside either allocation or overlapping
         *         ranges within one buffer.
         */
        bool CopyToEXT(IStorageBufferRenderer& destination, std::size_t sourceByteOffset,
                       std::size_t destinationByteOffset, std::size_t byteSize) override;
        /** @brief Returns the allocation size in bytes. */
        [[nodiscard]] std::size_t GetByteSize() const override { return byteSize_; }
        /** @brief Returns the declared `StorageBufferUsage` bits. */
        [[nodiscard]] std::uint32_t GetUsageEXT() const override { return usage_; }
        /** @brief Returns the declared `StorageBufferCpuAccess` bits. */
        [[nodiscard]] std::uint32_t GetCpuAccessEXT() const override { return cpuAccess_; }

        /** @brief Returns the GL buffer name. */
        CNAEXT [[nodiscard]] unsigned int GLHandle() const noexcept { return buffer_; }

    private:
        unsigned int buffer_ = 0;
        std::size_t byteSize_ = 0;
        std::uint32_t usage_ = 0;
        std::uint32_t cpuAccess_ = 0;
    };

    /**
     * @brief One linked GL compute program and the resources a dispatch of it reads (GL4-0025).
     *
     * Nothing here touches GL binding state when it is called. Uniforms go to the program object
     * directly (`glProgramUniform*`), and buffer, texture and image bindings are recorded and held
     * alive until the dispatch, which installs them, runs, and puts every binding it changed back.
     * A compute pass interleaved with drawing therefore cannot leave a draw reading its textures,
     * its storage blocks or its program -- XNA state set before the dispatch is the state after it.
     */
    class OpenGL4ComputeShaderRenderer final : public IComputeShaderRenderer,
                                               public OpenGL4ContextResource
    {
    public:
        OpenGL4ComputeShaderRenderer() = default;
        /** @brief Deletes the program in its own context; issues no GL if that context is gone. */
        ~OpenGL4ComputeShaderRenderer() override;

        OpenGL4ComputeShaderRenderer(const OpenGL4ComputeShaderRenderer&) = delete;
        OpenGL4ComputeShaderRenderer& operator=(const OpenGL4ComputeShaderRenderer&) = delete;

        /**
         * @brief Compiles and links a desktop GLSL compute shader.
         *
         * @param computeSrc The shader source, exactly as the driver receives it.
         * @return True when the program linked; otherwise GetCompileError() holds the logs.
         */
        bool CompileProgram(const std::string& computeSrc) override;
        /** @brief Does nothing: the program is installed by the dispatch, never left bound. */
        void Bind() override {}
        /**
         * @brief Sets an `int` uniform on the program object.
         *
         * @param name Uniform name; an inactive or unknown name is ignored.
         * @param value The value.
         */
        void SetUniformInt(const char* name, int value) override;
        /**
         * @brief Sets a `float` uniform on the program object.
         *
         * @param name Uniform name; an inactive or unknown name is ignored.
         * @param value The value.
         */
        void SetUniformFloat(const char* name, float value) override;
        /**
         * @brief Records a shader-storage block binding for the next dispatches.
         *
         * @param binding Storage-block binding point.
         * @param buffer The buffer, or null to clear the binding.
         */
        void BindStorageBuffer(int binding, IStorageBufferRenderer* buffer) override;
        /**
         * @brief Records a uniform-block binding for the next dispatches.
         *
         * @param binding Uniform-block binding point.
         * @param buffer A buffer declared with Constant usage, or null to clear the binding.
         * @return False for a binding beyond the context's compute uniform-block limit or a buffer
         *         without Constant usage.
         */
        [[nodiscard]] bool BindConstantBufferEXT(int binding,
                                                 IStorageBufferRenderer* buffer) override;
        /**
         * @brief Records a Texture2D (or render target) as an image for the next dispatches.
         *
         * @param unit Image unit.
         * @param texture The texture; its own GL storage format is the image format.
         * @param accessMode `CNA::GraphicsImageAccess` ordinal.
         * @throws System::NotSupportedException when the texture's storage has no image format.
         */
        void BindImageTexture(int unit, ITextureRenderer* texture, int accessMode) override;
        /**
         * @brief Records a StorageTexture2D as an image for the next dispatches.
         *
         * @param unit Image unit.
         * @param texture The storage texture, or null to clear the unit.
         * @param accessMode `CNA::GraphicsImageAccess` ordinal.
         * @return False for a unit beyond the compute image limit or a texture of another
         *         renderer family.
         */
        [[nodiscard]] bool BindStorageTexture2DEXT(
            int unit, std::shared_ptr<IStorageTexture2DRenderer> texture,
            int accessMode) override;
        /**
         * @brief Records a sampled Texture2D (or render target) for the next dispatches.
         *
         * @param unit Texture unit; the sampler uniform is pointed at it by the caller.
         * @param texture The texture.
         */
        void BindTexture(int unit, ITextureRenderer* texture) override;
        /** @brief Returns whether the program linked. */
        [[nodiscard]] bool IsValid() const override { return program_ != 0 && valid_; }
        /** @brief Returns the compiler and linker logs of a failed build. */
        [[nodiscard]] std::string GetCompileError() const override { return compileError_; }
        /** @brief Returns the GL program name (0 when the build failed). */
        CNAEXT [[nodiscard]] unsigned int GLProgram() const noexcept { return program_; }

        /**
         * @brief Installs the recorded bindings, dispatches, orders the writes, and restores.
         *
         * Called only by OpenGL4Renderer::DispatchCompute, inside this program's context.
         *
         * @param groupsX Work groups along X.
         * @param groupsY Work groups along Y.
         * @param groupsZ Work groups along Z.
         * @param sampler The renderer's nearest/clamp sampler object for sampled inputs.
         */
        CNAEXT void DispatchEXT(unsigned int groupsX, unsigned int groupsY, unsigned int groupsZ,
                                unsigned int sampler);

    private:
        struct BufferBinding
        {
            int binding = 0;
            std::shared_ptr<const IStorageBufferRenderer> buffer;
            unsigned int name = 0;
        };
        struct TextureBinding
        {
            int unit = 0;
            std::shared_ptr<const ITextureRenderer> texture;
            unsigned int name = 0;
        };
        struct ImageBinding
        {
            int unit = 0;
            std::shared_ptr<const void> keepAlive;
            unsigned int name = 0;
            GLenum access = 0;
            GLenum format = 0;
        };

        [[nodiscard]] int UniformLocation(const char* name) const;
        static void Record(std::vector<BufferBinding>& bindings, int binding,
                           const IStorageBufferRenderer* buffer);

        unsigned int program_ = 0;
        bool valid_ = false;
        std::string compileError_;
        std::vector<BufferBinding> storageBuffers_;
        std::vector<BufferBinding> constantBuffers_;
        std::vector<TextureBinding> textures_;
        std::vector<ImageBinding> images_;
    };

    /**
     * @brief GL storage and exact transfer layout of one storage-image SurfaceFormat (GL4-0030).
     */
    struct OpenGL4StorageImageFormat
    {
        /** @brief Sized internal format; always one of GL 4.3's image-unit formats. */
        GLenum internalFormat = 0;
        /** @brief Transfer pixel format. */
        GLenum pixelFormat = 0;
        /** @brief Transfer pixel type. */
        GLenum pixelType = 0;
        /** @brief Bytes of one texel in the declared (public) layout, which is also GL's. */
        int bytesPerTexel = 0;
    };

    /**
     * @brief Maps a SurfaceFormat to its storage-image format.
     *
     * The fifteen formats whose declared bytes are exactly one GL 4.3 image-unit format, so an
     * upload, a compute write and a readback all see the same bytes: Color, NormalizedByte2/4,
     * Rgba1010102, Rg32, Rgba64, Single, Vector2, Vector4, HalfSingle, HalfVector2,
     * HalfVector4, HdrBlendable, ByteEXT and UShortEXT -- Vulkan's set.
     *
     * @param surfaceFormat SurfaceFormat ordinal.
     * @param out Receives the mapping.
     * @return False for every other format.
     */
    [[nodiscard]] bool MapStorageImageFormat(int surfaceFormat, OpenGL4StorageImageFormat& out);

    /**
     * @brief One `CNA::Graphics::StorageTexture2D`: a mutable GL 2D texture with every declared
     *        level allocated, read and written by compute as an image (GL4-0030).
     *
     * Transfers are the declared format's exact bytes, tightly packed. A readback asks GL for the
     * whole level (`glGetTexImage`; the sub-image query is 4.5) and returns the rectangle.
     */
    class OpenGL4StorageTexture2DRenderer final : public IStorageTexture2DRenderer,
                                                  public OpenGL4ContextResource
    {
    public:
        /**
         * @brief Allocates the texture and every level of its chain.
         *
         * @param width Level-0 width.
         * @param height Level-0 height.
         * @param mipLevelCount Levels to allocate.
         * @param format The storage-image mapping of the SurfaceFormat.
         */
        OpenGL4StorageTexture2DRenderer(int width, int height, int mipLevelCount,
                                        const OpenGL4StorageImageFormat& format);
        /** @brief Deletes the texture in its own context; issues no GL if that context is gone. */
        ~OpenGL4StorageTexture2DRenderer() override;

        OpenGL4StorageTexture2DRenderer(const OpenGL4StorageTexture2DRenderer&) = delete;
        OpenGL4StorageTexture2DRenderer& operator=(const OpenGL4StorageTexture2DRenderer&) = delete;

        /**
         * @brief Uploads a rectangle of one level.
         *
         * @param mipLevel Level.
         * @param x Left texel.
         * @param y Top texel.
         * @param width Rectangle width.
         * @param height Rectangle height.
         * @param data Tightly packed texels in the declared format.
         * @param byteCount Bytes at @p data; must be exactly the rectangle's.
         * @return False when the rectangle, the byte count or the context is not valid.
         */
        [[nodiscard]] bool SetData(int mipLevel, int x, int y, int width, int height,
                                   const void* data, std::size_t byteCount) override;
        /**
         * @brief Reads a rectangle of one level back, after every write issued before it.
         *
         * @param mipLevel Level.
         * @param x Left texel.
         * @param y Top texel.
         * @param width Rectangle width.
         * @param height Rectangle height.
         * @param data Receives tightly packed texels in the declared format.
         * @param byteCount Bytes at @p data; must be exactly the rectangle's.
         * @return False when the rectangle, the byte count or the context is not valid.
         */
        [[nodiscard]] bool GetData(int mipLevel, int x, int y, int width, int height,
                                   void* data, std::size_t byteCount) const override;

        /** @brief Returns the GL texture name. */
        CNAEXT [[nodiscard]] unsigned int GLHandle() const noexcept { return texture_; }
        /** @brief Returns the sized internal format, which is the image-unit format. */
        CNAEXT [[nodiscard]] GLenum InternalFormat() const noexcept { return format_.internalFormat; }

    private:
        [[nodiscard]] bool ValidRegion(int mipLevel, int x, int y, int width, int height,
                                       std::size_t byteCount, int& levelWidth,
                                       int& levelHeight) const;

        unsigned int texture_ = 0;
        int width_ = 0;
        int height_ = 0;
        int levels_ = 1;
        OpenGL4StorageImageFormat format_;
    };

    /**
     * @brief A GPU timer made of two `GL_TIMESTAMP` queries (GL4-0027).
     *
     * Timestamps rather than one `GL_TIME_ELAPSED` query: only one elapsed-time query may be
     * active at a time in a GL context, so two overlapping or nested CNA timers would collide,
     * while any number of timestamp pairs can interleave. The difference of two 64-bit
     * nanosecond counters does not saturate. Polling never blocks: availability is asked before a
     * result is read.
     */
    class OpenGL4GpuTimerRenderer final : public IGpuTimerRenderer, public OpenGL4ContextResource
    {
    public:
        OpenGL4GpuTimerRenderer();
        /** @brief Deletes the queries in their own context; issues no GL if that context is gone. */
        ~OpenGL4GpuTimerRenderer() override;

        OpenGL4GpuTimerRenderer(const OpenGL4GpuTimerRenderer&) = delete;
        OpenGL4GpuTimerRenderer& operator=(const OpenGL4GpuTimerRenderer&) = delete;

        /** @brief Records the opening timestamp; ignored while a range is open. */
        void Begin() override;
        /** @brief Records the closing timestamp; ignored when no range is open. */
        void End() override;
        /** @brief Returns whether a closed range's timestamps have arrived, without waiting. */
        [[nodiscard]] bool IsResultAvailable() const override;
        /** @brief Returns the nanoseconds between the two timestamps, or 0 before they arrive. */
        [[nodiscard]] std::uint64_t ElapsedNanoseconds() const override;
        /**
         * @brief Returns one of the two timestamp query names.
         *
         * @param index 0 for the opening timestamp, 1 for the closing one.
         * @return The GL query name.
         */
        CNAEXT [[nodiscard]] unsigned int GLQuery(int index) const noexcept
        {
            return queries_[index == 0 ? 0 : 1];
        }

    private:
        unsigned int queries_[2] = {0, 0};
        bool open_ = false;
        bool closed_ = false;
        mutable bool cached_ = false;
        mutable std::uint64_t nanoseconds_ = 0;
    };
}
