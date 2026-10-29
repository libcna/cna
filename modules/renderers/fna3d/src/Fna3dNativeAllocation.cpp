// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Fna3d/Fna3dNativeAllocation.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <new>

namespace CNA::Internal::Renderers::Fna3d
{
    namespace
    {
        constexpr std::size_t kFamilyCount = static_cast<std::size_t>(Fna3dNativeObjectEXT::Count);

        // Negative when disarmed; otherwise the allocation points still to pass on this thread.
        thread_local std::int64_t armedOrdinal = -1;

        std::array<std::atomic<std::int64_t>, kFamilyCount> liveCounts{};

        bool TakeInjectedFailure() noexcept
        {
            if (armedOrdinal < 0)
            {
                return false;
            }
            if (armedOrdinal == 0)
            {
                armedOrdinal = -1;
                return true;
            }
            --armedOrdinal;
            return false;
        }

        template <typename Handle>
        Handle* Counted(Fna3dNativeObjectEXT family, Handle* handle) noexcept
        {
            if (handle != nullptr)
            {
                liveCounts[static_cast<std::size_t>(family)].fetch_add(1,
                                                                       std::memory_order_relaxed);
            }
            return handle;
        }

        void Released(Fna3dNativeObjectEXT family) noexcept
        {
            liveCounts[static_cast<std::size_t>(family)].fetch_sub(1, std::memory_order_relaxed);
        }
    }

    void Fna3dAllocationFailureInjectionEXT::ArmEXT(std::uint32_t ordinal) noexcept
    {
        armedOrdinal = static_cast<std::int64_t>(ordinal);
    }

    void Fna3dAllocationFailureInjectionEXT::ClearEXT() noexcept
    {
        armedOrdinal = -1;
    }

    bool Fna3dAllocationFailureInjectionEXT::IsArmedEXT() noexcept
    {
        return armedOrdinal >= 0;
    }

    std::int64_t Fna3dAllocationFailureInjectionEXT::GetLiveCountEXT(
        Fna3dNativeObjectEXT family) noexcept
    {
        if (family >= Fna3dNativeObjectEXT::Count)
        {
            return 0;
        }
        return liveCounts[static_cast<std::size_t>(family)].load(std::memory_order_relaxed);
    }

    void Detail::WrapperAllocationPoint()
    {
        if (TakeInjectedFailure())
        {
            throw std::bad_alloc();
        }
    }

    FNA3D_Device* Detail::CreateDevice(FNA3D_PresentationParameters* presentation,
                                       std::uint8_t debugMode)
    {
        if (TakeInjectedFailure())
        {
            return nullptr;
        }
        return Counted(Fna3dNativeObjectEXT::Device, FNA3D_CreateDevice(presentation, debugMode));
    }

    void Detail::DestroyDevice(FNA3D_Device* device)
    {
        if (device == nullptr)
        {
            return;
        }
        FNA3D_DestroyDevice(device);
        Released(Fna3dNativeObjectEXT::Device);
    }

    void Detail::CreateEffect(FNA3D_Device* device, std::uint8_t* code, std::uint32_t length,
                              FNA3D_Effect** effect, MOJOSHADER_effect** effectData)
    {
        *effect = nullptr;
        *effectData = nullptr;
        if (TakeInjectedFailure())
        {
            return;
        }
        FNA3D_CreateEffect(device, code, length, effect, effectData);
        (void) Counted(Fna3dNativeObjectEXT::Effect, *effect);
    }

    void Detail::CloneEffect(FNA3D_Device* device, FNA3D_Effect* source, FNA3D_Effect** effect,
                             MOJOSHADER_effect** effectData)
    {
        *effect = nullptr;
        *effectData = nullptr;
        if (TakeInjectedFailure())
        {
            return;
        }
        FNA3D_CloneEffect(device, source, effect, effectData);
        (void) Counted(Fna3dNativeObjectEXT::Effect, *effect);
    }

    void Detail::DisposeEffect(FNA3D_Device* device, FNA3D_Effect* effect)
    {
        if (effect == nullptr)
        {
            return;
        }
        FNA3D_AddDisposeEffect(device, effect);
        Released(Fna3dNativeObjectEXT::Effect);
    }

    void Detail::FreeSentinelEffect(FNA3D_Effect* effect, void (*freeWrapper)(void*))
    {
        if (effect == nullptr)
        {
            return;
        }
        freeWrapper(effect);
        Released(Fna3dNativeObjectEXT::Effect);
    }

    FNA3D_Texture* Detail::CreateTexture2D(FNA3D_Device* device, FNA3D_SurfaceFormat format,
                                           std::int32_t width, std::int32_t height,
                                           std::int32_t levelCount, std::uint8_t isRenderTarget)
    {
        if (TakeInjectedFailure())
        {
            return nullptr;
        }
        return Counted(Fna3dNativeObjectEXT::Texture,
                       FNA3D_CreateTexture2D(device, format, width, height, levelCount,
                                             isRenderTarget));
    }

    FNA3D_Texture* Detail::CreateTexture3D(FNA3D_Device* device, FNA3D_SurfaceFormat format,
                                           std::int32_t width, std::int32_t height,
                                           std::int32_t depth, std::int32_t levelCount)
    {
        if (TakeInjectedFailure())
        {
            return nullptr;
        }
        return Counted(Fna3dNativeObjectEXT::Texture,
                       FNA3D_CreateTexture3D(device, format, width, height, depth, levelCount));
    }

    FNA3D_Texture* Detail::CreateTextureCube(FNA3D_Device* device, FNA3D_SurfaceFormat format,
                                             std::int32_t size, std::int32_t levelCount,
                                             std::uint8_t isRenderTarget)
    {
        if (TakeInjectedFailure())
        {
            return nullptr;
        }
        return Counted(Fna3dNativeObjectEXT::Texture,
                       FNA3D_CreateTextureCube(device, format, size, levelCount, isRenderTarget));
    }

    void Detail::DisposeTexture(FNA3D_Device* device, FNA3D_Texture* texture)
    {
        if (texture == nullptr)
        {
            return;
        }
        FNA3D_AddDisposeTexture(device, texture);
        Released(Fna3dNativeObjectEXT::Texture);
    }

    FNA3D_Renderbuffer* Detail::GenColorRenderbuffer(FNA3D_Device* device, std::int32_t width,
                                                     std::int32_t height,
                                                     FNA3D_SurfaceFormat format,
                                                     std::int32_t multiSampleCount,
                                                     FNA3D_Texture* texture)
    {
        if (TakeInjectedFailure())
        {
            return nullptr;
        }
        return Counted(Fna3dNativeObjectEXT::Renderbuffer,
                       FNA3D_GenColorRenderbuffer(device, width, height, format, multiSampleCount,
                                                  texture));
    }

    FNA3D_Renderbuffer* Detail::GenDepthStencilRenderbuffer(FNA3D_Device* device,
                                                            std::int32_t width,
                                                            std::int32_t height,
                                                            FNA3D_DepthFormat format,
                                                            std::int32_t multiSampleCount)
    {
        if (TakeInjectedFailure())
        {
            return nullptr;
        }
        return Counted(Fna3dNativeObjectEXT::Renderbuffer,
                       FNA3D_GenDepthStencilRenderbuffer(device, width, height, format,
                                                         multiSampleCount));
    }

    void Detail::DisposeRenderbuffer(FNA3D_Device* device, FNA3D_Renderbuffer* renderbuffer)
    {
        if (renderbuffer == nullptr)
        {
            return;
        }
        FNA3D_AddDisposeRenderbuffer(device, renderbuffer);
        Released(Fna3dNativeObjectEXT::Renderbuffer);
    }

    FNA3D_Buffer* Detail::GenVertexBuffer(FNA3D_Device* device, std::uint8_t dynamic,
                                          FNA3D_BufferUsage usage, std::int32_t sizeInBytes)
    {
        if (TakeInjectedFailure())
        {
            return nullptr;
        }
        return Counted(Fna3dNativeObjectEXT::VertexBuffer,
                       FNA3D_GenVertexBuffer(device, dynamic, usage, sizeInBytes));
    }

    void Detail::DisposeVertexBuffer(FNA3D_Device* device, FNA3D_Buffer* buffer)
    {
        if (buffer == nullptr)
        {
            return;
        }
        FNA3D_AddDisposeVertexBuffer(device, buffer);
        Released(Fna3dNativeObjectEXT::VertexBuffer);
    }

    FNA3D_Buffer* Detail::GenIndexBuffer(FNA3D_Device* device, std::uint8_t dynamic,
                                         FNA3D_BufferUsage usage, std::int32_t sizeInBytes)
    {
        if (TakeInjectedFailure())
        {
            return nullptr;
        }
        return Counted(Fna3dNativeObjectEXT::IndexBuffer,
                       FNA3D_GenIndexBuffer(device, dynamic, usage, sizeInBytes));
    }

    void Detail::DisposeIndexBuffer(FNA3D_Device* device, FNA3D_Buffer* buffer)
    {
        if (buffer == nullptr)
        {
            return;
        }
        FNA3D_AddDisposeIndexBuffer(device, buffer);
        Released(Fna3dNativeObjectEXT::IndexBuffer);
    }

    FNA3D_Query* Detail::CreateQuery(FNA3D_Device* device)
    {
        if (TakeInjectedFailure())
        {
            return nullptr;
        }
        return Counted(Fna3dNativeObjectEXT::Query, FNA3D_CreateQuery(device));
    }

    void Detail::DisposeQuery(FNA3D_Device* device, FNA3D_Query* query)
    {
        if (query == nullptr)
        {
            return;
        }
        FNA3D_AddDisposeQuery(device, query);
        Released(Fna3dNativeObjectEXT::Query);
    }
}
