// SPDX-License-Identifier: MS-PL
// GraphicsDevice::SetRenderTarget(RenderTargetCube*, CubeMapFace).
//
// Link boundary: the one GraphicsDevice member that names RenderTargetCube, apart from the device
// every program links, so that only a program binding a cube map links the cube-map classes.
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    void GraphicsDevice::SetRenderTarget(RenderTargetCube* renderTarget, CubeMapFace cubeMapFace)
    {
        // FNA/XNA parity: the singular cube overload is only a convenience wrapper around the
        // normalized binding path. Keeping one path guarantees identical face selection,
        // viewport/scissor reset, RenderTargetUsage handling, resolve, and public state.
        if (renderTarget == nullptr)
        {
            SetRenderTargets({});
            return;
        }
        SetRenderTargets({RenderTargetBinding(renderTarget, cubeMapFace)});
    }
}
