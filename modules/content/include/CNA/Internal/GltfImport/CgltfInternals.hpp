// SPDX-License-Identifier: MS-PL
#pragma once
// Two of cgltf's own element readers, which cgltf.h keeps static to its implementation. The
// importer's sparse-accessor answer (ApplySparseOverridesTightly, GLTF-062) reads elements exactly
// as cgltf does, so CgltfImplementation.cpp -- the one translation unit that compiles cgltf --
// exports them under CNA names rather than the importer compiling a second copy of cgltf. The same
// unit also reads back one token cgltf parses but does not keep (CgltfFindInvalidInterpolation).
#include "cgltf.h"

#include <string>

namespace CNA::Internal::GltfImport
{
    /**
     * @brief cgltf_component_read_index.
     * @param in Component bytes. @param componentType Component type.
     * @return The index.
     */
    cgltf_size CgltfComponentReadIndex(const void* in, cgltf_component_type componentType);

    /**
     * @brief cgltf_element_read_float.
     * @param element Element bytes. @param type Element type. @param componentType Component type.
     * @param normalized Normalized integer components. @param out Destination.
     * @param elementSize Components to write.
     * @return Whether the element was read.
     */
    cgltf_bool CgltfElementReadFloat(const uint8_t* element, cgltf_type type,
                                     cgltf_component_type componentType, cgltf_bool normalized,
                                     cgltf_float* out, cgltf_size elementSize);

    /**
     * @brief Finds the first animation sampler whose authored `interpolation` is not one glTF 2.0
     * allows (LINEAR, STEP, CUBICSPLINE). cgltf keeps only a recognised token and otherwise
     * leaves LINEAR in place, so the token is read back from the document's JSON.
     * @param data Parsed document. @param animationIndex Receives the animation's index.
     * @param samplerIndex Receives the sampler's index. @param token Receives the authored value.
     * @return Whether such a sampler exists.
     */
    bool CgltfFindInvalidInterpolation(const cgltf_data* data, cgltf_size& animationIndex,
                                       cgltf_size& samplerIndex, std::string& token);
}
