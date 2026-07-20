#pragma once

#include "CNA/Internal/Backends/Metal/MetalVertexAttribFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include <cstddef>
#include <vector>

// plan_metal.md METAL-27: the core, plain-C++ logic of building a generic vertex-attribute layout
// from an arbitrary VertexElement list -- everything except the final MTLVertexDescriptor object
// construction itself (which stays in MetalGraphicsBackend.mm, the one piece that must touch
// Objective-C). Reads only VertexElement (plain XNA framework type) and MetalVertexAttribFormat
// (plain C++, see MetalVertexAttribFormat.hpp), so this is genuinely unit-testable on any platform
// without an Apple toolchain, unlike the 4 (now 8) hand-written fixed-stride MTLVertexDescriptor
// blocks this generalizes.
namespace CNA::Internal::Backends::Metal
{
    // One real Metal vertex-attribute binding, still in neutral (pre-MTLVertexDescriptor) form.
    struct MetalVertexAttributePlan
    {
        int location;                     // Attribute index in the shader's [[stage_in]] struct.
        MetalVertexAttribKind kind;        // Translated to a real MTLVertexFormat in the .mm.
        int offset;                        // Byte offset within one vertex.
        int bufferIndex;                   // Vertex buffer slot (always 0 for a single, non-instanced stream).
    };

    struct MetalVertexDescriptorPlan
    {
        std::vector<MetalVertexAttributePlan> attributes;
        int stride;
    };

    // Builds the plan from an arbitrary VertexElement list. Attribute location = the element's own
    // index within the list (matching EasyGLGraphicsBackend::ApplyLayout()'s own already-tested
    // convention exactly: "layout(location=N) == Nth field of the ported HLSL input struct" --
    // this is a project-wide convention, not an EasyGL-specific choice, so the same rule applies
    // here). `stride` is passed explicitly rather than inferred from the elements themselves,
    // matching every other call site in this backend (GpuDrawParams/IVertexBufferBackend::SetData
    // never derive stride from a VertexDeclaration either) -- the caller (a VertexBuffer's own
    // SetData) is always the authority on the real per-vertex byte size.
    inline MetalVertexDescriptorPlan BuildMetalVertexDescriptorPlan(
        int stride, const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& elements)
    {
        MetalVertexDescriptorPlan plan;
        plan.stride = stride;
        plan.attributes.reserve(elements.size());
        for (std::size_t i = 0; i < elements.size(); ++i)
        {
            const auto& element = elements[i];
            const MetalVertexAttribFormat format = DescribeMetalVertexElementFormat(element.getVertexElementFormatProperty());
            plan.attributes.push_back(MetalVertexAttributePlan{
                /* location    */ static_cast<int>(i),
                /* kind        */ format.kind,
                /* offset      */ element.getOffsetProperty(),
                /* bufferIndex */ 0,
            });
        }
        return plan;
    }
}
