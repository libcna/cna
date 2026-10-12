// SPDX-License-Identifier: MS-PL
//
// The one translation unit that defines CGLTF_IMPLEMENTATION (cgltf.h's single-header convention).
//
// Link boundary -- keep this file separate from GltfImportCore.cpp. cgltf is a small parser that
// more than one subsystem uses: GltfImportCore (glTF models, which also pulls in the Draco decoder)
// and the GamerServices avatar GLB reader. When the implementation lived inside GltfImportCore.cpp,
// the avatar reader's reference to cgltf_parse extracted that whole object, and with it Draco,
// into every program that touched avatars -- see docs/link-size-architecture.md.

// The declarations first: cgltf.h's implementation section has no include guard of its own.
#include "CNA/Internal/GltfImport/CgltfInternals.hpp"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

namespace CNA::Internal::GltfImport
{
    cgltf_size CgltfComponentReadIndex(const void* in, cgltf_component_type componentType)
    {
        return cgltf_component_read_index(in, componentType);
    }

    cgltf_bool CgltfElementReadFloat(const uint8_t* element, cgltf_type type,
                                     cgltf_component_type componentType, cgltf_bool normalized,
                                     cgltf_float* out, cgltf_size elementSize)
    {
        return cgltf_element_read_float(element, type, componentType, normalized, out, elementSize);
    }
}
