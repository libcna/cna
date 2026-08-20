// SPDX-License-Identifier: MS-PL
// Minimal-link probe for CNA::GraphicsCore (plans/MODULARIZATION_PLAN.md §4): device-free graphics
// state objects and vertex metadata must be usable without content, media, runtime, devices
// or networking. The paired ModuleLinkClosure_Graphics ctest inspects this executable's
// generated link line; under CNA_GRAPHICS_RENDERER=HEADLESS it additionally proves the closure
// contains no native renderer SDK library.
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include <cstdio>

int main()
{
    using namespace Microsoft::Xna::Framework::Graphics;

    BlendState blend;
    DepthStencilState depth;
    SamplerState sampler;

    const VertexElement position(
        0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0);
    VertexDeclaration declaration({position});

    std::printf("graphics probe: stride=%d\n", declaration.getVertexStrideProperty());
    return 0;
}
