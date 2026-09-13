// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    RenderTargetBinding::RenderTargetBinding() = default;

    RenderTargetBinding::RenderTargetBinding(RenderTarget2D* renderTarget)
        : RenderTargetBinding(static_cast<Texture*>(renderTarget), 0)
    {
    }

    RenderTargetBinding::RenderTargetBinding(
        RenderTargetCube* renderTarget, CubeMapFace cubeMapFace)
        : RenderTargetBinding(static_cast<Texture*>(renderTarget), cubeMapFace)
    {
    }

    RenderTargetBinding::RenderTargetBinding(Texture* renderTarget, int arraySlice)
        : renderTarget_(renderTarget), arraySlice_(arraySlice)
    {
        System::ArgumentNullException::ThrowIfNull(renderTarget, "renderTarget");
    }

    RenderTargetBinding::RenderTargetBinding(Texture* renderTarget, CubeMapFace cubeMapFace)
        : renderTarget_(renderTarget), cubeMapFace_(cubeMapFace)
    {
        System::ArgumentNullException::ThrowIfNull(renderTarget, "renderTarget");
    }

    Texture* RenderTargetBinding::getRenderTargetProperty() const { return renderTarget_; }
    int RenderTargetBinding::getArraySliceProperty() const { return arraySlice_; }
    CubeMapFace RenderTargetBinding::getCubeMapFaceProperty() const { return cubeMapFace_; }

    void RenderTargetBinding::ReplaceRenderTargetAfterMove(
        const Texture* source, Texture* destination) noexcept
    {
        if (renderTarget_ == source)
            renderTarget_ = destination;
    }
}
