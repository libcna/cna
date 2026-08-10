// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

namespace CNA::Internal::Renderers::Metal
{
    /**
     * @brief Refuses a declaration the fixed Metal stride table cannot represent faithfully.
     *
     * Metal currently chooses a native vertex descriptor from the uploaded byte stride. This
     * wrapper deliberately delegates the decision to the repository-wide declaration-fidelity
     * oracle, with the same `RendererRefusesIt` policy used by other fixed-table renderers.
     *
     * @param declared Declaration remembered when it was propagated to the vertex buffer.
     * @param nativeRecordStride Byte stride used by the native buffer submission.
     * @param route Draw route name included in a deterministic rejection diagnostic.
     * @throws System::NotSupportedException When the declaration would be reinterpreted.
     */
    inline void RequireFaithfulDeclarationEXT(
        const CNA::Internal::Graphics::DeclaredVertexLayout& declared,
        int nativeRecordStride,
        const char* route)
    {
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            declared,
            nativeRecordStride,
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt,
            "Metal",
            route);
    }
}
