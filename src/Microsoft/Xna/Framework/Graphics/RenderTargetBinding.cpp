// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    RenderTargetBinding::RenderTargetBinding() = default;

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
}
