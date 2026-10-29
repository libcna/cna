// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file
 * @brief Every native FNA3D object the renderer creates or releases, routed through one place.
 *
 * The routing exists for one reason: a native creation that fails is otherwise impossible to
 * produce on demand. FNA3D's drivers fail only when the GPU or the driver runs out of something,
 * so every rollback path behind a null handle would stay untested. Here each creation, and each
 * C++ wrapper allocation that takes ownership of an already created native object, is one
 * allocation point a test can make fail, and every creation and release is counted per object
 * family, so a test can prove that a failed operation leaves nothing behind.
 */

#include "CNA/Internal/Renderers/Fna3d/Fna3dApi.hpp"

#include <cstdint>

namespace CNA::Internal::Renderers::Fna3d
{
    /** @brief CNAEXT. The native FNA3D object families this renderer creates and releases. */
    enum class Fna3dNativeObjectEXT : std::uint8_t
    {
        /** @brief `FNA3D_Device`. */
        Device,
        /** @brief `FNA3D_Effect`, stock and compiled. */
        Effect,
        /** @brief `FNA3D_Texture`: 2D, 3D and cube, render targets included. */
        Texture,
        /** @brief `FNA3D_Renderbuffer`: MSAA colour and depth/stencil. */
        Renderbuffer,
        /** @brief `FNA3D_Buffer` made by `FNA3D_GenVertexBuffer`. */
        VertexBuffer,
        /** @brief `FNA3D_Buffer` made by `FNA3D_GenIndexBuffer`. */
        IndexBuffer,
        /** @brief `FNA3D_Query`. */
        Query,
        /** @brief Number of families; not a family. */
        Count
    };

    /**
     * @brief CNAEXT. Test-only control of deterministic native-allocation failure.
     *
     * Arming with an ordinal makes the allocation point that many points ahead on the calling
     * thread fail the way the real one does: a native creation returns null without reaching
     * FNA3D, and a wrapper allocation throws `std::bad_alloc`. The injection is thread-local and
     * one-shot, so it cannot reach another thread or any later allocation. A test that arms
     * ordinals 0, 1, 2, ... in turn walks every failure point an operation has, without having to
     * know them; the first ordinal the operation leaves armed is past its last point.
     *
     * The live counts are process-wide: the creations that succeeded minus the releases handed back
     * to FNA3D, per family. Resources still owned by a wrapper whose device is gone are no longer
     * released by CNA and stay counted.
     */
    class Fna3dAllocationFailureInjectionEXT
    {
    public:
        Fna3dAllocationFailureInjectionEXT() = delete;

        /**
         * @brief Arms the injection on the calling thread.
         *
         * @param ordinal Zero-based number of allocation points to pass before one fails.
         */
        static void ArmEXT(std::uint32_t ordinal) noexcept;

        /** @brief Disarms the injection on the calling thread. */
        static void ClearEXT() noexcept;

        /**
         * @brief Whether the calling thread's injection is still waiting for its point.
         *
         * @return True until the armed allocation point has failed or the injection is cleared.
         */
        [[nodiscard]] static bool IsArmedEXT() noexcept;

        /**
         * @brief Native objects of one family currently created and not yet released.
         *
         * @param family The object family to count.
         * @return Successful creations minus releases, across every thread.
         */
        [[nodiscard]] static std::int64_t GetLiveCountEXT(Fna3dNativeObjectEXT family) noexcept;
    };

    namespace Detail
    {
        /**
         * @brief Allocation point before a C++ wrapper takes ownership of a native object.
         *
         * Throws `std::bad_alloc` when it is the armed point, which is how a failed wrapper
         * allocation reaches the caller; the caller still owns the native object and must release
         * it.
         */
        void WrapperAllocationPoint();

        /**
         * @brief `FNA3D_CreateDevice` behind an allocation point.
         *
         * @param presentation Presentation parameters for the new device.
         * @param debugMode    FNA3D's debug-mode flag.
         * @return The device, or null when FNA3D or the injection fails it.
         */
        [[nodiscard]] FNA3D_Device* CreateDevice(FNA3D_PresentationParameters* presentation,
                                                 std::uint8_t debugMode);

        /**
         * @brief `FNA3D_DestroyDevice`, counted.
         * @param device Device to destroy; null is ignored.
         */
        void DestroyDevice(FNA3D_Device* device);

        /**
         * @brief `FNA3D_CreateEffect` behind an allocation point.
         *
         * @param device     Owning device.
         * @param code       Effect Framework bytecode.
         * @param length     Bytecode length in bytes.
         * @param effect     Receives the native effect, or null.
         * @param effectData Receives the MojoShader parse result, or null.
         */
        void CreateEffect(FNA3D_Device* device, std::uint8_t* code, std::uint32_t length,
                          FNA3D_Effect** effect, MOJOSHADER_effect** effectData);

        /**
         * @brief `FNA3D_CloneEffect` behind an allocation point.
         *
         * @param device     Owning device.
         * @param source     Effect to copy.
         * @param effect     Receives the native copy, or null.
         * @param effectData Receives the copy's MojoShader data, or null.
         */
        void CloneEffect(FNA3D_Device* device, FNA3D_Effect* source, FNA3D_Effect** effect,
                         MOJOSHADER_effect** effectData);

        /**
         * @brief `FNA3D_AddDisposeEffect`, counted.
         * @param device Owning device.
         * @param effect Effect to release; null is ignored.
         */
        void DisposeEffect(FNA3D_Device* device, FNA3D_Effect* effect);

        /**
         * @brief Frees only the driver wrapper of an effect parsed into a static sentinel.
         * @param effect      Effect wrapper to free; null is ignored.
         * @param freeWrapper The allocator's free function the driver allocated it with.
         */
        void FreeSentinelEffect(FNA3D_Effect* effect, void (*freeWrapper)(void*));

        /**
         * @brief `FNA3D_CreateTexture2D` behind an allocation point.
         *
         * @param device         Owning device.
         * @param format         Storage format.
         * @param width          Width in texels.
         * @param height         Height in texels.
         * @param levelCount     Mip level count.
         * @param isRenderTarget Whether the texture is a render-target attachment.
         * @return The texture, or null.
         */
        [[nodiscard]] FNA3D_Texture* CreateTexture2D(FNA3D_Device* device,
                                                     FNA3D_SurfaceFormat format, std::int32_t width,
                                                     std::int32_t height, std::int32_t levelCount,
                                                     std::uint8_t isRenderTarget);

        /**
         * @brief `FNA3D_CreateTexture3D` behind an allocation point.
         *
         * @param device     Owning device.
         * @param format     Storage format.
         * @param width      Width in texels.
         * @param height     Height in texels.
         * @param depth      Depth in texels.
         * @param levelCount Mip level count.
         * @return The texture, or null.
         */
        [[nodiscard]] FNA3D_Texture* CreateTexture3D(FNA3D_Device* device,
                                                     FNA3D_SurfaceFormat format, std::int32_t width,
                                                     std::int32_t height, std::int32_t depth,
                                                     std::int32_t levelCount);

        /**
         * @brief `FNA3D_CreateTextureCube` behind an allocation point.
         *
         * @param device         Owning device.
         * @param format         Storage format.
         * @param size           Edge length in texels.
         * @param levelCount     Mip level count.
         * @param isRenderTarget Whether the texture is a render-target attachment.
         * @return The texture, or null.
         */
        [[nodiscard]] FNA3D_Texture* CreateTextureCube(FNA3D_Device* device,
                                                       FNA3D_SurfaceFormat format,
                                                       std::int32_t size, std::int32_t levelCount,
                                                       std::uint8_t isRenderTarget);

        /**
         * @brief `FNA3D_AddDisposeTexture`, counted.
         * @param device  Owning device.
         * @param texture Texture to release; null is ignored.
         */
        void DisposeTexture(FNA3D_Device* device, FNA3D_Texture* texture);

        /**
         * @brief `FNA3D_GenColorRenderbuffer` behind an allocation point.
         *
         * @param device           Owning device.
         * @param width            Width in pixels.
         * @param height           Height in pixels.
         * @param format           Storage format.
         * @param multiSampleCount Sample count.
         * @param texture          Resolve texture.
         * @return The renderbuffer, or null.
         */
        [[nodiscard]] FNA3D_Renderbuffer* GenColorRenderbuffer(
            FNA3D_Device* device, std::int32_t width, std::int32_t height,
            FNA3D_SurfaceFormat format, std::int32_t multiSampleCount, FNA3D_Texture* texture);

        /**
         * @brief `FNA3D_GenDepthStencilRenderbuffer` behind an allocation point.
         *
         * @param device           Owning device.
         * @param width            Width in pixels.
         * @param height           Height in pixels.
         * @param format           Depth/stencil format.
         * @param multiSampleCount Sample count.
         * @return The renderbuffer, or null.
         */
        [[nodiscard]] FNA3D_Renderbuffer* GenDepthStencilRenderbuffer(
            FNA3D_Device* device, std::int32_t width, std::int32_t height,
            FNA3D_DepthFormat format, std::int32_t multiSampleCount);

        /**
         * @brief `FNA3D_AddDisposeRenderbuffer`, counted.
         * @param device       Owning device.
         * @param renderbuffer Renderbuffer to release; null is ignored.
         */
        void DisposeRenderbuffer(FNA3D_Device* device, FNA3D_Renderbuffer* renderbuffer);

        /**
         * @brief `FNA3D_GenVertexBuffer` behind an allocation point.
         *
         * @param device      Owning device.
         * @param dynamic     FNA3D's dynamic-buffer flag.
         * @param usage       Buffer usage.
         * @param sizeInBytes Capacity in bytes.
         * @return The buffer, or null.
         */
        [[nodiscard]] FNA3D_Buffer* GenVertexBuffer(FNA3D_Device* device, std::uint8_t dynamic,
                                                    FNA3D_BufferUsage usage,
                                                    std::int32_t sizeInBytes);

        /**
         * @brief `FNA3D_AddDisposeVertexBuffer`, counted.
         * @param device Owning device.
         * @param buffer Buffer to release; null is ignored.
         */
        void DisposeVertexBuffer(FNA3D_Device* device, FNA3D_Buffer* buffer);

        /**
         * @brief `FNA3D_GenIndexBuffer` behind an allocation point.
         *
         * @param device      Owning device.
         * @param dynamic     FNA3D's dynamic-buffer flag.
         * @param usage       Buffer usage.
         * @param sizeInBytes Capacity in bytes.
         * @return The buffer, or null.
         */
        [[nodiscard]] FNA3D_Buffer* GenIndexBuffer(FNA3D_Device* device, std::uint8_t dynamic,
                                                   FNA3D_BufferUsage usage,
                                                   std::int32_t sizeInBytes);

        /**
         * @brief `FNA3D_AddDisposeIndexBuffer`, counted.
         * @param device Owning device.
         * @param buffer Buffer to release; null is ignored.
         */
        void DisposeIndexBuffer(FNA3D_Device* device, FNA3D_Buffer* buffer);

        /**
         * @brief `FNA3D_CreateQuery` behind an allocation point.
         * @param device Owning device.
         * @return The query, or null.
         */
        [[nodiscard]] FNA3D_Query* CreateQuery(FNA3D_Device* device);

        /**
         * @brief `FNA3D_AddDisposeQuery`, counted.
         * @param device Owning device.
         * @param query  Query to release; null is ignored.
         */
        void DisposeQuery(FNA3D_Device* device, FNA3D_Query* query);
    }
}
