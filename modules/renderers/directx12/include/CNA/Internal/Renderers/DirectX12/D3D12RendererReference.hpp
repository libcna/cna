// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/ObjectDisposedException.hpp"

#include <memory>
#include <utility>

namespace CNA::Internal::Renderers::DirectX12
{
    class DirectX12Renderer;

    /**
     * @brief Non-owning renderer reference that detects destruction before dereferencing it.
     *
     * D3D12 resources own their native allocations independently, but command submission remains
     * renderer-owned. The weak token therefore prevents a detached resource from using a dangling
     * DirectX12Renderer pointer while allowing its destructor to release native allocations.
     */
    class D3D12RendererReference final
    {
    public:
        /**
         * @brief Creates a checked non-owning renderer reference.
         *
         * @param renderer Renderer pointer that is valid while lifetime is alive.
         * @param lifetime Weak renderer lifetime token.
         * @param objectName Resource name included in detachment exceptions.
         */
        D3D12RendererReference(DirectX12Renderer* renderer, std::weak_ptr<void> lifetime,
                               const char* objectName) noexcept
            : renderer_(renderer), lifetime_(std::move(lifetime)), objectName_(objectName)
        {
        }

        /**
         * @brief Reports whether the renderer can still be dereferenced.
         *
         * @return true while the renderer and its lifetime token remain valid.
         */
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return renderer_ != nullptr && !lifetime_.expired();
        }

        /**
         * @brief Returns the live renderer or throws a named disposal exception.
         *
         * @return The live renderer pointer.
         */
        [[nodiscard]] DirectX12Renderer* Get() const
        {
            if (!*this)
            {
                throw System::ObjectDisposedException(
                    objectName_, "The owning DirectX12Renderer has been disposed.");
            }
            return renderer_;
        }

        /**
         * @brief Dereferences the live renderer or throws a named disposal exception.
         *
         * @return The live renderer pointer.
         */
        [[nodiscard]] DirectX12Renderer* operator->() const
        {
            return Get();
        }

    private:
        DirectX12Renderer* renderer_ = nullptr;
        std::weak_ptr<void> lifetime_;
        const char* objectName_ = "D3D12Resource";
    };
}
