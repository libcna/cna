// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Internal::Renderers { class RenderTargetBindingDescriptor; }
namespace Microsoft::Xna::Framework::Graphics { class RenderTargetBinding; }

namespace CNA::Internal::Graphics
{
    /**
     * @brief Implemented by the textures a GraphicsDevice can bind as render targets.
     *
     * The device asks a bound texture through this interface instead of testing for
     * RenderTarget2D and RenderTargetCube itself. Link boundary: naming RenderTargetCube in the
     * device linked the cube-map classes into every program (docs/link-size-architecture.md).
     */
    class IRenderTargetBindingSource
    {
    public:
        virtual ~IRenderTargetBindingSource() = default;

        /**
         * @brief Describes binding this texture as @p binding asks, for the renderer.
         *
         * @param binding The binding being made: its cube face or array slice.
         * @return The descriptor GraphicsDevice::SetRenderTargets() hands the renderer.
         * @throws System::NotSupportedException when the renderer cannot bind this kind of target
         *         or the slice is not 0; System::ArgumentOutOfRangeException for an invalid face.
         */
        [[nodiscard]] virtual Renderers::RenderTargetBindingDescriptor DescribeRenderTargetBindingEXT(
            const Microsoft::Xna::Framework::Graphics::RenderTargetBinding& binding) = 0;

        /**
         * @brief Whether the renderer gave this target a real depth and a real stencil buffer.
         *
         * @param depth Set to whether its depth-stencil format has real depth storage.
         * @param stencil Set to whether it has real stencil storage.
         */
        virtual void QueryRealDepthStencilEXT(bool& depth, bool& stencil) const = 0;
    };
}
