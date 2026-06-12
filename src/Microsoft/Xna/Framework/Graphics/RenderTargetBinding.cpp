// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    RenderTargetBinding::RenderTargetBinding() = default;

    RenderTargetBinding::RenderTargetBinding(Texture* renderTarget, int arraySlice)
        : renderTarget_(renderTarget), arraySlice_(arraySlice)
    {
    }

    RenderTargetBinding::RenderTargetBinding(Texture* renderTarget, CubeMapFace cubeMapFace)
        : renderTarget_(renderTarget), cubeMapFace_(cubeMapFace)
    {
    }

    Texture* RenderTargetBinding::getRenderTargetProperty() const { return renderTarget_; }
    int RenderTargetBinding::getArraySliceProperty() const { return arraySlice_; }
    CubeMapFace RenderTargetBinding::getCubeMapFaceProperty() const { return cubeMapFace_; }
}
