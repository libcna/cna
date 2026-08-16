// SPDX-License-Identifier: MS-PL
// plan_runtimerenderer.md RTR-P1: the TinyGL family's pre-construction contract.
//
// MERGE (plan_platform.md x plan_runtimerenderer.md): TINYGL is the 47th public renderer identity
// and arrived on the platform branch, which has no descriptor concept -- so it was the one family
// with no pre-construction contract. This file supplies it, which is what
// scripts/check_runtime_renderer_discipline.py requires of every family.
//
// TinyGL is a CPU fixed-function OpenGL 1.x subset that rasterizes into its own framebuffer: it
// asks the platform for nothing. Despite the name it takes no GL context -- there is no driver
// behind it -- so needsGlContext stays false and the shape matches SOFTWARE and PORTABLEGL rather
// than the EasyGL set.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"

namespace CNA::Internal::Renderers::TinyGL
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args);

    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::TinyGL,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::TinyGL),
            .windowKind               = RendererWindowKind::None,
            .needsWindow              = false,
            .needsVideoSubsystem      = false,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}
