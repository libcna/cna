// SPDX-License-Identifier: MS-PL
//
// plan_cnj.md CNB-70 (Phase 13D): the glTF parsing/skeleton/animation core originally built for
// tools/gltf_to_cnj (Phase 12) is now a reusable library, so a runtime ContentManager reader
// (GltfModelTypeReader, see ContentManager.cpp) can parse a .gltf/.glb file directly into a
// Microsoft::Xna::Framework::Graphics::Model with no intermediate .cnj/binary sidecar files --
// the same parsing/topological-bone-reorder/CUBICSPLINE-Hermite/sparse-accessor logic tools/
// gltf_to_cnj's own file header documents, just returning in-memory structs (MeshOut/ClipOut/
// SkeletonResult/ExtractedImage) instead of writing them to disk. tools/gltf_to_cnj.cpp itself
// was refactored to call these same functions rather than duplicating them.
//
// This is the one and only translation unit that defines CGLTF_IMPLEMENTATION (cgltf.h's own
// single-header convention) -- every other translation unit that needs cgltf symbols (this
// library's own header, tools/gltf_to_cnj.cpp, ContentManager.cpp) links against this instead.

#define CGLTF_IMPLEMENTATION
#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

#include <functional>

// RemapOcclusionImageForDualTextureEXT's own decode/re-encode step (plan_cnj.md CNB-88). STATIC
// so every stbi_*/stbiw_* symbol has internal linkage in this one translation unit -- other
// dependencies in the CNA tree also compile a vendored stb_image.h implementation, and
// without STATIC the two would collide at link time (duplicate global symbols) in any executable
// that links both.
//
// The plain C <math.h>/<stdarg.h> headers are included explicitly (not just <cmath>/<cstdarg>)
// immediately before stb_image.h/stb_image_write.h: this TU's own earlier transitive <cmath>
// inclusion (via Matrix.hpp/Vector3.hpp) left pow/ldexp/frexp/va_arg unavailable in the global
// namespace under this toolchain's libstdc++/glibc pairing, which stb_image.h's/stb_image_write.
// h's own C-style code relies on unqualified.
#include <math.h>
#include <stdarg.h>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_set>

// plan_cnj.md CNB-91 / plan_gltf.md GLTF-353: KHR_draco_mesh_compression decoding. The normal
// build uses CNA's pinned Draco submodule; CNA_DRACO_AVAILABLE remains conditional so packagers
// and the conformance gate can deliberately exercise the decoder-free refusal path.
#ifdef CNA_DRACO_AVAILABLE
#include "draco/compression/decode.h"
#endif

#include "Microsoft/Xna/Framework/Vector2.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

namespace CNA::Internal::GltfImport
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::GltfImportDiagnosticEXT;
        using Microsoft::Xna::Framework::Graphics::GltfImportDiagnosticKindEXT;
        using Microsoft::Xna::Framework::Graphics::GltfImportDiagnosticSeverityEXT;
        using Microsoft::Xna::Framework::Graphics::GltfImportReportEXT;
        using Microsoft::Xna::Framework::Graphics::TextureTransformEXT;

        void AddImportDiagnosticEXT(
            GltfImportReportEXT& report, std::string code,
            GltfImportDiagnosticKindEXT kind, std::string message,
            std::size_t count = 1, double worstMagnitude = 0.0,
            std::string subject = {}, std::vector<std::string> details = {})
        {
            if (count == 0) { return; }
            GltfImportDiagnosticEXT diagnostic;
            diagnostic.Code = std::move(code);
            diagnostic.Severity = kind == GltfImportDiagnosticKindEXT::Information ||
                                  kind == GltfImportDiagnosticKindEXT::GeneratedData
                ? GltfImportDiagnosticSeverityEXT::Information
                : GltfImportDiagnosticSeverityEXT::Warning;
            diagnostic.Kind = kind;
            diagnostic.Subject = std::move(subject);
            diagnostic.Count = count;
            diagnostic.WorstMagnitude = worstMagnitude;
            diagnostic.Details = std::move(details);
            diagnostic.Message = std::move(message);
            report.Diagnostics.push_back(std::move(diagnostic));
        }

        std::size_t NonNegativeSizeEXT(int value)
        {
            return value > 0 ? static_cast<std::size_t>(value) : 0;
        }

        // Whether this build can actually decode Draco geometry. The extension registry's own
        // `claimed` column reads this rather than repeating the #ifdef: claiming Draco in a build
        // without the decoder would accept a file whose geometry then arrives empty, which is the
        // one failure mode the extensionsRequired gate exists to prevent.
#ifdef CNA_DRACO_AVAILABLE
        constexpr bool kDracoAvailable = true;
#else
        constexpr bool kDracoAvailable = false;
#endif

        void AppendFloat(std::vector<std::uint8_t>& out, float v)
        {
            std::uint8_t bytes[4];
            std::memcpy(bytes, &v, 4);
            out.insert(out.end(), bytes, bytes + 4);
        }

        void AppendUint16(std::vector<std::uint8_t>& out, std::uint16_t v)
        {
            std::uint8_t bytes[2];
            std::memcpy(bytes, &v, 2);
            out.insert(out.end(), bytes, bytes + 2);
        }

        void AppendUint32(std::vector<std::uint8_t>& out, std::uint32_t v)
        {
            std::uint8_t bytes[4];
            std::memcpy(bytes, &v, 4);
            out.insert(out.end(), bytes, bytes + 4);
        }

        // glTF matrices (raw node.matrix, inverseBindMatrices) are a flat 16-float array in
        // column-major order with column-vector convention (v' = M * v); XNA's Matrix is
        // row-major with row-vector convention (v' = v * M) -- numerically the transpose of the
        // same transform. Converting by copying basis vectors directly (rather than a generic
        // transpose) since every matrix converted here is a plain affine transform (no
        // projective/shear component).
        Matrix ConvertGltfMatrix(const float g[16])
        {
            return Matrix(
                g[0], g[1], g[2], 0.0f,
                g[4], g[5], g[6], 0.0f,
                g[8], g[9], g[10], 0.0f,
                g[12], g[13], g[14], 1.0f);
        }

        // plan_gltf.md GLTF-062: a CONFIRMED bug in the vendored parser, worked around here.
        //
        // §3.6.2.3 makes a sparse accessor's `values` array TIGHTLY PACKED -- its bufferView must
        // not declare a byteStride at all. cgltf's reader walks it with
        // `reader_head += accessor->stride` instead, which is the BASE bufferView's stride. For a
        // tightly-packed base the two coincide and nothing is wrong, which is why this has never
        // been noticed; but when the base view is INTERLEAVED, the stride is larger than one
        // element and every sparse override after the first is read from the wrong offset. cgltf's
        // own validator disagrees with its own reader here -- it sizes the values view as
        // `element_size * count`, tightly.
        //
        // The result is silently wrong vertex data, so the overrides are re-read here with the
        // packing the specification actually requires, on top of whatever cgltf already wrote.
        // Only for the combination that is affected: a tightly-packed base is left alone entirely,
        // which is every accessor in every asset that does not interleave a sparse attribute.
        //
        // This must run after EVERY cgltf_accessor_unpack_floats call in the importer, not just
        // UnpackAccessor's -- hence a free function rather than a block inside one caller.
        void ApplySparseOverridesTightly(const cgltf_accessor* accessor, float* out,
                                         cgltf_size componentsPerElement, const char* context)
        {
            if (!accessor->is_sparse) { return; }
            const cgltf_accessor_sparse& sparse = accessor->sparse;
            const cgltf_size elementSize = cgltf_calc_size(accessor->type, accessor->component_type);
            if (accessor->stride == elementSize || sparse.count == 0 ||
                sparse.values_buffer_view == nullptr || sparse.indices_buffer_view == nullptr)
            {
                return;
            }

            const auto* values = static_cast<const std::uint8_t*>(
                cgltf_buffer_view_data(sparse.values_buffer_view));
            const auto* indices = static_cast<const std::uint8_t*>(
                cgltf_buffer_view_data(sparse.indices_buffer_view));
            if (values == nullptr || indices == nullptr)
            {
                throw std::runtime_error(
                    std::string("Sparse accessor '") + context +
                    "' has an unreadable indices or values bufferView.");
            }
            values += sparse.values_byte_offset;
            indices += sparse.indices_byte_offset;
            const cgltf_size indexStride = cgltf_component_size(sparse.indices_component_type);

            for (cgltf_size i = 0; i < sparse.count; ++i)
            {
                const cgltf_size writer = cgltf_component_read_index(
                    indices + i * indexStride, sparse.indices_component_type);
                if (writer >= accessor->count)
                {
                    throw std::runtime_error(
                        std::string("Sparse accessor '") + context + "' override " +
                        std::to_string(i) + " targets element " + std::to_string(writer) +
                        ", past the accessor's own " + std::to_string(accessor->count) + ".");
                }
                // The one difference from cgltf: `i * elementSize`, tightly packed, rather than
                // `i * accessor->stride`.
                if (!cgltf_element_read_float(
                        values + i * elementSize, accessor->type, accessor->component_type,
                        accessor->normalized, out + writer * componentsPerElement,
                        componentsPerElement))
                {
                    throw std::runtime_error(
                        std::string("Failed to read sparse override ") + std::to_string(i) +
                        " of accessor '" + context + "'.");
                }
            }
        }

        // plan_gltf.md GLTF-056: the SECOND confirmed defect in the vendored parser, and a smaller
        // one than GLTF-062 only in blast radius.
        //
        // §3.6.2.2 gives the signed normalized conversions as `max(c / 127, -1)` and
        // `max(c / 32767, -1)`. The clamp is not decoration: the negative range of a two's
        // complement integer is one wider than the positive one, so -128/127 is -1.0079 and
        // -32768/32767 is -1.00003 -- outside the unit range the normalization exists to produce.
        // cgltf's `cgltf_component_read_float` divides and returns, with no clamp for either type.
        //
        // Applied here rather than by patching the vendored header, for the same reason as
        // GLTF-062: the workaround stays visible and a cgltf upgrade that fixes it makes this a
        // no-op rather than a conflict. Only for a normalized signed accessor -- every other
        // accessor takes the unchanged path.
        void ClampNormalizedSigned(const cgltf_accessor* accessor, std::vector<float>& values)
        {
            if (accessor->normalized == 0) { return; }
            if (accessor->component_type != cgltf_component_type_r_8 &&
                accessor->component_type != cgltf_component_type_r_16)
            {
                return;
            }
            for (float& value : values)
            {
                if (value < -1.0f) { value = -1.0f; }
            }
        }

        // The last byte an accessor-like walk touches: offset + (count-1)*stride + elementSize,
        // computed so that an overflow throws instead of wrapping into a small, plausible span
        // that would then pass the bounds check (plan_gltf.md §8.1).
        std::size_t RequiredSpan(std::size_t byteOffset, std::size_t count, std::size_t stride,
                                  std::size_t elementSize, const std::string& context)
        {
            if (count == 0) { return byteOffset; }
            constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
            if (stride != 0 && (count - 1) > kMax / stride)
            {
                throw std::runtime_error("Accessor '" + context + "' declares a byte span that "
                                          "overflows: count " + std::to_string(count) +
                                          " at stride " + std::to_string(stride) + ".");
            }
            const std::size_t walk = (count - 1) * stride;
            if (walk > kMax - elementSize || byteOffset > kMax - walk - elementSize)
            {
                throw std::runtime_error("Accessor '" + context + "' declares a byte span that "
                                          "overflows its own byteOffset.");
            }
            return byteOffset + walk + elementSize;
        }

        // plan_gltf.md GLTF-039/GLTF-040: an allocation a malformed file asked for, refused by
        // name.
        //
        // An accessor with **no** bufferView is initialised with zeros (§3.6.2.1), so nothing in
        // the file bounds its count -- an eight-byte edit to the JSON can demand terabytes that
        // the document never shipped. The span guard cannot help there: there is no view to check
        // against. What the caller needs either way is a diagnostic naming the file and the
        // number, and `std::length_error: cannot create std::vector larger than max_size()` from
        // inside the allocator names neither. Reserving through this helper turns both it and
        // `std::bad_alloc` into the same named refusal every other malformed input produces, which
        // is what lets a caller catch one thing. Found by the container fuzz, not by inspection.
        template <typename T>
        std::vector<T> AllocateDecodedElements(std::size_t elements, const std::string& context)
        {
            constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
            if (elements > kMax / sizeof(T))
            {
                throw std::runtime_error("Accessor '" + context + "' declares " +
                                          std::to_string(elements) + " components, whose byte size "
                                          "overflows.");
            }
            try
            {
                return std::vector<T>(elements);
            }
            catch (const std::length_error&)
            {
                throw std::runtime_error(
                    "Accessor '" + context + "' declares " + std::to_string(elements) +
                    " components (" + std::to_string(elements * sizeof(T)) + " bytes), which is "
                    "more than can be allocated.");
            }
            catch (const std::bad_alloc&)
            {
                throw std::runtime_error(
                    "Accessor '" + context + "' declares " + std::to_string(elements) +
                    " components (" + std::to_string(elements * sizeof(T)) + " bytes), which this "
                    "build could not allocate.");
            }
        }

        /// Reserves capacity for @p elements items of @p bytesPerElement, refusing by name when the
        /// product overflows or the allocation fails.
        ///
        /// plan_gltf.md `GLTF-040`. A `reserve` is an optimisation, and an optimisation must not be
        /// the thing that decides how a malformed file fails: an accessor count no buffer backs
        /// reached `vertexBytes.reserve()` before any decode, and the run ended in
        /// `std::length_error: vector::reserve` -- naming neither the primitive nor the number.
        /// Refusing here is also what stops the process from *attempting* the allocation, which for
        /// a count in the billions is a request for tens of gigabytes rather than an error.
        template <typename T>
        void ReserveOrRefuse(std::vector<T>& out, std::size_t elements, std::size_t bytesPerElement,
                              const std::string& context)
        {
            constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
            if (bytesPerElement != 0 && elements > kMax / bytesPerElement)
            {
                throw std::runtime_error("Primitive '" + context + "' declares " +
                                          std::to_string(elements) + " elements at " +
                                          std::to_string(bytesPerElement) +
                                          " bytes each, whose product overflows.");
            }
            try
            {
                out.reserve(elements * bytesPerElement / sizeof(T));
            }
            catch (const std::length_error&)
            {
                throw std::runtime_error("Primitive '" + context + "' declares " +
                                          std::to_string(elements) + " elements (" +
                                          std::to_string(elements * bytesPerElement) +
                                          " bytes), which is more than can be allocated.");
            }
            catch (const std::bad_alloc&)
            {
                throw std::runtime_error("Primitive '" + context + "' declares " +
                                          std::to_string(elements) + " elements (" +
                                          std::to_string(elements * bytesPerElement) +
                                          " bytes), which this build could not allocate.");
            }
        }

        // Unpacks an entire accessor to floats in one call. Unlike per-element
        // cgltf_accessor_read_float, cgltf_accessor_unpack_floats correctly resolves sparse
        // accessors (base values overlaid with sparse overrides) -- read_float rejects sparse
        // accessors outright.
        std::vector<float> UnpackAccessor(const cgltf_accessor* accessor, cgltf_size expectedComponents,
                                           const char* context)
        {
            const cgltf_size actualComponents = cgltf_num_components(accessor->type);
            if (actualComponents != expectedComponents)
            {
                throw std::runtime_error(
                    std::string("Accessor '") + context + "' has " + std::to_string(actualComponents) +
                    " components per element, expected " + std::to_string(expectedComponents) + ".");
            }
            // plan_gltf.md GLTF-039. The span guard again, at the point of allocation.
            // `ValidateGltfEXT` runs it first on every production load, but `ExtractMesh` is
            // reachable without it -- the conformance harness and the offline converter both call
            // the extraction path directly -- and an accessor whose declared count wraps its own
            // span otherwise surfaces as `std::length_error: cannot create std::vector larger than
            // max_size()`, thrown from inside the allocator, naming neither the file nor the
            // accessor nor what is wrong with it.
            if (accessor->buffer_view != nullptr)
            {
                const std::size_t elementSize =
                    cgltf_calc_size(accessor->type, accessor->component_type);
                const std::size_t stride = accessor->stride != 0
                    ? static_cast<std::size_t>(accessor->stride) : elementSize;
                const std::size_t span = RequiredSpan(static_cast<std::size_t>(accessor->offset),
                                                       static_cast<std::size_t>(accessor->count),
                                                       stride, elementSize, context);
                if (span > static_cast<std::size_t>(accessor->buffer_view->size))
                {
                    throw std::runtime_error(
                        std::string("Accessor '") + context + "' declares " +
                        std::to_string(static_cast<std::size_t>(accessor->count)) +
                        " elements, which read " + std::to_string(span) +
                        " bytes from a bufferView of " +
                        std::to_string(static_cast<std::size_t>(accessor->buffer_view->size)) +
                        " bytes.");
                }
            }
            std::size_t components = static_cast<std::size_t>(accessor->count);
            if (expectedComponents != 0 &&
                components > std::numeric_limits<std::size_t>::max() /
                                 static_cast<std::size_t>(expectedComponents))
            {
                throw std::runtime_error(std::string("Accessor '") + context + "' declares " +
                                          std::to_string(components) + " elements of " +
                                          std::to_string(expectedComponents) +
                                          " components, whose product overflows.");
            }
            components *= static_cast<std::size_t>(expectedComponents);
            std::vector<float> out = AllocateDecodedElements<float>(components, context);
            const cgltf_size unpacked = cgltf_accessor_unpack_floats(accessor, out.data(), out.size());
            if (unpacked != out.size())
            {
                throw std::runtime_error(
                    std::string("Failed to unpack accessor '") + context +
                    "' (malformed data or an unsupported layout).");
            }
            ApplySparseOverridesTightly(accessor, out.data(), expectedComponents, context);
            ClampNormalizedSigned(accessor, out);
            return out;
        }

        // plan_gltf.md GLTF-063: the index accessor's own decode path.
        //
        // Indices are NOT read through cgltf_accessor_read_index. That function returns 0 -- with
        // no error channel, as its own upstream comment concedes -- for a sparse accessor and for
        // one with no bufferView, so a spec-legal sparse index accessor silently decoded to all
        // zeros and collapsed the primitive to a degenerate point. Everything below mirrors
        // UnpackAccessor's structure (validate, then decode the whole accessor in one call,
        // throwing a named error rather than returning a wrong value) for the index accessor's own
        // integer semantics. The attribute path is unchanged and must stay that way -- it was
        // proven correct and is locked by GLTF-041.

        // glTF §3.6.2.2/§3.7.2.1: an index accessor is SCALAR, non-normalized, and one of exactly
        // three unsigned component types. Anything else is a malformed file.
        std::size_t IndexComponentSize(cgltf_component_type type)
        {
            switch (type)
            {
                case cgltf_component_type_r_8u:  return 1;
                case cgltf_component_type_r_16u: return 2;
                case cgltf_component_type_r_32u: return 4;
                default: return 0;
            }
        }

        const char* ComponentTypeName(cgltf_component_type type)
        {
            switch (type)
            {
                case cgltf_component_type_r_8:   return "BYTE (5120)";
                case cgltf_component_type_r_8u:  return "UNSIGNED_BYTE (5121)";
                case cgltf_component_type_r_16:  return "SHORT (5122)";
                case cgltf_component_type_r_16u: return "UNSIGNED_SHORT (5123)";
                case cgltf_component_type_r_32u: return "UNSIGNED_INT (5125)";
                case cgltf_component_type_r_32f: return "FLOAT (5126)";
                default: return "an unrecognized component type";
            }
        }

        // Reads one unsigned index component. std::memcpy rather than a cast through a pointer to
        // the wider type: a bufferView may legally start at any byte offset, so the source address
        // is not guaranteed to satisfy the alignment of uint16_t/uint32_t (plan_gltf.md §8.4).
        std::uint32_t ReadIndexComponent(const std::uint8_t* at, cgltf_component_type type)
        {
            switch (type)
            {
                case cgltf_component_type_r_8u:
                {
                    std::uint8_t v = 0;
                    std::memcpy(&v, at, sizeof(v));
                    return v;
                }
                case cgltf_component_type_r_16u:
                {
                    std::uint16_t v = 0;
                    std::memcpy(&v, at, sizeof(v));
                    return v;
                }
                default:
                {
                    std::uint32_t v = 0;
                    std::memcpy(&v, at, sizeof(v));
                    return v;
                }
            }
        }


        // Decodes a whole index accessor to uint32, resolving `sparse` overrides (§3.6.2.3) and
        // bounds-checking every read against the owning bufferView. Never returns a value it could
        // not actually read: a malformed accessor throws a named error, because "wrong geometry
        // with no diagnostic" is the failure mode this replaces.
        std::vector<std::uint32_t> UnpackIndexAccessor(const cgltf_accessor& accessor,
                                                        const std::string& context)
        {
            if (accessor.type != cgltf_type_scalar)
            {
                throw std::runtime_error("Index accessor of '" + context + "' is not SCALAR, which "
                                          "the glTF specification requires for primitive indices.");
            }
            if (accessor.normalized)
            {
                throw std::runtime_error("Index accessor of '" + context + "' declares "
                                          "'normalized', which the glTF specification forbids for "
                                          "primitive indices.");
            }
            const std::size_t componentSize = IndexComponentSize(accessor.component_type);
            if (componentSize == 0)
            {
                throw std::runtime_error(
                    "Index accessor of '" + context + "' has component type " +
                    ComponentTypeName(accessor.component_type) + "; the glTF specification allows "
                    "only UNSIGNED_BYTE (5121), UNSIGNED_SHORT (5123) and UNSIGNED_INT (5125) for "
                    "primitive indices.");
            }

            const std::size_t count = static_cast<std::size_t>(accessor.count);
            std::vector<std::uint32_t> out = AllocateDecodedElements<std::uint32_t>(count, context);

            if (accessor.buffer_view != nullptr)
            {
                const std::uint8_t* view = cgltf_buffer_view_data(accessor.buffer_view);
                if (view == nullptr)
                {
                    throw std::runtime_error("Index accessor of '" + context + "' references a "
                                              "bufferView whose buffer data is not loaded.");
                }
                // cgltf resolves accessor.stride to bufferView.byteStride when the view declares
                // one and to the element size otherwise, so it is never zero here.
                const std::size_t stride = static_cast<std::size_t>(accessor.stride) != 0
                    ? static_cast<std::size_t>(accessor.stride) : componentSize;
                const std::size_t byteOffset = static_cast<std::size_t>(accessor.offset);
                const std::size_t span = RequiredSpan(byteOffset, count, stride, componentSize, context);
                if (span > static_cast<std::size_t>(accessor.buffer_view->size))
                {
                    throw std::runtime_error(
                        "Index accessor of '" + context + "' reads " + std::to_string(span) +
                        " bytes from a bufferView that is only " +
                        std::to_string(static_cast<std::size_t>(accessor.buffer_view->size)) +
                        " bytes long.");
                }
                for (std::size_t i = 0; i < count; ++i)
                {
                    out[i] = ReadIndexComponent(view + byteOffset + i * stride, accessor.component_type);
                }
            }
            else if (!accessor.is_sparse)
            {
                // §3.6.2: an accessor with neither a bufferView nor sparse data has no values at
                // all. Reading it as zeros is exactly the silent corruption GLTF-063 removes.
                throw std::runtime_error("Index accessor of '" + context + "' has neither a "
                                          "bufferView nor sparse data, so it carries no indices.");
            }
            // else: §3.6.2.3's zero-initialised base array, which the sparse pass below displaces.

            if (accessor.is_sparse && accessor.sparse.count > 0)
            {
                const cgltf_accessor_sparse& sparse = accessor.sparse;
                const std::size_t sparseCount = static_cast<std::size_t>(sparse.count);
                const std::size_t sparseIndexSize = IndexComponentSize(sparse.indices_component_type);
                if (sparseIndexSize == 0)
                {
                    throw std::runtime_error(
                        "Index accessor of '" + context + "' has sparse indices of component type " +
                        ComponentTypeName(sparse.indices_component_type) + ", which the glTF "
                        "specification does not allow for a sparse index array.");
                }
                if (sparse.indices_buffer_view == nullptr || sparse.values_buffer_view == nullptr)
                {
                    throw std::runtime_error("Index accessor of '" + context + "' has sparse data "
                                              "with a missing indices or values bufferView.");
                }
                const std::uint8_t* sparseIndices = cgltf_buffer_view_data(sparse.indices_buffer_view);
                const std::uint8_t* sparseValues = cgltf_buffer_view_data(sparse.values_buffer_view);
                if (sparseIndices == nullptr || sparseValues == nullptr)
                {
                    throw std::runtime_error("Index accessor of '" + context + "' has sparse "
                                              "bufferViews whose buffer data is not loaded.");
                }
                // §3.6.2.3: both sparse arrays are tightly packed -- their bufferViews may not
                // declare a byteStride -- so the element size is the stride here, never the base
                // accessor's own (possibly interleaved) stride.
                const std::size_t indexSpan = RequiredSpan(
                    static_cast<std::size_t>(sparse.indices_byte_offset), sparseCount,
                    sparseIndexSize, sparseIndexSize, context);
                const std::size_t valueSpan = RequiredSpan(
                    static_cast<std::size_t>(sparse.values_byte_offset), sparseCount,
                    componentSize, componentSize, context);
                if (indexSpan > static_cast<std::size_t>(sparse.indices_buffer_view->size) ||
                    valueSpan > static_cast<std::size_t>(sparse.values_buffer_view->size))
                {
                    throw std::runtime_error("Index accessor of '" + context + "' has a sparse "
                                              "array that reads past its own bufferView.");
                }

                for (std::size_t k = 0; k < sparseCount; ++k)
                {
                    const std::uint32_t target = ReadIndexComponent(
                        sparseIndices + static_cast<std::size_t>(sparse.indices_byte_offset) +
                            k * sparseIndexSize,
                        sparse.indices_component_type);
                    if (static_cast<std::size_t>(target) >= count)
                    {
                        throw std::runtime_error(
                            "Index accessor of '" + context + "' has sparse entry " +
                            std::to_string(k) + " displacing element " + std::to_string(target) +
                            ", but the accessor declares only " + std::to_string(count) +
                            " elements.");
                    }
                    out[target] = ReadIndexComponent(
                        sparseValues + static_cast<std::size_t>(sparse.values_byte_offset) +
                            k * componentSize,
                        accessor.component_type);
                }
            }

            return out;
        }

        // Uniformly scales just the translation part of an affine transform, leaving rotation and
        // any local scale factor untouched -- correct for a global unit-of-measure correction
        // (e.g. a source file authored in centimeters), not a shape-changing per-axis scale.
        // Applying the same factor to both a bone's local bind pose translation and its
        // (separately glTF-authored) inverse bind matrix translation is mathematically
        // consistent: Inverse([R|t]) = [R^-1 | -R^-1*t], so scaling t by k scales the inverse's
        // own translation by exactly k too, not 1/k.
        Matrix ScaleTranslation(const Matrix& m, float scale)
        {
            Matrix result = m;
            result.M41 *= scale;
            result.M42 *= scale;
            result.M43 *= scale;
            return result;
        }

        // The determinant of a transform's linear (3x3) part. Its SIGN is the whole question for
        // GLTF-116: negative means the transform mirrors, so the triangle winding a renderer sees
        // is the reverse of the one the file authored. The translation row is excluded because it
        // has no bearing on handedness -- for the affine matrices produced here the 4x4
        // determinant happens to agree, but only as long as the last column stays (0,0,0,1), and
        // relying on that would make an unrelated future change silently answer a different
        // question.
        float Determinant3x3(const Matrix& m)
        {
            return m.M11 * (m.M22 * m.M33 - m.M23 * m.M32)
                 - m.M12 * (m.M21 * m.M33 - m.M23 * m.M31)
                 + m.M13 * (m.M21 * m.M32 - m.M22 * m.M31);
        }

        // A fully unpacked (and therefore sparse-accessor-safe) animation channel: sample times
        // plus the flattened value array (componentsPerValue floats per sample; 3x samples for
        // CUBICSPLINE, only the middle "value" third of each triplet is ever read).
        struct SampledChannel
        {
            std::vector<double> times;
            std::vector<float> values;
            int componentsPerValue = 0;
            bool cubicSpline = false;
            bool stepInterpolation = false;
            /// Adjacent input samples sharing a time (§3.11 forbids them; see `LoadChannel`).
            int duplicateTimes = 0;
        };

        // plan_gltf.md GLTF-313. §3.11 requires a sampler's input times to be **strictly
        // increasing**, and every reader here takes that on trust: `FindBracket` walks the array
        // once looking for the first pair straddling t, and `BuildTrack` merges channel times with
        // a sort-then-unique that assumes the inputs were already ordered.
        //
        // The two ways to break that rule are not equally harmful, so they get different answers:
        //
        //   * A **decreasing** step is refused. There is no defensible reading of it -- the curve
        //     doubles back on itself, so a time inside the reversed span has two authored values
        //     and `FindBracket` returns whichever it meets first. Sorting instead would silently
        //     re-pair each time with a different value than the exporter wrote, turning a broken
        //     file into a plausible-looking wrong animation, which is worse than a named failure.
        //   * **Equal** adjacent times are tolerated and counted. They are what an exporter emits
        //     for a hard cut (hold, then jump), `FindBracket`'s zero-length span already yields
        //     amount 0 and therefore the earlier sample, and refusing them would reject assets
        //     that play correctly. The count reaches `AnimationReportEXT` so the tolerance is
        //     visible rather than assumed.
        SampledChannel LoadChannel(const cgltf_animation_channel& ch, cgltf_size componentsPerValue,
                                    const std::string& context)
        {
            SampledChannel result;
            result.componentsPerValue = static_cast<int>(componentsPerValue);
            result.cubicSpline = ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
            result.stepInterpolation = ch.sampler->interpolation == cgltf_interpolation_type_step;

            const std::vector<float> times = UnpackAccessor(ch.sampler->input, 1, context.c_str());
            result.times.assign(times.begin(), times.end());
            for (std::size_t i = 0; i + 1 < result.times.size(); ++i)
            {
                if (result.times[i + 1] < result.times[i])
                {
                    throw std::runtime_error(
                        "Animation sampler input of " + context + " is not ascending: sample " +
                        std::to_string(i + 1) + " is at t=" + std::to_string(result.times[i + 1]) +
                        ", before sample " + std::to_string(i) + " at t=" +
                        std::to_string(result.times[i]) +
                        ". glTF requires sampler input to be strictly increasing.");
                }
                if (result.times[i + 1] == result.times[i]) { ++result.duplicateTimes; }
            }
            result.values = UnpackAccessor(ch.sampler->output, componentsPerValue, context.c_str());
            return result;
        }

        // plan_gltf.md GLTF-301. STEP holds the sample at the START of the interval it is in, and
        // §3.6's intervals are half-open: `times[i]`'s value applies on `[times[i], times[i+1])`,
        // so at exactly `times[i+1]` the NEXT sample is already in force.
        //
        // FindBracket reports that moment as `lo = i` with `amount = 1`, which is right for LINEAR
        // -- `Lerp(a, b, 1)` is `b` -- and was wrong for STEP, whose branch ignored `amount` and
        // returned sample `i`. That is not an edge case: `ExtractClips` resamples onto the union of
        // a bone's own channel times, so EVERY interior keyframe of a STEP channel lands exactly on
        // a sample boundary. A three-sample STEP channel therefore played 0, 0, 20 instead of
        // 0, 10, 20 -- the last value correct only because the end clamp takes a different path,
        // which is what made it look plausible.
        std::size_t StepSampleIndex(std::size_t lo, float amount, std::size_t count)
        {
            return (amount >= 1.0f && lo + 1 < count) ? lo + 1 : lo;
        }

        // Finds the bracketing pair [lo, lo+1] in a sorted, ascending time array such that
        // times[lo] <= t <= times[lo+1] (clamped at the ends), returning lo and the interpolation
        // fraction within that bracket.
        void FindBracket(const std::vector<double>& times, double t, std::size_t& lo, float& amount)
        {
            if (t <= times.front()) { lo = 0; amount = 0.0f; return; }
            if (t >= times.back()) { lo = times.size() - 1; amount = 0.0f; return; }
            for (std::size_t i = 0; i + 1 < times.size(); ++i)
            {
                if (t >= times[i] && t <= times[i + 1])
                {
                    lo = i;
                    const double span = times[i + 1] - times[i];
                    amount = span > 0.0 ? static_cast<float>((t - times[i]) / span) : 0.0f;
                    return;
                }
            }
            lo = times.size() - 1;
            amount = 0.0f;
        }

        Vector3 ReadVec3Sample(const SampledChannel& ch, std::size_t sampleIndex)
        {
            const std::size_t idx = ch.cubicSpline ? sampleIndex * 3 + 1 : sampleIndex;
            const std::size_t o = idx * static_cast<std::size_t>(ch.componentsPerValue);
            return Vector3(ch.values[o], ch.values[o + 1], ch.values[o + 2]);
        }

        Quaternion ReadQuatSample(const SampledChannel& ch, std::size_t sampleIndex)
        {
            const std::size_t idx = ch.cubicSpline ? sampleIndex * 3 + 1 : sampleIndex;
            const std::size_t o = idx * static_cast<std::size_t>(ch.componentsPerValue);
            return Quaternion(ch.values[o], ch.values[o + 1], ch.values[o + 2], ch.values[o + 3]);
        }

        // glTF's CUBICSPLINE Hermite basis, applied component-wise: given the bracketing
        // keyframes lo/lo+1 (each holding an [in-tangent, value, out-tangent] triplet in
        // ch.values), the normalized fraction s within the bracket, and the bracket's real time
        // span deltaT (the spec's Hermite formula scales tangents by the interval length, not
        // just s), writes ch.componentsPerValue interpolated components into out.
        void HermiteEvaluate(const SampledChannel& ch, std::size_t lo, float s, double deltaT, float* out)
        {
            const int n = ch.componentsPerValue;
            const std::size_t stride = static_cast<std::size_t>(n);
            const float* v0 = ch.values.data() + (lo * 3 + 1) * stride;       // value at lo
            const float* b0 = ch.values.data() + (lo * 3 + 2) * stride;       // out-tangent at lo
            const float* v1 = ch.values.data() + ((lo + 1) * 3 + 1) * stride; // value at lo+1
            const float* a1 = ch.values.data() + ((lo + 1) * 3 + 0) * stride; // in-tangent at lo+1

            const float s2 = s * s, s3 = s2 * s;
            const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f;
            const float h10 = s3 - 2.0f * s2 + s;
            const float h01 = -2.0f * s3 + 3.0f * s2;
            const float h11 = s3 - s2;
            const float dt = static_cast<float>(deltaT);

            for (int i = 0; i < n; ++i)
            {
                out[i] = h00 * v0[i] + dt * h10 * b0[i] + h01 * v1[i] + dt * h11 * a1[i];
            }
        }

        Vector3 EvaluateVec3Channel(const SampledChannel* ch, double t, Vector3 fallback)
        {
            if (ch == nullptr) { return fallback; }
            std::size_t lo = 0; float amount = 0.0f;
            FindBracket(ch->times, t, lo, amount);
            if (ch->stepInterpolation)
            {
                return ReadVec3Sample(*ch, StepSampleIndex(lo, amount, ch->times.size()));
            }
            if (amount <= 0.0f || lo + 1 >= ch->times.size())
            {
                return ReadVec3Sample(*ch, lo);
            }
            if (ch->cubicSpline)
            {
                float out[3];
                HermiteEvaluate(*ch, lo, amount, ch->times[lo + 1] - ch->times[lo], out);
                return Vector3(out[0], out[1], out[2]);
            }
            return Vector3::Lerp(ReadVec3Sample(*ch, lo), ReadVec3Sample(*ch, lo + 1), amount);
        }

        Quaternion EvaluateQuatChannel(const SampledChannel* ch, double t, Quaternion fallback)
        {
            if (ch == nullptr) { return fallback; }
            std::size_t lo = 0; float amount = 0.0f;
            FindBracket(ch->times, t, lo, amount);
            if (ch->stepInterpolation)
            {
                return ReadQuatSample(*ch, StepSampleIndex(lo, amount, ch->times.size()));
            }
            if (amount <= 0.0f || lo + 1 >= ch->times.size())
            {
                return ReadQuatSample(*ch, lo);
            }
            if (ch->cubicSpline)
            {
                // Component-wise Hermite does not preserve unit length -- normalize afterward,
                // the standard treatment for glTF CUBICSPLINE rotation channels.
                float out[4];
                HermiteEvaluate(*ch, lo, amount, ch->times[lo + 1] - ch->times[lo], out);
                Quaternion q(out[0], out[1], out[2], out[3]);
                const float lenSq = q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W;
                if (lenSq > 1e-12f)
                {
                    const float invLen = 1.0f / std::sqrt(lenSq);
                    q = Quaternion(q.X * invLen, q.Y * invLen, q.Z * invLen, q.W * invLen);
                }
                return q;
            }
            return Quaternion::Slerp(ReadQuatSample(*ch, lo), ReadQuatSample(*ch, lo + 1), amount);
        }

        std::uint8_t ToByteColorChannel(float v)
        {
            return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        }

        // The interior angle at the corner where edges `a` and `b` (both pointing away from that
        // corner) meet, in radians, clamped to a safe acos() domain -- 0.0 for a degenerate
        // (zero-length) edge, which naturally zeroes that corner's contribution below rather than
        // needing a separate special case.
        float CornerAngleEXT(const Vector3& a, const Vector3& b)
        {
            const float lenA = a.Length(), lenB = b.Length();
            if (lenA < 1e-12f || lenB < 1e-12f) { return 0.0f; }
            const float cosAngle = std::clamp(Vector3::Dot(a, b) / (lenA * lenB), -1.0f, 1.0f);
            return std::acos(cosAngle);
        }

        // plan_cnj.md CNB-57/CNB-94 (Phase 13A/14G): computes a per-vertex tangent basis when the
        // file has no TANGENT accessor of its own, for PbrEffect's normal mapping.
        //
        // Per triangle: the same position+UV-gradient tangent/bitangent formula Lengyel's method
        // and MikkTSpace both start from, skipped entirely for a degenerate-UV triangle (near-zero
        // UV parallelogram area) so it contributes neither a real value nor NaN/Inf to any of its
        // 3 corners. That per-triangle tangent/bitangent is then accumulated onto each of its 3
        // corners weighted by the triangle's own interior angle at that specific corner (via
        // CornerAngleEXT) rather than an unweighted sum -- the real MikkTSpace algorithm's own
        // rationale: an unweighted sum implicitly lets a large or thin triangle sharing a vertex
        // dominate a small one, which angle-weighting corrects for. Finalized per vertex via
        // Gram-Schmidt orthogonalization against the vertex normal and a handedness sign derived
        // from agreement with the accumulated bitangent -- the same finalization step real
        // MikkTSpace itself also uses.
        //
        // This is NOT a bit-for-bit port of Morten Mikkelsen's reference `mikktspace.c`. Real
        // MikkTSpace internally welds compatible face corners into shared "TSpace" groups and
        // returns an unindexed result; its own API explicitly forbids averaging that result back
        // through an existing index list. This simpler routine owns exactly one tangent per glTF
        // vertex and cannot reproduce that topology step. GLTF-179 measures the consequence on a
        // duplicated compatible edge: 42.1447 degrees maximum / 34.4110 degrees RMS across its six
        // corners, with no handedness mismatch. A documented, deliberate scope cut, not an
        // unmeasured claim of parity.
        std::vector<Vector4> ComputeTangentsEXT(const std::vector<float>& positions,
                                                 const std::vector<float>& normals,
                                                 const std::vector<float>& uvs,
                                                 const std::vector<std::uint32_t>& indices,
                                                 cgltf_size vertexCount)
        {
            std::vector<Vector3> tanAccum(vertexCount);
            std::vector<Vector3> bitanAccum(vertexCount);

            auto pos = [&](std::uint32_t idx) {
                const std::size_t o = static_cast<std::size_t>(idx) * 3;
                return Vector3(positions[o], positions[o + 1], positions[o + 2]);
            };
            auto uv = [&](std::uint32_t idx) {
                if (uvs.empty()) { return Vector2(0.0f, 0.0f); }
                const std::size_t o = static_cast<std::size_t>(idx) * 2;
                return Vector2(uvs[o], uvs[o + 1]);
            };

            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const std::uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];

                const Vector3 p0 = pos(i0), p1 = pos(i1), p2 = pos(i2);
                const Vector2 uv0 = uv(i0), uv1 = uv(i1), uv2 = uv(i2);

                const Vector3 edge1 = p1 - p0, edge2 = p2 - p0;
                const float du1 = uv1.X - uv0.X, dv1 = uv1.Y - uv0.Y;
                const float du2 = uv2.X - uv0.X, dv2 = uv2.Y - uv0.Y;
                const float denom = du1 * dv2 - du2 * dv1;
                if (std::fabs(denom) < 1e-12f) { continue; } // degenerate UV triangle -- no contribution

                const float f = 1.0f / denom;
                const Vector3 tangent   = f * (dv2 * edge1 - dv1 * edge2);
                const Vector3 bitangent = f * (du1 * edge2 - du2 * edge1);

                // Angle-weighted accumulation: the interior angle at each of the 3 corners,
                // computed from the two triangle edges meeting there (note edge3 = p2-p1, needed
                // only for corner i1/i2's own angle, not the tangent formula above).
                const Vector3 edge3 = p2 - p1;
                const float angle0 = CornerAngleEXT(edge1, edge2);           // corner i0: edges (p1-p0),(p2-p0)
                const float angle1 = CornerAngleEXT(-edge1, edge3);          // corner i1: edges (p0-p1),(p2-p1)
                const float angle2 = CornerAngleEXT(-edge2, -edge3);         // corner i2: edges (p0-p2),(p1-p2)

                tanAccum[i0] = tanAccum[i0] + tangent * angle0;
                tanAccum[i1] = tanAccum[i1] + tangent * angle1;
                tanAccum[i2] = tanAccum[i2] + tangent * angle2;
                bitanAccum[i0] = bitanAccum[i0] + bitangent * angle0;
                bitanAccum[i1] = bitanAccum[i1] + bitangent * angle1;
                bitanAccum[i2] = bitanAccum[i2] + bitangent * angle2;
            }

            std::vector<Vector4> result(vertexCount);
            for (cgltf_size v = 0; v < vertexCount; ++v)
            {
                const std::size_t o = static_cast<std::size_t>(v) * 3;
                const Vector3 n = normals.empty() ? Vector3(0.0f, 0.0f, 1.0f)
                                                   : Vector3(normals[o], normals[o + 1], normals[o + 2]);
                Vector3 t = tanAccum[v] - n * Vector3::Dot(n, tanAccum[v]);
                const float len = t.Length();
                Vector3 tOrtho = (len > 1e-8f) ? Vector3(t.X / len, t.Y / len, t.Z / len) : Vector3(1.0f, 0.0f, 0.0f);
                const float handedness = (Vector3::Dot(Vector3::Cross(n, tOrtho), bitanAccum[v]) < 0.0f) ? -1.0f : 1.0f;
                result[v] = Vector4(tOrtho.X, tOrtho.Y, tOrtho.Z, handedness);
            }
            return result;
        }

        // cgltf_find_accessor only searches a cgltf_primitive's own attributes[], not a morph
        // target's -- cgltf has no built-in equivalent for cgltf_morph_target, so this mirrors it
        // for the one attribute shape (a flat cgltf_attribute[] array) both share.
        const cgltf_accessor* FindMorphTargetAttribute(const cgltf_morph_target& target, cgltf_attribute_type type)
        {
            for (cgltf_size i = 0; i < target.attributes_count; ++i)
            {
                if (target.attributes[i].type == type) { return target.attributes[i].data; }
            }
            return nullptr;
        }

        // Nodes reachable from the file's default scene (data->scene, or the first scene if
        // that's unset but at least one scene exists), walked via each node's own children[]
        // array. A file with more than one scene may have nodes that exist only in a non-default
        // scene, or that aren't part of any scene at all (e.g. staging nodes an authoring tool
        // left behind) -- those must not be silently imported alongside the actual content.
        std::unordered_set<const cgltf_node*> CollectSceneReachableNodes(const cgltf_data* data)
        {
            std::unordered_set<const cgltf_node*> reachable;
            const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
            if (!scene) { return reachable; } // no scenes at all -- caller falls back to "every node"

            std::vector<const cgltf_node*> stack(scene->nodes, scene->nodes + scene->nodes_count);
            while (!stack.empty())
            {
                const cgltf_node* node = stack.back();
                stack.pop_back();
                if (!reachable.insert(node).second) { continue; } // already visited
                for (cgltf_size c = 0; c < node->children_count; ++c) { stack.push_back(node->children[c]); }
            }
            return reachable;
        }
    }

    void AppendGltfNodeGraphReportEXT(GltfImportReportEXT& destination,
                                      const NodeGraphReportEXT& source)
    {
        destination.NodeCount = NonNegativeSizeEXT(source.nodeCount);
        destination.MeshInstanceCount = NonNegativeSizeEXT(source.meshInstanceCount);
        destination.DistinctMeshCount = NonNegativeSizeEXT(source.distinctMeshCount);
        destination.SharedMeshCount = NonNegativeSizeEXT(source.sharedMeshCount);
        destination.MaxNodeDepth = NonNegativeSizeEXT(source.maxDepth);
        destination.CameraNodeCount = NonNegativeSizeEXT(source.cameraNodeCount);
        destination.LightNodeCount = NonNegativeSizeEXT(source.lightNodeCount);
        AddImportDiagnosticEXT(
            destination, "gpu-instancing-dropped", GltfImportDiagnosticKindEXT::DroppedData,
            "Per-instance transforms from EXT_mesh_gpu_instancing were not imported; each node "
            "keeps only its own single placement.",
            NonNegativeSizeEXT(source.gpuInstancedNodeCount));
    }

    void AppendGltfValidationWarningsEXT(GltfImportReportEXT& destination,
                                         const std::vector<std::string>& warnings)
    {
        for (const std::string& warning : warnings)
        {
            const bool ignoredExtension = warning.find(" uses extension '") != std::string::npos;
            std::string portableMessage = warning;
            if (ignoredExtension)
            {
                // ValidateGltfEXT includes sourceName before this phrase. Reports serialized into
                // .cnj must remain relocatable and reproducible, so retain the useful extension
                // diagnosis without embedding an absolute build-machine path.
                const std::size_t phrase = warning.find(" uses extension '");
                portableMessage = "The source asset" + warning.substr(phrase);
            }
            AddImportDiagnosticEXT(
                destination, ignoredExtension ? "ignored-extension" : "accessor-bounds-mismatch",
                ignoredExtension ? GltfImportDiagnosticKindEXT::UnsupportedFeature
                                 : GltfImportDiagnosticKindEXT::InvalidSourceData,
                std::move(portableMessage));
        }
    }

    void AppendGltfInstanceReportEXT(GltfImportReportEXT& destination,
                                     const MeshInstanceOut& instance,
                                     const std::string& subject)
    {
        if (!instance.mirroredEXT) { return; }
        AddImportDiagnosticEXT(
            destination, "mirrored-winding-unapplied", GltfImportDiagnosticKindEXT::DroppedData,
            "The placement has a negative world determinant, but CNA leaves shared triangle "
            "winding unchanged; the application must reverse culling for this draw.",
            1, 0.0, subject);
    }

    void AppendGltfMeshReportEXT(GltfImportReportEXT& destination, const MeshOut& mesh,
                                 const std::string& subject, bool countPrimitive)
    {
        if (countPrimitive) { ++destination.PrimitiveCount; }
        if (!mesh.unsupportedMaterialModelEXT.empty())
        {
            AddImportDiagnosticEXT(
                destination, "material-model-dropped",
                GltfImportDiagnosticKindEXT::DroppedData,
                "The selected vertex layout cannot carry this material model; its factors and "
                "maps were not applied.", 1, 0.0, subject,
                {mesh.unsupportedMaterialModelEXT});
        }
        if (mesh.convertedFromSpecularGlossinessEXT)
        {
            AddImportDiagnosticEXT(
                destination, "specular-glossiness-converted",
                GltfImportDiagnosticKindEXT::Approximation,
                "KHR_materials_pbrSpecularGlossiness was converted to metallic-roughness and "
                "its coloured specular term cannot be represented exactly.",
                1, mesh.droppedSpecularStrengthEXT, subject);
        }
        if (!mesh.unrepresentableForStrideEXT.empty())
        {
            AddImportDiagnosticEXT(
                destination, "vertex-layout-limit", GltfImportDiagnosticKindEXT::Information,
                "The chosen CNA vertex layout cannot carry every authored stream or material "
                "feature.", 1, 0.0, subject, {mesh.unrepresentableForStrideEXT});
        }
        if (mesh.droppedNormalForStrideEXT)
        {
            AddImportDiagnosticEXT(
                destination, "normal-dropped", GltfImportDiagnosticKindEXT::DroppedData,
                "The primitive authored NORMAL, but the selected vertex layout has no normal slot.",
                1, 0.0, subject);
        }
        if (mesh.droppedTangentForStrideEXT)
        {
            AddImportDiagnosticEXT(
                destination, "tangent-dropped", GltfImportDiagnosticKindEXT::DroppedData,
                "The primitive authored TANGENT, but the selected vertex layout has no tangent slot.",
                1, 0.0, subject);
        }
        if (mesh.droppedIncompleteIndicesEXT > 0)
        {
            AddImportDiagnosticEXT(
                destination, "incomplete-indices-dropped",
                GltfImportDiagnosticKindEXT::DroppedData,
                "Trailing indices that did not form a complete primitive were dropped.",
                mesh.droppedIncompleteIndicesEXT, 0.0, subject);
        }
        if (!mesh.unsupportedTextureSourcesEXT.empty())
        {
            AddImportDiagnosticEXT(
                destination, "texture-source-unsupported",
                GltfImportDiagnosticKindEXT::UnsupportedFeature,
                "Texture maps whose encoded source CNA cannot decode were not applied.",
                mesh.unsupportedTextureSourcesEXT.size(), 0.0, subject,
                mesh.unsupportedTextureSourcesEXT);
        }
        if (!mesh.mipmappedSamplerMapsWithoutMipChainEXT.empty())
        {
            AddImportDiagnosticEXT(
                destination, "mip-chain-missing", GltfImportDiagnosticKindEXT::Approximation,
                "The maps request mip filtering, but imported PNG/JPEG textures contain only "
                "level zero.", mesh.mipmappedSamplerMapsWithoutMipChainEXT.size(), 0.0, subject,
                mesh.mipmappedSamplerMapsWithoutMipChainEXT);
        }
        if (mesh.extraInfluenceSetsEXT > 0)
        {
            AddImportDiagnosticEXT(
                destination, "influence-sets-dropped", GltfImportDiagnosticKindEXT::DroppedData,
                "Joint influence sets beyond JOINTS_0/WEIGHTS_0 were dropped.",
                mesh.extraInfluenceSetsEXT, mesh.worstDroppedInfluenceEXT, subject);
        }
        if (mesh.generatedNormalsEXT)
        {
            AddImportDiagnosticEXT(
                destination, "normals-generated", GltfImportDiagnosticKindEXT::GeneratedData,
                "The primitive authored no NORMAL, so CNA generated normals.", 1, 0.0, subject);
        }
        if (mesh.smoothedNormalVertexCountEXT > 0)
        {
            AddImportDiagnosticEXT(
                destination, "generated-normals-smoothed",
                GltfImportDiagnosticKindEXT::Approximation,
                "Generated normals on vertices shared by differently oriented faces were "
                "averaged instead of duplicating vertices for exact flat shading.",
                mesh.smoothedNormalVertexCountEXT, 0.0, subject);
        }
        if (mesh.renormalisedWeightVertexCountEXT > 0)
        {
            AddImportDiagnosticEXT(
                destination, "weights-renormalised", GltfImportDiagnosticKindEXT::Approximation,
                "Joint weights that did not sum to one were renormalised.",
                mesh.renormalisedWeightVertexCountEXT, mesh.worstWeightSumDeviationEXT, subject);
        }
        if (mesh.zeroWeightVertexCountEXT > 0)
        {
            AddImportDiagnosticEXT(
                destination, "zero-weight-vertices", GltfImportDiagnosticKindEXT::DroppedData,
                "Vertices whose joint weights sum to zero remain unweighted rather than being "
                "assigned to an arbitrary joint.", mesh.zeroWeightVertexCountEXT, 0.0, subject);
        }
        if (mesh.transmissionApproximatedEXT)
        {
            AddImportDiagnosticEXT(
                destination, "transmission-approximated",
                GltfImportDiagnosticKindEXT::Approximation,
                "KHR_materials_transmission was approximated as alpha blending; refraction, "
                "roughness blur and physical energy behaviour are not represented.",
                1, mesh.transmissionFactorEXT, subject);
        }
        if (mesh.transmissionHasTextureEXT)
        {
            AddImportDiagnosticEXT(
                destination, "transmission-texture-dropped",
                GltfImportDiagnosticKindEXT::DroppedData,
                "The per-texel transmission texture cannot be represented by the scalar alpha "
                "approximation and was dropped.", 1, 0.0, subject);
        }
        if (!mesh.uvSetMismatchedMapsEXT.empty())
        {
            AddImportDiagnosticEXT(
                destination, "uv-set-mismatch", GltfImportDiagnosticKindEXT::DroppedData,
                "These maps need a third distinct TEXCOORD set beyond CNA's two carried channels "
                "and fall back to the first.",
                mesh.uvSetMismatchedMapsEXT.size(), 0.0, subject,
                mesh.uvSetMismatchedMapsEXT);
        }
        if (mesh.extraColorSetsEXT > 0)
        {
            AddImportDiagnosticEXT(
                destination, "color-sets-dropped", GltfImportDiagnosticKindEXT::DroppedData,
                "Colour attribute sets beyond COLOR_0 were dropped.",
                NonNegativeSizeEXT(mesh.extraColorSetsEXT), 0.0, subject);
        }
        if (!mesh.ignoredCustomAttributesEXT.empty())
        {
            AddImportDiagnosticEXT(
                destination, "custom-attributes-ignored", GltfImportDiagnosticKindEXT::DroppedData,
                "Application-specific vertex attributes were ignored as glTF permits.",
                mesh.ignoredCustomAttributesEXT.size(), 0.0, subject,
                mesh.ignoredCustomAttributesEXT);
        }
        if (mesh.sourceTopology != mesh.topology)
        {
            AddImportDiagnosticEXT(
                destination, "topology-converted", GltfImportDiagnosticKindEXT::Information,
                "The primitive topology was converted exactly and its index list was rewritten.",
                1, 0.0, subject,
                {PrimitiveTopologyName(mesh.sourceTopology), PrimitiveTopologyName(mesh.topology)});
        }
    }

    void AppendGltfMorphReportEXT(GltfImportReportEXT& destination,
                                  const MorphReportEXT& source,
                                  const std::string& subject, bool normalMapped)
    {
        if (source.targetCount <= 0) { return; }
        AddImportDiagnosticEXT(
            destination, "morph-target-summary", GltfImportDiagnosticKindEXT::Information,
            source.hasNonZeroDefaultWeights
                ? "Morph targets were imported and non-zero default weights were applied."
                : "Morph targets were imported with zero default weights.",
            NonNegativeSizeEXT(source.targetCount), 0.0, subject);
        if (normalMapped && source.targetsWithoutTangents == source.targetCount &&
            source.targetsWithoutPositions < source.targetCount)
        {
            AddImportDiagnosticEXT(
                destination, "morph-tangents-missing",
                GltfImportDiagnosticKindEXT::DroppedData,
                "The normal-mapped primitive morphs positions without tangent deltas, so the "
                "deformed surface keeps its rest-pose tangent basis.",
                NonNegativeSizeEXT(source.targetsWithoutTangents), 0.0, subject);
        }
    }

    void AppendGltfLightReportEXT(GltfImportReportEXT& destination,
                                  const LightReportEXT& source,
                                  std::size_t importedLightCount)
    {
        destination.ImportedLightCount = importedLightCount;
        AddImportDiagnosticEXT(
            destination, "lights-dropped", GltfImportDiagnosticKindEXT::DroppedData,
            "Lights beyond the three slots provided by XNA stock effects were dropped.",
            source.droppedLightCount);
        AddImportDiagnosticEXT(
            destination, "point-lights-approximated",
            GltfImportDiagnosticKindEXT::Approximation,
            "Point lights were approximated as directional lights aimed at the scene origin.",
            source.approximatedPointLightCount);
        AddImportDiagnosticEXT(
            destination, "spot-lights-approximated",
            GltfImportDiagnosticKindEXT::Approximation,
            "Spot lights were approximated as directional lights aimed at the scene origin.",
            source.approximatedSpotLightCount);
        AddImportDiagnosticEXT(
            destination, "light-intensity-clamped",
            GltfImportDiagnosticKindEXT::Approximation,
            "Photometric light colour times intensity exceeded one and was clamped.",
            source.clampedIntensityLightCount, source.worstPreClampChannelEXT);
        AddImportDiagnosticEXT(
            destination, "light-range-ignored", GltfImportDiagnosticKindEXT::DroppedData,
            "Finite point/spot light ranges were ignored by the directional-light approximation.",
            source.ignoredRangeCount);
        AddImportDiagnosticEXT(
            destination, "spot-cone-ignored", GltfImportDiagnosticKindEXT::DroppedData,
            "Spot-light cone angles were ignored by the directional-light approximation.",
            source.ignoredConeAngleCount);
    }

    void AppendGltfAnimationReportEXT(GltfImportReportEXT& destination,
                                      const AnimationReportEXT& source)
    {
        destination.AnimationCount = std::max(
            destination.AnimationCount, NonNegativeSizeEXT(source.animationCount));
        destination.ClipCount += NonNegativeSizeEXT(source.clipCount);
        AddImportDiagnosticEXT(
            destination, "animations-empty", GltfImportDiagnosticKindEXT::DroppedData,
            "Animations that resolved to no supported in-scene track produced no clip.",
            NonNegativeSizeEXT(source.emptyAnimationCount));
        AddImportDiagnosticEXT(
            destination, "animation-targets-unavailable",
            GltfImportDiagnosticKindEXT::DroppedData,
            "Animation channels whose target node is absent from this Model's scene-node or "
            "joint-palette target space were skipped.",
            NonNegativeSizeEXT(source.skippedOutOfSceneChannels));
        AddImportDiagnosticEXT(
            destination, "animation-paths-unsupported",
            GltfImportDiagnosticKindEXT::UnsupportedFeature,
            "Animation channels whose target path CNA cannot drive were skipped.",
            NonNegativeSizeEXT(source.skippedUnsupportedPathChannels));
        AddImportDiagnosticEXT(
            destination, "animation-tracks-resampled",
            GltfImportDiagnosticKindEXT::Approximation,
            "Animation tracks were resampled onto the union of sibling channel key times.",
            NonNegativeSizeEXT(source.resampledTrackCount));
        AddImportDiagnosticEXT(
            destination, "animation-input-times-duplicated",
            GltfImportDiagnosticKindEXT::Approximation,
            "Adjacent animation input samples repeat a time; the hard cut is retained with one "
            "value at that instant.", NonNegativeSizeEXT(source.duplicateInputTimeCount));
    }

    void AppendGltfRigidAnimationDropEXT(GltfImportReportEXT& destination,
                                         const std::string& clipName,
                                         std::size_t droppedTrackCount)
    {
        AddImportDiagnosticEXT(
            destination, "rigid-animation-dropped-on-skinned-model",
            GltfImportDiagnosticKindEXT::DroppedData,
            "Scene-node animation tracks that are not carried by this Model's skin palettes "
            "cannot be retained because Model::Tag already contains SkinningData.",
            droppedTrackCount, 0.0, clipName);
    }

    std::size_t CountGltfRigidAnimationDropsEXT(
        const ClipOut& clip, const SceneGraphOut& scene,
        const std::vector<const SkeletonResult*>& skins)
    {
        return static_cast<std::size_t>(std::count_if(
            clip.tracks.begin(), clip.tracks.end(),
            [&](const TrackOut& track)
            {
                if (track.boneIndex < 0 ||
                    static_cast<std::size_t>(track.boneIndex) >= scene.nodes.size())
                {
                    return true;
                }
                const cgltf_node* node =
                    scene.nodes[static_cast<std::size_t>(track.boneIndex)].node;
                return std::none_of(
                    skins.begin(), skins.end(),
                    [&](const SkeletonResult* skin)
                    {
                        return skin != nullptr &&
                            skin->nodeToNewIndex.find(node) != skin->nodeToNewIndex.end();
                    });
            }));
    }

    SkeletonResult BuildSkeleton(const cgltf_skin* skin, float unitScale)
    {
        // No scene graph available: every root joint keeps an identity prefix, which is only
        // correct when the joint set already reaches the scene root and the mesh node is
        // untransformed. Callers that can build the graph must use the four-argument overload --
        // both model loaders do (plan_gltf.md GLTF-245).
        return BuildSkeleton(skin, SceneGraphOut{}, Matrix::getIdentityProperty(), unitScale);
    }

    SkeletonResult BuildSkeleton(const cgltf_skin* skin, const SceneGraphOut& scene,
                                  const Matrix& meshNodeWorld, float unitScale)
    {
        SkeletonResult result;

        // GLTF-249: read the declared root, and read it ONLY to report it. §15.1.1 makes the rule
        // explicit -- skin.skeleton names the rig's semantic root, and an implementation that used
        // it to stop walking ancestors would recreate D8 under a new name. Nothing below consults
        // these two fields; the ancestry that feeds parentWorldPrefix comes from the scene graph.
        if (skin->skeleton != nullptr)
        {
            result.declaredSkeletonRootName =
                skin->skeleton->name != nullptr ? skin->skeleton->name : "";
            const auto placed = scene.indexOfNode.find(skin->skeleton);
            if (placed != scene.indexOfNode.end())
            {
                result.declaredSkeletonRootNodeIndex = placed->second;
            }
        }

        const std::size_t n = skin->joints_count;
        // GLTF-252: these are the explicit bridge between the stable scene-node identity space
        // and this skin's private GPU-palette space. They stay populated independently of
        // oldToNew, whose input is the file-local skin.joints[] order rather than a scene index.
        result.sceneNodeIndexToPaletteIndex.assign(scene.nodes.size(), -1);
        result.paletteIndexToSceneNodeIndex.assign(n, -1);
        if (n == 0) { return result; }

        // plan_gltf.md GLTF-261. The GPU palette is MaxBones (72) mat4s, a real XNA constant that
        // every renderer's uniform array is sized by, so raising it is not a local change. A rig
        // above the limit is therefore refused -- clearly, here, naming the file's own joint count
        // and the limit -- rather than left to surface later as SetBoneTransforms' generic
        // "boneTransforms exceeds MaxBones", which says nothing about which asset caused it.
        //
        // Refusing rather than truncating is the point: truncating leaves the joints past the limit
        // with an identity matrix, so every vertex bound to them collapses toward the origin. That
        // is the audit's H6, and it is exactly the class of silent wrongness this campaign removes.
        constexpr std::size_t kMaxPaletteBones = 72;
        if (n > kMaxPaletteBones)
        {
            throw std::runtime_error(
                "Skin '" + std::string(skin->name != nullptr ? skin->name : "<unnamed>") +
                "' has " + std::to_string(n) + " joints, but the GPU bone palette holds " +
                std::to_string(kMaxPaletteBones) +
                ". Truncating would leave every vertex bound to a joint past the limit collapsed "
                "toward the origin, so the skin is refused instead (plan_gltf.md GLTF-261).");
        }

        std::vector<const cgltf_node*> oldNodes(n);
        for (std::size_t i = 0; i < n; ++i) { oldNodes[i] = skin->joints[i]; }

        std::unordered_map<const cgltf_node*, int> nodeToOldIndex;
        nodeToOldIndex.reserve(n);
        for (std::size_t i = 0; i < n; ++i) { nodeToOldIndex[oldNodes[i]] = static_cast<int>(i); }

        std::vector<int> oldParentOfOld(n, -1);
        for (std::size_t i = 0; i < n; ++i)
        {
            const cgltf_node* p = oldNodes[i]->parent;
            auto it = nodeToOldIndex.find(p);
            oldParentOfOld[i] = (it != nodeToOldIndex.end()) ? it->second : -1;
        }

        std::vector<std::vector<int>> childrenOfOld(n);
        std::vector<int> queue;
        for (std::size_t i = 0; i < n; ++i)
        {
            if (oldParentOfOld[i] == -1) { queue.push_back(static_cast<int>(i)); }
            else { childrenOfOld[static_cast<std::size_t>(oldParentOfOld[i])].push_back(static_cast<int>(i)); }
        }

        std::vector<int> newOrderOldIndices;
        newOrderOldIndices.reserve(n);
        for (std::size_t qi = 0; qi < queue.size(); ++qi)
        {
            const int oldIdx = queue[qi];
            newOrderOldIndices.push_back(oldIdx);
            for (int c : childrenOfOld[static_cast<std::size_t>(oldIdx)]) { queue.push_back(c); }
        }
        if (newOrderOldIndices.size() != n)
        {
            throw std::runtime_error(
                "Skin joint hierarchy is inconsistent (a joint's parent chain does not resolve to "
                "a root within the skin's own joint set).");
        }

        result.oldToNew.assign(n, -1);
        for (std::size_t newIdx = 0; newIdx < n; ++newIdx)
        {
            result.oldToNew[static_cast<std::size_t>(newOrderOldIndices[newIdx])] = static_cast<int>(newIdx);
        }

        const std::vector<float> ibm = skin->inverse_bind_matrices
            // The joint matrix this feeds -- and the two conventions it has to reconcile, glTF's
            // column-vector one and XNA's row-vector one -- is stated once in
            // docs/gltf-conventions.md ("The joint matrix, in both conventions"). Read it before
            // changing any multiplication order here: every term is individually plausible.
            ? UnpackAccessor(skin->inverse_bind_matrices, 16, "inverseBindMatrices")
            : std::vector<float>();

        result.bones.resize(n);
        for (std::size_t newIdx = 0; newIdx < n; ++newIdx)
        {
            const int oldIdx = newOrderOldIndices[newIdx];
            const cgltf_node* node = oldNodes[static_cast<std::size_t>(oldIdx)];

            BoneOut bone;
            bone.name = node->name ? node->name : ("Bone" + std::to_string(newIdx));
            const int oldParent = oldParentOfOld[static_cast<std::size_t>(oldIdx)];
            bone.parentIndex = (oldParent == -1) ? -1 : result.oldToNew[static_cast<std::size_t>(oldParent)];

            float localMat[16];
            cgltf_node_transform_local(node, localMat);
            bone.bindPoseLocal = ScaleTranslation(ConvertGltfMatrix(localMat), unitScale);

            // GLTF-245/GLTF-247: a root joint -- one whose parent is not itself in this skin's
            // joint set -- carries the two terms that live above the joint set. Everything the
            // ancestry contributes is real scene data; skin.skeleton does not truncate it, and an
            // ancestor that is not a joint contributes exactly as much as one that is.
            if (bone.parentIndex == -1)
            {
                Matrix ancestorWorld = Matrix::getIdentityProperty();
                if (const cgltf_node* parent = node->parent)
                {
                    const auto placed = scene.indexOfNode.find(parent);
                    if (placed != scene.indexOfNode.end())
                    {
                        ancestorWorld = scene.nodes[static_cast<std::size_t>(placed->second)].worldTransform;
                    }
                    else
                    {
                        // The joint hangs off a node outside the default scene. cgltf can still
                        // compose the chain, so use it rather than silently dropping the ancestry
                        // -- dropping it is exactly D8.
                        float parentWorld[16];
                        cgltf_node_transform_world(parent, parentWorld);
                        ancestorWorld = ConvertGltfMatrix(parentWorld);
                    }
                }
                // Ancestor translations are unit-scaled for the same reason bone translations are:
                // the correction has to apply uniformly across every position-derived quantity, or
                // an animated bone would jump between two different unit spaces mid-clip.
                ancestorWorld = ScaleTranslation(ancestorWorld, unitScale);
                bone.parentWorldPrefix =
                    ancestorWorld * Matrix::Invert(ScaleTranslation(meshNodeWorld, unitScale));
            }

            if (!ibm.empty())
            {
                const float* m = ibm.data() + static_cast<std::size_t>(oldIdx) * 16;
                bone.inverseBindGlobal = ScaleTranslation(ConvertGltfMatrix(m), unitScale);
            }

            result.bones[newIdx] = bone;
            result.nodeToNewIndex[node] = static_cast<int>(newIdx);

            const auto sceneNode = scene.indexOfNode.find(node);
            if (sceneNode != scene.indexOfNode.end())
            {
                const int sceneNodeIndex = sceneNode->second;
                if (sceneNodeIndex < 0 ||
                    static_cast<std::size_t>(sceneNodeIndex) >=
                        result.sceneNodeIndexToPaletteIndex.size())
                {
                    throw std::runtime_error(
                        "The scene graph returned an out-of-range sceneNodeIndex for joint '" +
                        bone.name + "'.");
                }
                int& paletteIndex = result.sceneNodeIndexToPaletteIndex[
                    static_cast<std::size_t>(sceneNodeIndex)];
                if (paletteIndex != -1)
                {
                    throw std::runtime_error(
                        "Skin '" + std::string(skin->name != nullptr ? skin->name : "<unnamed>") +
                        "' lists scene node '" + bone.name + "' as a joint more than once.");
                }
                paletteIndex = static_cast<int>(newIdx);
                result.paletteIndexToSceneNodeIndex[newIdx] = sceneNodeIndex;
            }
        }

        return result;
    }

    namespace
    {
        /// One clip's channels, already grouped by the bone they drive.
        struct BoneChannels
        {
            std::optional<SampledChannel> translation, rotation, scale;
        };

        /// Resolves one animation's channels against a caller-supplied node -> bone mapping.
        ///
        /// Shared by the joint-palette and scene-node extractors so the two cannot drift: the ONLY
        /// difference between rigid and skinned animation is which index space a target node
        /// resolves into (§15.1.2). Everything after this -- union resampling, bind-pose fallback,
        /// unit scaling -- is identical, and D6 existed precisely because the two were never
        /// separated in the first place.
        ///
        /// @param resolve Maps a channel's target node to a bone index, or -1 to skip it.
        /// @param skippedTargets Incremented for each channel whose target `resolve` rejected.
        /// @param clipName Names the owning clip in any diagnostic a loaded channel raises.
        /// @param duplicateTimes Accumulates every channel's equal-adjacent-input count.
        std::unordered_map<int, BoneChannels> GatherChannels(
            const cgltf_animation& anim, float unitScale,
            const std::function<int(const cgltf_node*)>& resolve, double& maxTime,
            std::size_t& unsupportedPaths, std::size_t& skippedTargets,
            const std::string& clipName, int& duplicateTimes)
        {
            std::unordered_map<int, BoneChannels> byBone;
            for (cgltf_size c = 0; c < anim.channels_count; ++c)
            {
                const cgltf_animation_channel& ch = anim.channels[c];
                const std::string context =
                    "clip '" + clipName + "' channel " + std::to_string(c);
                const int boneIdx = resolve(ch.target_node);
                if (boneIdx < 0) { ++skippedTargets; continue; }

                if (ch.target_path == cgltf_animation_path_type_translation)
                {
                    byBone[boneIdx].translation = LoadChannel(ch, 3, context + " (translation)");
                    // Translation values (and, for CUBICSPLINE, their in/out tangents -- both are
                    // position-derived quantities) must track the same unit-scale correction
                    // already applied to bind-pose translations, or an animated bone would jump
                    // back to unscaled-space offsets mid-clip.
                    for (float& component : byBone[boneIdx].translation->values) { component *= unitScale; }
                    duplicateTimes += byBone[boneIdx].translation->duplicateTimes;
                }
                else if (ch.target_path == cgltf_animation_path_type_rotation)
                {
                    byBone[boneIdx].rotation = LoadChannel(ch, 4, context + " (rotation)");
                    duplicateTimes += byBone[boneIdx].rotation->duplicateTimes;
                }
                else if (ch.target_path == cgltf_animation_path_type_scale)
                {
                    byBone[boneIdx].scale = LoadChannel(ch, 3, context + " (scale)");
                    duplicateTimes += byBone[boneIdx].scale->duplicateTimes;
                }
                else { ++unsupportedPaths; continue; } // e.g. morph target weights

                if (ch.sampler->input->count > 0)
                {
                    const std::vector<float> t = UnpackAccessor(ch.sampler->input, 1, "sampler input");
                    maxTime = std::max(maxTime, static_cast<double>(t.back()));
                }
            }
            return byBone;
        }

        /// The longest single source channel of one bone, in samples. A track with more keys than
        /// this was genuinely resampled onto a union of disagreeing key times (`GLTF-315`); a track
        /// with exactly this many passed its source through.
        std::size_t LongestSourceChannel(const BoneChannels& channels)
        {
            std::size_t longest = 0;
            for (const auto* ch : {&channels.translation, &channels.rotation, &channels.scale})
            {
                if (*ch) { longest = std::max(longest, ch->value().times.size()); }
            }
            return longest;
        }

        /// Resamples one bone's channels onto the union of their key times, filling each missing
        /// component from the bone's own bind pose.
        std::optional<TrackOut> BuildTrack(int boneIdx, const BoneChannels& channels,
                                           const Matrix& bindPoseLocal)
        {
            std::vector<double> unionTimes;
            for (const auto* ch : {&channels.translation, &channels.rotation, &channels.scale})
            {
                if (*ch) { unionTimes.insert(unionTimes.end(), ch->value().times.begin(), ch->value().times.end()); }
            }
            if (unionTimes.empty()) { return std::nullopt; }
            std::sort(unionTimes.begin(), unionTimes.end());
            unionTimes.erase(
                std::unique(unionTimes.begin(), unionTimes.end(),
                            [](double x, double y) { return std::fabs(x - y) < 1e-9; }),
                unionTimes.end());

            Vector3 bindScale{1.0f, 1.0f, 1.0f};
            Quaternion bindRotation = Quaternion::Identity;
            Vector3 bindTranslation;
            (void)bindPoseLocal.Decompose(bindScale, bindRotation, bindTranslation);

            TrackOut track;
            track.boneIndex = boneIdx;
            track.keys.reserve(unionTimes.size());
            for (double t : unionTimes)
            {
                KeyframeOut key;
                key.time = t;
                key.translation = EvaluateVec3Channel(channels.translation ? &*channels.translation : nullptr, t, bindTranslation);
                key.rotation = EvaluateQuatChannel(channels.rotation ? &*channels.rotation : nullptr, t, bindRotation);
                key.scale = EvaluateVec3Channel(channels.scale ? &*channels.scale : nullptr, t, bindScale);
                track.keys.push_back(key);
            }
            return track;
        }
    }

    std::vector<ClipOut> ExtractSceneNodeClips(const cgltf_data* data, const SceneGraphOut& scene,
                                                float unitScale,
                                                std::vector<std::string>& warnings,
                                                AnimationReportEXT* report)
    {
        // GLTF-293: rigid (non-joint) node animation. Before the real ModelBone hierarchy existed
        // there was nothing for such a channel to drive, which is why D6 waited on GLTF-103/113/114
        // rather than on the animation layer.
        std::vector<ClipOut> clips;
        if (data == nullptr) { return clips; }
        if (report != nullptr) { *report = AnimationReportEXT{}; }

        for (cgltf_size a = 0; a < data->animations_count; ++a)
        {
            const cgltf_animation& anim = data->animations[a];
            const std::string clipName = anim.name ? anim.name : ("Clip" + std::to_string(a));

            double maxTime = 0.0;
            std::size_t unsupportedPaths = 0;
            std::size_t skippedTargets = 0;
            int duplicateTimes = 0;
            const std::unordered_map<int, BoneChannels> byBone = GatherChannels(
                anim, unitScale,
                [&scene](const cgltf_node* node) {
                    const auto it = scene.indexOfNode.find(node);
                    return it == scene.indexOfNode.end() ? -1 : it->second;
                },
                maxTime, unsupportedPaths, skippedTargets, clipName, duplicateTimes);

            if (unsupportedPaths > 0)
            {
                warnings.push_back(
                    "Clip '" + clipName + "' has " + std::to_string(unsupportedPaths) +
                    " channel(s) on a path this tool does not import (e.g. morph target "
                    "weights) -- skipped.");
            }
            if (skippedTargets > 0)
            {
                // Never silent: a dropped channel is exactly what D6 was.
                warnings.push_back(
                    "Clip '" + clipName + "' has " + std::to_string(skippedTargets) +
                    " channel(s) whose target node is not in the default scene -- they drive "
                    "nothing that was imported, and are skipped.");
            }

            ClipOut clip;
            clip.name = clipName;
            clip.duration = maxTime;
            clip.targetSpace = ClipTargetSpace::SceneNode;
            int resampledTracks = 0;
            for (const auto& [boneIdx, channels] : byBone)
            {
                const Matrix& bindPose =
                    scene.nodes[static_cast<std::size_t>(boneIdx)].localTransform;
                if (std::optional<TrackOut> track = BuildTrack(boneIdx, channels, bindPose))
                {
                    if (track->keys.size() > LongestSourceChannel(channels)) { ++resampledTracks; }
                    clip.tracks.push_back(std::move(*track));
                }
            }

            if (report != nullptr)
            {
                ++report->animationCount;
                report->channelCount += static_cast<int>(anim.channels_count);
                report->skippedOutOfSceneChannels += static_cast<int>(skippedTargets);
                report->skippedUnsupportedPathChannels += static_cast<int>(unsupportedPaths);
                report->duplicateInputTimeCount += duplicateTimes;
                report->trackCount += static_cast<int>(clip.tracks.size());
                report->resampledTrackCount += resampledTracks;
                if (clip.tracks.empty()) { ++report->emptyAnimationCount; }
                else
                {
                    ++report->clipCount;
                    report->longestClipDuration =
                        std::max(report->longestClipDuration, clip.duration);
                }
            }
            // Deterministic order: an unordered_map's iteration order is not a property anyone
            // should be able to observe in a committed .cnj or a test expectation.
            std::sort(clip.tracks.begin(), clip.tracks.end(),
                      [](const TrackOut& l, const TrackOut& r) { return l.boneIndex < r.boneIndex; });

            // An animation that drives nothing imported is not a clip. Emitting an empty one would
            // put an "animations" key in the .cnj that plays nothing, which is a different lie
            // from D6's but a lie all the same -- the warning above already reported why.
            if (!clip.tracks.empty()) { clips.push_back(std::move(clip)); }
        }

        return clips;
    }

    std::vector<ClipOut> ExtractClips(const cgltf_data* data, const SkeletonResult& skel,
                                       float unitScale, std::vector<std::string>& warnings,
                                       AnimationReportEXT* report)
    {
        std::vector<ClipOut> clips;
        if (report != nullptr) { *report = AnimationReportEXT{}; }

        for (cgltf_size a = 0; a < data->animations_count; ++a)
        {
            const cgltf_animation& anim = data->animations[a];
            const std::string clipName = anim.name ? anim.name : ("Clip" + std::to_string(a));

            double maxTime = 0.0;
            std::size_t unsupportedPaths = 0;
            std::size_t skippedTargets = 0;
            int duplicateTimes = 0;
            // The same gatherer the scene-node path uses -- the only difference between the two is
            // which index space a target node resolves into (§15.1.2), and keeping the resolution
            // as the sole parameter is what stops the joint and rigid readers drifting apart.
            const std::unordered_map<int, BoneChannels> byBone = GatherChannels(
                anim, unitScale,
                [&skel](const cgltf_node* node) {
                    const auto it = skel.nodeToNewIndex.find(node);
                    return it == skel.nodeToNewIndex.end() ? -1 : it->second;
                },
                maxTime, unsupportedPaths, skippedTargets, clipName, duplicateTimes);

            if (unsupportedPaths > 0)
            {
                warnings.push_back(
                    "Clip '" + clipName + "' has " + std::to_string(unsupportedPaths) +
                    " channel(s) on a path this tool does not import (e.g. morph target "
                    "weights) -- skipped.");
            }
            if (skippedTargets > 0)
            {
                warnings.push_back(
                    "Clip '" + clipName + "' has " + std::to_string(skippedTargets) +
                    " channel(s) whose target node is not a joint of this skin -- they drive "
                    "nothing in this palette, and are skipped. Rigid node animation is imported "
                    "separately (GLTF-293).");
            }

            ClipOut clip;
            clip.name = clipName;
            clip.duration = maxTime;
            int resampledTracks = 0;

            for (const auto& [boneIdx, channels] : byBone)
            {
                // Same resampling as the scene-node path -- shared so the two cannot diverge.
                if (std::optional<TrackOut> track = BuildTrack(
                        boneIdx, channels, skel.bones[static_cast<std::size_t>(boneIdx)].bindPoseLocal))
                {
                    if (track->keys.size() > LongestSourceChannel(channels)) { ++resampledTracks; }
                    clip.tracks.push_back(std::move(*track));
                }
            }
            // Deterministic order, for the same reason the scene-node path sorts: byBone is an
            // unordered_map, so without this the track order of a skinned clip is a rehash away
            // from changing, and `GLTF-314` asks the two loaders to agree track-for-track.
            std::sort(clip.tracks.begin(), clip.tracks.end(),
                      [](const TrackOut& l, const TrackOut& r) { return l.boneIndex < r.boneIndex; });

            if (report != nullptr)
            {
                ++report->animationCount;
                report->channelCount += static_cast<int>(anim.channels_count);
                report->skippedOutOfSceneChannels += static_cast<int>(skippedTargets);
                report->skippedUnsupportedPathChannels += static_cast<int>(unsupportedPaths);
                report->duplicateInputTimeCount += duplicateTimes;
                report->trackCount += static_cast<int>(clip.tracks.size());
                report->resampledTrackCount += resampledTracks;
                ++report->clipCount;
                if (clip.tracks.empty()) { ++report->emptyAnimationCount; }
                report->longestClipDuration =
                    std::max(report->longestClipDuration, clip.duration);
            }

            // Unlike the scene-node path, a trackless clip is still emitted here. The two are not
            // inconsistent: a skinned model's clips are selected BY NAME (GLTF-306), so dropping
            // one silently renames every clip after it from the application's point of view,
            // whereas a rigid clip driving nothing would put an "animations" key on a model that
            // has no animated bone at all. The warning above says which channels were lost.
            clips.push_back(std::move(clip));
        }

        return clips;
    }

    // plan_gltf.md GLTF-199: what the bytes ARE, not what the file says they are. A `data:` URI's
    // media type and an `image` object's `mimeType` are both author-supplied strings, and an
    // exporter that writes `image/png` above JPEG bytes is not hypothetical -- it is what a
    // "convert to PNG" step that failed silently produces. The extension travels with the bytes
    // (the offline path writes a file with it, and a `.cnj` then names that file), so a wrong one
    // hands a consumer a file whose contents do not match its name.
    //
    // Signature-based, with the declared type as a hint only: PNG's 8-byte signature and JPEG's
    // SOI marker are both unambiguous, and anything else falls back to what the file claimed.
    std::string SniffImageExtension(const std::vector<std::uint8_t>& bytes, const std::string& hint)
    {
        static const std::uint8_t kPng[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
        if (bytes.size() >= sizeof(kPng) &&
            std::equal(std::begin(kPng), std::end(kPng), bytes.begin()))
        {
            return "png";
        }
        if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
        {
            return "jpg";
        }
        return hint.empty() ? "png" : hint;
    }

    std::optional<ExtractedImage> ExtractImage(const cgltf_image* image, const std::filesystem::path& gltfDir)
    {
        if (!image) { return std::nullopt; }

        std::string ext;
        if (image->mime_type)
        {
            const std::string mt = image->mime_type;
            if (mt == "image/jpeg") { ext = "jpg"; }
            else if (mt == "image/png") { ext = "png"; }
        }

        if (image->buffer_view)
        {
            const std::uint8_t* data = cgltf_buffer_view_data(image->buffer_view);
            if (!data) { throw std::runtime_error("Failed to read embedded image bufferView."); }
            ExtractedImage result;
            result.bytes.assign(data, data + image->buffer_view->size);
            result.extension = SniffImageExtension(result.bytes, ext);
            return result;
        }

        if (image->uri)
        {
            const std::string uri = image->uri;

            if (uri.rfind("data:", 0) == 0)
            {
                const auto comma = uri.find(',');
                if (comma == std::string::npos) { throw std::runtime_error("Malformed data: URI for image."); }
                if (ext.empty() && uri.find("image/jpeg") != std::string::npos) { ext = "jpg"; }

                const std::string b64 = uri.substr(comma + 1);
                std::size_t padding = 0;
                if (!b64.empty() && b64.back() == '=') { ++padding; }
                if (b64.size() > 1 && b64[b64.size() - 2] == '=') { ++padding; }
                const std::size_t decodedSize = b64.size() >= 4 ? (b64.size() / 4) * 3 - padding : 0;

                cgltf_options opts{};
                void* decoded = nullptr;
                const cgltf_result r = cgltf_load_buffer_base64(&opts, decodedSize, b64.c_str(), &decoded);
                if (r != cgltf_result_success || !decoded)
                {
                    throw std::runtime_error("Failed to decode base64 image data: URI.");
                }
                ExtractedImage result;
                result.bytes.assign(static_cast<std::uint8_t*>(decoded), static_cast<std::uint8_t*>(decoded) + decodedSize);
                result.extension = SniffImageExtension(result.bytes, ext);
                std::free(decoded);
                return result;
            }

            // plan_gltf.md GLTF-198: percent-decoding and containment together, so an image URI
            // cannot reach outside the asset's directory. ContentManager already refused this file
            // before a byte was read; this is the second gate, for the offline tool and for any
            // caller that reaches ExtractImage without the up-front sweep.
            const std::filesystem::path imgPath = ResolveExternalUriEXT(gltfDir, uri, "image");
            std::ifstream f(imgPath, std::ios::binary);
            if (!f) { throw std::runtime_error("Cannot open external image file: " + imgPath.string()); }
            ExtractedImage result;
            result.bytes.assign(std::istreambuf_iterator<char>(f), {});
            if (ext.empty())
            {
                std::string realExt = imgPath.extension().string();
                if (!realExt.empty() && realExt.front() == '.') { realExt = realExt.substr(1); }
                ext = realExt;
            }
            // The filename's own extension is a hint too, and a weaker one than the bytes: a
            // texture called `.png` that is really a JPEG is the most common form this takes.
            result.extension = SniffImageExtension(result.bytes, ext);
            return result;
        }

        return std::nullopt;
    }

    std::optional<ExtractedImage> RemapOcclusionImageForDualTextureEXT(const ExtractedImage& image)
    {
        int width = 0, height = 0, channelsInFile = 0;
        stbi_uc* pixels = stbi_load_from_memory(
            image.bytes.data(), static_cast<int>(image.bytes.size()),
            &width, &height, &channelsInFile, 4);
        if (!pixels) { return std::nullopt; }

        const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        for (std::size_t i = 0; i < pixelCount; ++i)
        {
            stbi_uc* p = pixels + i * 4;
            // RGB halved (DualTextureEffect's own "0.5 = neutral" blend convention); alpha (p[3])
            // left unchanged -- glTF occlusion textures have no meaningful alpha channel anyway.
            p[0] = static_cast<stbi_uc>(p[0] / 2);
            p[1] = static_cast<stbi_uc>(p[1] / 2);
            p[2] = static_cast<stbi_uc>(p[2] / 2);
        }

        std::vector<std::uint8_t> encoded;
        const auto writeCallback = [](void* context, void* data, int size)
        {
            auto* out = static_cast<std::vector<std::uint8_t>*>(context);
            const auto* bytes = static_cast<std::uint8_t*>(data);
            out->insert(out->end(), bytes, bytes + size);
        };
        const int ok = stbi_write_png_to_func(writeCallback, &encoded, width, height, 4, pixels, width * 4);
        stbi_image_free(pixels);
        if (!ok) { return std::nullopt; }

        ExtractedImage result;
        result.bytes = std::move(encoded);
        result.extension = "png";
        return result;
    }

    // plan_gltf.md GLTF-200/GLTF-350: the image a texture's pixels actually come from, and -- when
    // there is none CNA can read -- which extension took it away.
    //
    // `KHR_texture_basisu` and `EXT_texture_webp` both attach their own image to the texture and
    // both are specified so the texture MAY also keep a plain PNG/JPEG `source` as a fallback. That
    // fallback is exactly `texture->image`, so preferring it is not a compromise: it is what the
    // extensions tell a reader without a decoder to do. What was silent before is the other case --
    // no fallback authored -- where this returned nullptr and every downstream check simply read
    // "no texture on this slot".
    //
    // A mime type CNA cannot decode is the same loss wearing different clothes: the image is
    // present and readable as bytes, and stb_image will refuse it. Caught here, where the map's
    // name is still known, rather than at a decode failure that cannot say which map it was.
    const cgltf_image* ImageForTexture(const cgltf_texture* texture, const char* mapName,
                                       std::vector<std::string>* unsupportedOut)
    {
        if (texture == nullptr) { return nullptr; }

        const auto report = [&](const char* reason) {
            if (unsupportedOut != nullptr)
            {
                unsupportedOut->emplace_back(std::string(mapName) + ": " + reason);
            }
        };

        if (texture->image != nullptr)
        {
            const char* mime = texture->image->mime_type;
            if (mime != nullptr)
            {
                const std::string mimeType = mime;
                if (mimeType == "image/ktx2" || mimeType == "image/webp" ||
                    mimeType == "image/basis")
                {
                    report(("declares mimeType '" + mimeType + "', which CNA has no decoder for")
                               .c_str());
                    return nullptr;
                }
            }
            return texture->image;
        }

        if (texture->has_basisu != 0) { report("KHR_texture_basisu, with no fallback source"); }
        else if (texture->has_webp != 0) { report("EXT_texture_webp, with no fallback source"); }
        return nullptr;
    }

    const cgltf_image* FindBaseColorImage(const cgltf_primitive& prim,
                                          std::vector<std::string>* unsupportedOut = nullptr)
    {
        if (!prim.material) { return nullptr; }
        // plan_gltf.md GLTF-349: a specular-glossiness material's `diffuseTexture` is its base
        // colour under a different name, and it is the only texture such a material carries that
        // survives the conversion -- so reading only `pbrMetallicRoughness` would leave an
        // archived-but-valid asset untextured on top of losing its specular tint.
        if (prim.material->has_pbr_specular_glossiness)
        {
            return ImageForTexture(prim.material->pbr_specular_glossiness.diffuse_texture.texture,
                                    "base color (from specular-glossiness diffuse)",
                                    unsupportedOut);
        }
        if (!prim.material->has_pbr_metallic_roughness) { return nullptr; }
        const cgltf_texture_view& view = prim.material->pbr_metallic_roughness.base_color_texture;
        return ImageForTexture(view.texture, "base color", unsupportedOut);
    }

    const cgltf_image* FindOcclusionImage(const cgltf_primitive& prim,
                                          std::vector<std::string>* unsupportedOut = nullptr)
    {
        if (!prim.material) { return nullptr; }
        return ImageForTexture(prim.material->occlusion_texture.texture, "occlusion",
                               unsupportedOut);
    }

    const cgltf_image* FindNormalImage(const cgltf_primitive& prim,
                                       std::vector<std::string>* unsupportedOut = nullptr)
    {
        if (!prim.material) { return nullptr; }
        return ImageForTexture(prim.material->normal_texture.texture, "normal", unsupportedOut);
    }

    const cgltf_image* FindMetallicRoughnessImage(const cgltf_primitive& prim,
                                                  std::vector<std::string>* unsupportedOut = nullptr)
    {
        if (!prim.material || !prim.material->has_pbr_metallic_roughness) { return nullptr; }
        const cgltf_texture_view& view =
            prim.material->pbr_metallic_roughness.metallic_roughness_texture;
        return ImageForTexture(view.texture, "metallic-roughness", unsupportedOut);
    }

    const cgltf_image* FindEmissiveImage(const cgltf_primitive& prim,
                                         std::vector<std::string>* unsupportedOut = nullptr)
    {
        if (!prim.material) { return nullptr; }
        return ImageForTexture(prim.material->emissive_texture.texture, "emissive", unsupportedOut);
    }

    const cgltf_image* FindSpecularImageEXT(const cgltf_primitive& prim,
                                            std::vector<std::string>* unsupportedOut = nullptr)
    {
        if (!prim.material || !prim.material->has_specular) { return nullptr; }
        return ImageForTexture(prim.material->specular.specular_texture.texture,
                               "specular strength", unsupportedOut);
    }

    const cgltf_image* FindSpecularColorImageEXT(
        const cgltf_primitive& prim, std::vector<std::string>* unsupportedOut = nullptr)
    {
        if (!prim.material || !prim.material->has_specular) { return nullptr; }
        return ImageForTexture(prim.material->specular.specular_color_texture.texture,
                               "specular color", unsupportedOut);
    }

    /// GLTF-202: the sampler a texture view's texture declares, mapped to XNA state. A view with no
    /// texture, or a texture with no sampler, yields glTF's own default (repeat + linear) with
    /// `declared` left false -- so "the author chose repeat" and "the author said nothing" stay
    /// distinguishable, which is what an import report needs.
    SamplerOut SamplerForTextureView(const cgltf_texture_view& view)
    {
        if (view.texture == nullptr || view.texture->sampler == nullptr)
        {
            return MapGltfSamplerEXT(0, 0, 0, 0);
        }
        const cgltf_sampler& sampler = *view.texture->sampler;
        SamplerOut out = MapGltfSamplerEXT(static_cast<int>(sampler.mag_filter),
                                           static_cast<int>(sampler.min_filter),
                                           static_cast<int>(sampler.wrap_s),
                                           static_cast<int>(sampler.wrap_t));
        out.declared = true;
        return out;
    }

    /// The TEXCOORD suffix a texture view actually selects. KHR_texture_transform's own selector
    /// overrides the view-level one (§3.9.2), including for non-base-colour maps.
    int EffectiveTexcoordSetEXT(const cgltf_texture_view& view)
    {
        if (view.has_transform && view.transform.has_texcoord)
        {
            return view.transform.texcoord;
        }
        return view.texcoord;
    }

    /// Retains KHR_texture_transform in its authored form. The effect converts this to affine rows
    /// once per draw; keeping it out of vertex bytes lets maps sharing a UV stream transform it
    /// independently and keeps direct and offline material state structurally identical.
    TextureTransformEXT TextureTransformForViewEXT(const cgltf_texture_view& view)
    {
        TextureTransformEXT result;
        if (view.has_transform == 0) { return result; }
        result.Offset = Vector2{view.transform.offset[0], view.transform.offset[1]};
        result.Scale = Vector2{view.transform.scale[0], view.transform.scale[1]};
        result.Rotation = view.transform.rotation;
        return result;
    }

    /// The texture view CNA treats as base colour after the archived specular-glossiness
    /// conversion. Kept beside FindBaseColorImage so image, sampler and UV selection cannot choose
    /// different source views.
    const cgltf_texture_view* BaseColorTextureViewEXT(const cgltf_material* material)
    {
        if (material == nullptr) { return nullptr; }
        if (material->has_pbr_specular_glossiness)
        {
            return &material->pbr_specular_glossiness.diffuse_texture;
        }
        if (material->has_pbr_metallic_roughness)
        {
            return &material->pbr_metallic_roughness.base_color_texture;
        }
        return nullptr;
    }

    int FindDracoUniqueIdEXT(const cgltf_primitive& prim, const cgltf_data* data,
                             cgltf_attribute_type type, int index)
    {
        if (data == nullptr || data->accessors == nullptr || data->accessors_count == 0)
        {
            return -1;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(data->accessors);
        const auto byteCount = data->accessors_count * sizeof(cgltf_accessor);
        const auto end = base + byteCount;
        if (end < base) { return -1; }

        for (cgltf_size k = 0; k < prim.draco_mesh_compression.attributes_count; ++k)
        {
            const cgltf_attribute& attribute = prim.draco_mesh_compression.attributes[k];
            if (attribute.type != type || attribute.index != index || attribute.data == nullptr)
            {
                continue;
            }

            // KHR_draco_mesh_compression stores a Draco unique ID, not a glTF accessor index.
            // cgltf nevertheless routes the integer through its generic attribute fixup, so it
            // arrives as a pointer into data->accessors. Check the byte range/alignment before
            // subtracting: a future cgltf representation change must fail closed, never invoke
            // undefined pointer arithmetic or accidentally select another Draco attribute.
            const auto address = reinterpret_cast<std::uintptr_t>(attribute.data);
            if (address < base || address >= end) { return -1; }
            const auto offset = address - base;
            if (offset % sizeof(cgltf_accessor) != 0) { return -1; }
            return static_cast<int>(offset / sizeof(cgltf_accessor));
        }
        return -1;
    }

#ifdef CNA_DRACO_AVAILABLE
    // CNB-91 (Phase 14F): decodes a Draco-compressed primitive's buffer_view into a real
    // draco::Mesh. Dequantization/attribute-transform is left at the decoder's own default
    // (i.e. every attribute reads back as real, already-dequantized float values via
    // PointAttribute::ConvertValue<float>, matching the semantic meaning UnpackAccessor's own
    // cgltf_accessor_unpack_floats gives for a regular accessor) -- SetSkipAttributeTransform is
    // never called.
    std::unique_ptr<draco::Mesh> DecodeDracoPrimitiveEXT(const cgltf_primitive& prim, const std::string& name)
    {
        const cgltf_buffer_view* bv = prim.draco_mesh_compression.buffer_view;
        if (!bv) { throw std::runtime_error("Primitive '" + name + "' has no Draco buffer_view."); }
        const std::uint8_t* data = cgltf_buffer_view_data(bv);
        if (!data) { throw std::runtime_error("Primitive '" + name + "' has an unreadable Draco buffer_view."); }

        draco::DecoderBuffer buffer;
        buffer.Init(reinterpret_cast<const char*>(data), bv->size);

        draco::Decoder decoder;
        draco::StatusOr<std::unique_ptr<draco::Mesh>> result = decoder.DecodeMeshFromBuffer(&buffer);
        if (!result.ok())
        {
            throw std::runtime_error(
                "Primitive '" + name + "' failed Draco decoding: " + result.status().error_msg_string());
        }
        return std::move(result).value();
    }

    // Reads one Draco-decoded attribute out into a flat per-point float array, in Draco's own
    // point-index order -- the same shape UnpackAccessor's own cgltf_accessor_unpack_floats
    // produces for a regular accessor, so callers can treat the two interchangeably.
    std::vector<float> UnpackDracoAttribute(const draco::Mesh& mesh, int uniqueId, int numComponents,
                                             const std::string& context)
    {
        if (uniqueId < 0) { return {}; }
        const draco::PointAttribute* attr = mesh.GetAttributeByUniqueId(static_cast<std::uint32_t>(uniqueId));
        if (!attr)
        {
            throw std::runtime_error("Draco attribute '" + context + "' (unique id " +
                                      std::to_string(uniqueId) + ") not found in decoded mesh.");
        }

        std::vector<float> out(static_cast<std::size_t>(mesh.num_points()) * static_cast<std::size_t>(numComponents));
        for (draco::PointIndex p(0); p < mesh.num_points(); ++p)
        {
            float* dst = out.data() + static_cast<std::size_t>(p.value()) * static_cast<std::size_t>(numComponents);
            if (!attr->ConvertValue<float>(attr->mapped_index(p), numComponents, dst))
            {
                throw std::runtime_error("Failed to convert Draco attribute '" + context + "' at point " +
                                          std::to_string(p.value()) + ".");
            }
        }
        return out;
    }
#endif

    PrimitiveTopology ClassifyPrimitiveTopology(const cgltf_primitive& prim, const std::string& name)
    {
        switch (prim.type)
        {
            case cgltf_primitive_type_points:         return PrimitiveTopology::Points;
            case cgltf_primitive_type_lines:          return PrimitiveTopology::Lines;
            case cgltf_primitive_type_line_loop:      return PrimitiveTopology::LineLoop;
            case cgltf_primitive_type_line_strip:     return PrimitiveTopology::LineStrip;
            case cgltf_primitive_type_triangles:      return PrimitiveTopology::Triangles;
            case cgltf_primitive_type_triangle_strip: return PrimitiveTopology::TriangleStrip;
            case cgltf_primitive_type_triangle_fan:   return PrimitiveTopology::TriangleFan;
            default: break;
        }
        // cgltf leaves the type invalid only for a `mode` outside 0..6, which the specification
        // does not define. Guessing "probably triangles" here is exactly the reflex GLTF-071 exists
        // to remove.
        throw std::runtime_error(
            "Primitive '" + name + "' declares a mesh.primitive.mode the glTF specification does "
            "not define (valid modes are 0..6).");
    }

    const char* PrimitiveTopologyName(PrimitiveTopology topology)
    {
        switch (topology)
        {
            case PrimitiveTopology::Points:        return "POINTS";
            case PrimitiveTopology::Lines:         return "LINES";
            case PrimitiveTopology::LineLoop:      return "LINE_LOOP";
            case PrimitiveTopology::LineStrip:     return "LINE_STRIP";
            case PrimitiveTopology::Triangles:     return "TRIANGLES";
            case PrimitiveTopology::TriangleStrip: return "TRIANGLE_STRIP";
            case PrimitiveTopology::TriangleFan:   return "TRIANGLE_FAN";
        }
        return "UNKNOWN";
    }

    int PrimitiveTopologyMode(PrimitiveTopology topology)
    {
        return static_cast<int>(topology);
    }

    bool IsPrimitiveTopologySupported(PrimitiveTopology topology)
    {
        // GLTF-072 gave the triangle topologies their conversion; GLTF-073/GLTF-076/GLTF-078 gave
        // the rest a draw path -- a real PrimitiveType on ModelMeshPart, a topology-aware primitive
        // count, and the closing-segment conversion a LINE_LOOP needs. All seven modes now import.
        //
        // Whether a given renderer can actually draw a point list is a separate question, answered
        // per renderer at draw time (GLTF-077), not by refusing to import the data.
        (void)topology;
        return true;
    }

    std::vector<std::uint32_t> ConvertToTriangleList(const std::vector<std::uint32_t>& indices,
                                                     PrimitiveTopology topology)
    {
        switch (topology)
        {
            // Already a triangle list: returned verbatim. A trailing partial triple has been
            // trimmed and counted by the caller (GLTF-079) before this point, so there is nothing
            // left here to decide.
            case PrimitiveTopology::Triangles: return indices;
            case PrimitiveTopology::TriangleStrip:
            {
                std::vector<std::uint32_t> out;
                if (indices.size() < 3) { return out; }
                out.reserve((indices.size() - 2) * 3);
                for (std::size_t i = 0; i + 2 < indices.size(); ++i)
                {
                    // Odd triangles have reversed winding in a strip, so their first two corners
                    // are swapped to bring every triangle back to the same orientation.
                    if (i % 2 == 0)
                    {
                        out.push_back(indices[i]);
                        out.push_back(indices[i + 1]);
                    }
                    else
                    {
                        out.push_back(indices[i + 1]);
                        out.push_back(indices[i]);
                    }
                    out.push_back(indices[i + 2]);
                }
                return out;
            }
            case PrimitiveTopology::TriangleFan:
            {
                std::vector<std::uint32_t> out;
                if (indices.size() < 3) { return out; }
                out.reserve((indices.size() - 2) * 3);
                for (std::size_t i = 1; i + 1 < indices.size(); ++i)
                {
                    out.push_back(indices[0]);
                    out.push_back(indices[i]);
                    out.push_back(indices[i + 1]);
                }
                return out;
            }
            case PrimitiveTopology::Points:
            case PrimitiveTopology::Lines:
            case PrimitiveTopology::LineLoop:
            case PrimitiveTopology::LineStrip:
                break;
        }
        // Asked to produce triangles from a topology that describes none. The caller, not this
        // function, is wrong: a line run is already what its own draw consumes and must not be
        // passed through here at all. Returning it unchanged would make the function's name a lie
        // and hide exactly the reinterpretation GLTF-071 removed.
        throw std::runtime_error(
            std::string("glTF primitive mode ") + std::to_string(PrimitiveTopologyMode(topology)) +
            " (" + PrimitiveTopologyName(topology) + ") describes no triangles, so it has no "
            "triangle-list equivalent.");
    }

    const char* AlphaModeEXTName(Microsoft::Xna::Framework::Graphics::AlphaModeEXT mode)
    {
        using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
        switch (mode)
        {
            case AlphaModeEXT::Mask:  return "MASK";
            case AlphaModeEXT::Blend: return "BLEND";
            case AlphaModeEXT::Opaque: break;
        }
        return "OPAQUE";
    }

    Microsoft::Xna::Framework::Graphics::AlphaModeEXT AlphaModeEXTFromName(const std::string& name)
    {
        using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
        if (name == "MASK")  { return AlphaModeEXT::Mask; }
        if (name == "BLEND") { return AlphaModeEXT::Blend; }
        return AlphaModeEXT::Opaque;
    }

    PrimitiveTopology PrimitiveTopologyFromName(const std::string& name)
    {
        for (int mode = 0; mode <= 6; ++mode)
        {
            const auto topology = static_cast<PrimitiveTopology>(mode);
            if (name == PrimitiveTopologyName(topology)) { return topology; }
        }
        // An absent or unrecognised name means a .cnj written before GLTF-073, which could only
        // ever have held a triangle list.
        return PrimitiveTopology::Triangles;
    }

    Microsoft::Xna::Framework::Graphics::PrimitiveType PrimitiveTypeForTopology(
        PrimitiveTopology topology)
    {
        using Microsoft::Xna::Framework::Graphics::PrimitiveType;
        switch (topology)
        {
            case PrimitiveTopology::Points:        return PrimitiveType::PointListEXT;
            case PrimitiveTopology::Lines:         return PrimitiveType::LineList;
            // Converted to a strip with its closing segment appended (GLTF-076), so by draw time
            // it IS a strip; naming it here keeps the mapping total rather than leaving a hole.
            case PrimitiveTopology::LineLoop:
            case PrimitiveTopology::LineStrip:     return PrimitiveType::LineStrip;
            case PrimitiveTopology::Triangles:     return PrimitiveType::TriangleList;
            // Converted to a triangle list at import (GLTF-072); neither reaches a draw as itself.
            case PrimitiveTopology::TriangleStrip:
            case PrimitiveTopology::TriangleFan:   return PrimitiveType::TriangleList;
        }
        return PrimitiveType::TriangleList;
    }

    /// Whether a topology describes triangles at all -- and therefore whether it has a
    /// triangle-list equivalent to be converted into (GLTF-072). KHR_draco_mesh_compression has a
    /// narrower mode restriction; IsDracoTopologyAllowedEXT owns that separate partition.
    bool ProducesTriangles(PrimitiveTopology topology)
    {
        return topology == PrimitiveTopology::Triangles
            || topology == PrimitiveTopology::TriangleStrip
            || topology == PrimitiveTopology::TriangleFan;
    }

    bool IsDracoTopologyAllowedEXT(PrimitiveTopology topology)
    {
        // KHR_draco_mesh_compression, "Restrictions on geometry type": exactly these two. A fan
        // produces triangles too, but the extension does not permit encoding one.
        return topology == PrimitiveTopology::Triangles
            || topology == PrimitiveTopology::TriangleStrip;
    }

    std::vector<std::uint32_t> NormalizeTriangleIndicesEXT(
        const std::vector<std::uint32_t>& indices,
        PrimitiveTopology sourceTopology,
        bool decodedDracoFaceList)
    {
        if (decodedDracoFaceList)
        {
            if (!IsDracoTopologyAllowedEXT(sourceTopology))
            {
                throw std::runtime_error(
                    std::string("KHR_draco_mesh_compression does not permit primitive mode ") +
                    PrimitiveTopologyName(sourceTopology) +
                    "; only TRIANGLES and TRIANGLE_STRIP are valid.");
            }
            // DecodeDracoPrimitiveEXT returns draco::Mesh, whose connectivity is exposed as
            // explicit faces. Flattening each face emits a triangle LIST regardless of whether
            // the source declared a list or strip. Running strip conversion over those triples
            // would invent overlapping triangles that were never in the mesh.
            return indices;
        }
        return ConvertToTriangleList(indices, sourceTopology);
    }

    std::vector<std::uint32_t> CloseLineLoop(const std::vector<std::uint32_t>& indices,
                                              PrimitiveTopology topology)
    {
        // GLTF-076. XNA has no LineLoop, and the difference between a loop and a strip is exactly
        // one segment: the one back to the first vertex, which glTF leaves implicit in the mode.
        // Appending it turns the run into an ordinary LINE_STRIP that every renderer can draw,
        // without the closing segment being lost -- which is what dropping the mode used to do.
        if (topology != PrimitiveTopology::LineLoop) { return indices; }
        if (indices.size() < 2) { return indices; }  // fewer than two vertices closes nothing
        std::vector<std::uint32_t> out(indices);
        out.push_back(indices.front());
        return out;
    }

    int PrimitiveCountForTopology(PrimitiveTopology topology, std::size_t indexCount)
    {
        const auto n = static_cast<long long>(indexCount);
        switch (topology)
        {
            case PrimitiveTopology::Points:        return static_cast<int>(n);
            case PrimitiveTopology::Lines:         return static_cast<int>(n / 2);
            // A loop's closing segment is implicit in the mode, so it describes the same count as
            // a strip over the same indices plus one -- but CNA converts a loop to a strip with the
            // closing index appended (GLTF-076), and by the time a count is asked for the run
            // already carries it. Answering n-1 for both keeps the two consistent.
            case PrimitiveTopology::LineLoop:
            case PrimitiveTopology::LineStrip:     return static_cast<int>(n > 0 ? n - 1 : 0);
            case PrimitiveTopology::Triangles:     return static_cast<int>(n / 3);
            case PrimitiveTopology::TriangleStrip:
            case PrimitiveTopology::TriangleFan:   return static_cast<int>(n > 2 ? n - 2 : 0);
        }
        return 0;
    }

    MeshOut ExtractMesh(const cgltf_data* data, const cgltf_primitive& prim, const std::string& name,
                         const SkeletonResult* skel, float unitScale)
    {
        // plan_gltf.md GLTF-071: read mesh.primitive.mode before anything else, and never
        // reinterpret it. GLTF-072 then converts the two topologies that have an exact triangle-
        // list equivalent; the line and point topologies stay rejected here with their real mode
        // named, because they decode fine but have nowhere to be drawn yet. The behaviour this
        // replaced was decoding a strip's index run as a triangle list -- dropping every triangle
        // after the first and drawing a chaotic tangle -- with no diagnostic anywhere.
        const PrimitiveTopology sourceTopology = ClassifyPrimitiveTopology(prim, name);
        if (!IsPrimitiveTopologySupported(sourceTopology))
        {
            const std::string owner = sourceTopology == PrimitiveTopology::Points
                ? "GLTF-077 decides whether a point list becomes a CNAEXT point topology or a "
                  "documented per-renderer rejection"
                : "GLTF-073 gives ModelMeshPart a real PrimitiveType and GLTF-078 a topology-aware "
                  "primitive count";
            throw std::runtime_error(
                "Primitive '" + name + "' uses glTF primitive mode " +
                std::to_string(PrimitiveTopologyMode(sourceTopology)) + " (" +
                PrimitiveTopologyName(sourceTopology) + "), which CNA does not import yet: it "
                "decodes correctly but has no draw path, since every loader still computes a "
                "triangle-list primitive count. " + owner + ". The triangle topologies (modes 4, "
                "5, 6) are supported; nothing is silently reinterpreted as a triangle list.");
        }

        // GLTF-080/GLTF-362: the extension's normative restriction is exactly TRIANGLES or
        // TRIANGLE_STRIP. That is narrower than "produces triangles": core glTF's TRIANGLE_FAN is
        // not legal here either. Refuse before decoding, independently of decoder availability, so
        // the diagnostic names the file-format contradiction instead of a later libdraco symptom.
        if (prim.has_draco_mesh_compression && !IsDracoTopologyAllowedEXT(sourceTopology))
        {
            throw std::runtime_error(
                "Primitive '" + name + "' declares mode " +
                std::string(PrimitiveTopologyName(sourceTopology)) +
                " together with KHR_draco_mesh_compression, whose specification permits only "
                "TRIANGLES or TRIANGLE_STRIP. The primitive is refused before decoding rather "
                "than drawn as another topology (plan_gltf.md GLTF-080/GLTF-362).");
        }

#ifdef CNA_DRACO_AVAILABLE
        // CNB-91 (Phase 14F): decoded once here; every per-attribute unpack below (via
        // unpackSemantic) and the index extraction near the end both branch on whether this is
        // non-null instead of reading from cgltf's own (bufferView-less, metadata-only for a
        // Draco primitive) accessors.
        std::unique_ptr<draco::Mesh> dracoMesh;
        if (prim.has_draco_mesh_compression)
        {
            dracoMesh = DecodeDracoPrimitiveEXT(prim, name);
        }
#else
        if (prim.has_draco_mesh_compression)
        {
            throw std::runtime_error(
                "Primitive '" + name + "' uses Draco mesh compression (KHR_draco_mesh_compression), "
                "which this build of CNA was compiled without libdraco support for.");
        }
#endif

        const cgltf_accessor* posAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0);
        if (!posAcc) { throw std::runtime_error("Primitive '" + name + "' has no POSITION attribute."); }
        const cgltf_accessor* normAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_normal, 0);

        // UV stream selection is finalised after all decoded material images are known, because
        // GLTF-182 can carry the first two DISTINCT sets sampled by those maps. GLTF-184 keeps
        // texture transforms separately on MaterialOut; vertex coordinates stay authored.
        int texcoordIndex = 0;
        const cgltf_texture_view* baseColorView = BaseColorTextureViewEXT(prim.material);
        if (baseColorView != nullptr && baseColorView->texture != nullptr)
        {
            texcoordIndex = EffectiveTexcoordSetEXT(*baseColorView);
        }

        const cgltf_accessor* colorAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_color, 0);
        const cgltf_accessor* jointsAcc = skel ? cgltf_find_accessor(&prim, cgltf_attribute_type_joints, 0) : nullptr;
        const cgltf_accessor* weightsAcc = skel ? cgltf_find_accessor(&prim, cgltf_attribute_type_weights, 0) : nullptr;

        MeshOut out;
        out.name = name;
        // The topology travels with the data it describes, so no downstream consumer has to assume
        // one. `topology` is what indexBytes ends up in and is always Triangles, because a strip or
        // fan is converted below; `sourceTopology` keeps what the file declared, so the conversion
        // is visible rather than lossy.
        out.sourceTopology = sourceTopology;
        // What indexBytes ends up in: a triangle list for the three triangle modes (converted where
        // needed), a line strip for a LINE_LOOP once its closing segment is appended, and the
        // source topology itself for the rest.
        out.topology = ProducesTriangles(sourceTopology) ? PrimitiveTopology::Triangles
                     : (sourceTopology == PrimitiveTopology::LineLoop
                            ? PrimitiveTopology::LineStrip : sourceTopology);
        out.skinned = (jointsAcc != nullptr) && (weightsAcc != nullptr);
        // A skinned+colored primitive uses a stride-56 layout (the stride-52 GPU-skinned layout
        // with a per-vertex Color appended at the end) and SkinnedEffect's own CNAEXT
        // VertexColorEnabled addition (real XNA's SkinnedEffect has no such property).
        out.colored = (colorAcc != nullptr);

        // plan_gltf.md GLTF-091/GLTF-092: attributes the file authored and CNA has nowhere to put.
        // Neither is an error -- §3.7.2.1 reserves the `_` prefix for custom semantics precisely so
        // a reader may ignore them, and XNA simply has one colour channel -- but both are data
        // that silently does not arrive, and a mesh whose real tint is in COLOR_1 imports looking
        // like a mistake nobody can trace.
        for (cgltf_size a = 0; a < prim.attributes_count; ++a)
        {
            const cgltf_attribute& attribute = prim.attributes[a];
            if (attribute.type == cgltf_attribute_type_color && attribute.index > 0)
            {
                ++out.extraColorSetsEXT;
            }
            if (attribute.name != nullptr && attribute.name[0] == '_')
            {
                out.ignoredCustomAttributesEXT.emplace_back(attribute.name);
            }
        }
        out.material.baseColorImage = FindBaseColorImage(prim, &out.unsupportedTextureSourcesEXT);
        // An unskinned, uncolored primitive with both a base-color and an occlusion texture is
        // imported through DualTextureEffect (Texture=base color, Texture2=occlusion) instead of
        // BasicEffect -- real XNA's DualTextureEffect always samples both texture slots (no
        // TextureEnabled-style toggle) via a single shared UV set at vertex attribute locations
        // 0/1 with no Normal in between (see EasyGLRenderer::ApplyLayout's stride==20
        // case), so this reuses the plain VertexPositionTexture layout rather than a new
        // Position+Normal+Texture+Texture2 vertex format. Skinned/colored meshes keep their
        // existing effect (SkinnedEffect has no Texture2 slot; the colored VertexPositionColor
        // Texture layout has no room for a second UV/texture either) -- a documented scope cut,
        // not an oversight.
        // plan_cnj.md CNB-59 (Phase 13A): an unskinned, uncolored primitive with a normal map or
        // metallic-roughness map is genuinely PBR-authored content (not just "any glTF material"
        // -- pbrMetallicRoughness is glTF's own default material block even for the simple
        // base-color-only content BasicEffect already handles) and is imported through PbrEffect
        // (stride 48, VertexPositionNormalTangentTexture) instead. Takes priority over
        // useDualTexture below when both would otherwise apply (a material with both an
        // occlusion map AND a normal/metallic-roughness map is unambiguously meant for real PBR
        // rendering, not the DualTextureEffect occlusion-as-lightmap approximation).
        out.material.normalImage = FindNormalImage(prim, &out.unsupportedTextureSourcesEXT);
        out.material.metallicRoughnessImage =
            FindMetallicRoughnessImage(prim, &out.unsupportedTextureSourcesEXT);
        out.material.emissiveImage = FindEmissiveImage(prim, &out.unsupportedTextureSourcesEXT);
        out.material.specularImageEXT =
            FindSpecularImageEXT(prim, &out.unsupportedTextureSourcesEXT);
        out.material.specularColorImageEXT =
            FindSpecularColorImageEXT(prim, &out.unsupportedTextureSourcesEXT);
        // PBR + skinning combo: a skinned primitive with a normal/metallic-roughness map is
        // imported through SkinnedPbrEffect (stride 68) instead of plain SkinnedEffect -- the
        // vertex-color combo (usePbr && colored) is still not attempted, matching PbrEffect's own
        // unskinned scope cut (no PBR shader currently reads a vertex Color stream).
        // plan_gltf.md GLTF-215/GLTF-217: the selection rule is the MATERIAL MODEL the file
        // declares, not which texture maps it happens to carry. glTF's default material *is*
        // metallic-roughness (§3.9), so a primitive with no material at all is not "unlit white" --
        // it is baseColor (1,1,1,1), metallic 1, roughness 1. The old rule
        // (`normalImage || metallicRoughnessImage`) asked whether a map was present, which is why
        // f8's gold factor-only material became a white BasicEffect: it had every PBR factor and
        // not one map.
        //
        // `!colored` stays, and is a real implementation limit rather than a spec one: no PBR
        // shader currently reads a vertex Color stream, so a COLOR_0 primitive keeps the stride-24
        // layout that does. GLTF-238 owns lifting it.
        // The test is which material MODEL applies, and metallic-roughness is glTF's default in
        // two distinct ways: a primitive with no material at all gets the default material, and a
        // material that simply omits the optional `pbrMetallicRoughness` object still uses that
        // model with default factors. Only a material that declares a *different* model --
        // KHR_materials_pbrSpecularGlossiness, or KHR_materials_unlit, neither of which CNA's PBR
        // shaders implement -- is excluded. Keying off `has_pbr_metallic_roughness` alone would
        // have missed a material that carries only a normalTexture, which is metallic-roughness
        // with defaults and is exactly the shape the tangent fixture authors.
        //
        // plan_gltf.md GLTF-349: a specular-glossiness material now counts as metallic-roughness
        // too, because it is CONVERTED to one below rather than excluded. The extension is
        // archived by Khronos but present in older assets, so refusing it would reject content
        // that is otherwise perfectly importable -- and leaving it on the non-PBR path, as it was,
        // silently dropped every factor it carries.
        const bool metallicRoughnessMaterial =
            (prim.material == nullptr) || !prim.material->unlit;
        out.usePbr = (!out.colored) && metallicRoughnessMaterial;
        // plan_gltf.md GLTF-337. Its own flag rather than "not usePbr", because the two mean
        // different things: a vertex-coloured metallic-roughness primitive is also non-PBR
        // (GLTF-241) and must still be LIT. Conflating them would darken a surface the file asked
        // to be shaded, which is the same class of silent wrongness as leaving unlit lit.
        out.unlitEXT = (prim.material != nullptr) && (prim.material->unlit != 0);
        // GLTF-238: the material's identity, for effect sharing in the loaders.
        out.material.sourceMaterialEXT = prim.material;
        // plan_gltf.md GLTF-241: `colored && metallicRoughnessMaterial` is the one combination the
        // rule above refuses, and refusing it silently is what the task is about. No CNA vertex
        // layout carries a Color alongside a Tangent and no PBR shader reads a colour stream, so
        // supporting it means a new stride plus a shader variant on every renderer -- the same
        // blast-radius argument that ruled out colour-space option A. The primitive is imported
        // through BasicEffect with its vertex colours intact and the material is NAMED as dropped,
        // which is the other outcome the task's acceptance allows.
        if (out.colored && metallicRoughnessMaterial)
        {
            out.unsupportedMaterialModelEXT = "metallic-roughness";
        }
        // GLTF-219/GLTF-221: the factors are read for ANY metallic-roughness material, not only
        // one that also selected PBR. They were assigned inside the old `usePbr` guard, so a
        // factor-only material left them at MaterialOut's defaults -- the second half of D7, and the
        // reason f8 lost its metallic/roughness/emissive as well as its base colour.
        if (prim.material && prim.material->has_pbr_metallic_roughness)
        {
            out.material.metallicFactor  = prim.material->pbr_metallic_roughness.metallic_factor;
            out.material.roughnessFactor = prim.material->pbr_metallic_roughness.roughness_factor;
            // GLTF-216: baseColorFactor multiplies the base-colour texture (or stands alone when
            // there is none). Never read anywhere before this.
            const cgltf_float* base = prim.material->pbr_metallic_roughness.base_color_factor;
            out.material.baseColorFactor = Vector4(base[0], base[1], base[2], base[3]);
        }
        // plan_gltf.md GLTF-343/GLTF-344: cgltf already parses these extensions, but nothing in
        // CNA copied their factor state before this point. Keep the raw values in MaterialOut;
        // PbrEffect/SkinnedPbrEffect derive shader-ready dielectric F0/F90 when filling the draw
        // block. GLTF-344 carries the two optional texture views independently below.
        if (prim.material && prim.material->has_ior)
        {
            out.material.iorEXT = prim.material->ior.ior;
        }
        if (prim.material && prim.material->has_specular)
        {
            out.material.specularFactorEXT = prim.material->specular.specular_factor;
            const cgltf_float* color = prim.material->specular.specular_color_factor;
            out.material.specularColorFactorEXT = Vector3(color[0], color[1], color[2]);
        }
        // plan_gltf.md GLTF-349: KHR_materials_pbrSpecularGlossiness, converted rather than
        // refused. Khronos archived it, but it is what a decade of older assets are authored in,
        // and rejecting them would be a worse answer than an approximation with a name.
        //
        // The standard mapping: diffuse becomes the base colour, the surface becomes a dielectric
        // (metallic 0), and roughness is glossiness inverted. What it cannot carry is
        // `specularFactor` -- specular-glossiness expresses a COLOURED specular reflection, which
        // metallic-roughness can only approach by making the surface metal, which would also
        // tint the diffuse. A dielectric material converts almost exactly; a brass one goes grey.
        // Both the fact and the size of the loss are recorded so a loader can say which happened.
        //
        // Placed after the metallic-roughness block on purpose: a material declaring both is
        // malformed, and preferring the newer model is the reading that loses less.
        if (prim.material && prim.material->has_pbr_specular_glossiness &&
            !prim.material->has_pbr_metallic_roughness)
        {
            const cgltf_pbr_specular_glossiness& sg = prim.material->pbr_specular_glossiness;
            out.material.baseColorFactor = Vector4(
                sg.diffuse_factor[0], sg.diffuse_factor[1], sg.diffuse_factor[2],
                sg.diffuse_factor[3]);
            out.material.metallicFactor = 0.0f;
            out.material.roughnessFactor =
                std::clamp(1.0f - sg.glossiness_factor, 0.0f, 1.0f);
            out.convertedFromSpecularGlossinessEXT = true;
            out.droppedSpecularStrengthEXT = std::max(
                sg.specular_factor[0], std::max(sg.specular_factor[1], sg.specular_factor[2]));
        }
        if (prim.material)
        {
            // plan_gltf.md GLTF-228/GLTF-229/GLTF-231: the alpha and sidedness state, read for any
            // material rather than only a PBR-selected one. These are the last three fields of the
            // factor-only material D7 recorded as entirely lost.
            using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
            switch (prim.material->alpha_mode)
            {
                case cgltf_alpha_mode_mask:
                    out.material.alphaMode = AlphaModeEXT::Mask;
                    break;
                case cgltf_alpha_mode_blend:
                    out.material.alphaMode = AlphaModeEXT::Blend;
                    break;
                case cgltf_alpha_mode_opaque:
                default:
                    out.material.alphaMode = AlphaModeEXT::Opaque;
                    break;
            }
            out.material.alphaCutoff = prim.material->alpha_cutoff;
            out.material.doubleSided = prim.material->double_sided != 0;

            // plan_gltf.md GLTF-339. KHR_materials_transmission was read by nobody, so a glass
            // material imported fully opaque -- the ChronographWatch defect, where the crystal
            // hides the dial it is supposed to reveal.
            //
            // A real transmission pass samples the framebuffer behind the surface and blurs it by
            // roughness; that needs a second pass and a scene-colour target, which no CNA stock
            // effect has. What is done instead is an ALPHA-BLEND APPROXIMATION, and calling it that
            // is the point: alpha = 1 - transmissionFactor, with alphaMode forced to BLEND.
            //
            // It is explicitly NOT physical, and the ways it is wrong are worth naming rather than
            // discovering. There is no refraction, so nothing behind the surface is displaced. The
            // blur roughness would cause does not happen. Alpha blending darkens what is behind a
            // tinted surface where transmission would tint it, which for coloured glass is a
            // visibly different result. And specular reflection, which a transmissive surface keeps
            // at full strength, fades out with the alpha.
            //
            // It is still much closer than opaque, and it is reported every time -- an
            // approximation nobody is told about is indistinguishable from a bug.
            if (prim.material->has_transmission != 0)
            {
                out.transmissionFactorEXT =
                    std::clamp(prim.material->transmission.transmission_factor, 0.0f, 1.0f);
                out.transmissionHasTextureEXT =
                    prim.material->transmission.transmission_texture.texture != nullptr;
                if (out.transmissionFactorEXT > 0.0f)
                {
                    out.transmissionApproximatedEXT = true;
                    out.material.alphaMode = AlphaModeEXT::Blend;
                    // Multiplied into whatever alpha the material already asked for, rather than
                    // replacing it: a material that is both partly transparent and transmissive
                    // should end up more transparent than either alone, not lose one of them.
                    out.material.baseColorFactor.W *= (1.0f - out.transmissionFactorEXT);
                }
            }
            // plan_gltf.md GLTF-224/GLTF-225. Read for ANY material, not only one that selected
            // PBR -- the same ungating GLTF-219/221 applied to the scalar factors, and for the
            // same reason: a value the file states should reach MeshOut whether or not the effect
            // chosen for it happens to consume it.
            //
            // cgltf sets a texture view's `scale` to 1 only when it PARSES that view; a material
            // with no `normalTexture` object at all leaves the struct zeroed by calloc. Reading it
            // straight through therefore yields 0 -- "flatten the normal map completely" -- for
            // every material that simply does not mention one, which is the opposite of glTF's own
            // default. So an undeclared view falls back to 1 explicitly. A view is declared when it
            // names a texture, or when cgltf's own parse left a non-zero scale behind; a view that
            // authors `scale: 0` *and* names no texture is degenerate either way, since the scalar
            // only means anything when a map is bound.
            const auto viewScalar = [](const cgltf_texture_view& view) {
                const bool declared = (view.texture != nullptr) || (view.scale != 0.0f);
                return declared ? view.scale : 1.0f;
            };
            out.material.normalScale = viewScalar(prim.material->normal_texture);
            out.material.occlusionStrength = viewScalar(prim.material->occlusion_texture);

            // plan_gltf.md GLTF-202: one sampler per texture slot, read from the texture each view
            // names. A slot with no texture keeps glTF's default with `declared` false.
            const auto slot = [](TextureSlotEXT s) { return static_cast<std::size_t>(s); };
            // FindBaseColorImage gives the archived specular-glossiness diffuse texture
            // precedence because GLTF-349 converts that material model to CNA's PBR path. Its
            // sampler must follow the same source view; reading the core metallic-roughness view
            // here instead made converted textured materials fall back to LinearWrap and also
            // bypassed GLTF-206's mip-chain report.
            if (prim.material->has_pbr_specular_glossiness)
            {
                out.material.samplers[slot(TextureSlotEXT::BaseColor)] =
                    SamplerForTextureView(
                        prim.material->pbr_specular_glossiness.diffuse_texture);
            }
            else if (prim.material->has_pbr_metallic_roughness)
            {
                out.material.samplers[slot(TextureSlotEXT::BaseColor)] =
                    SamplerForTextureView(prim.material->pbr_metallic_roughness.base_color_texture);
            }
            if (prim.material->has_pbr_metallic_roughness)
            {
                out.material.samplers[slot(TextureSlotEXT::MetallicRoughness)] =
                    SamplerForTextureView(
                        prim.material->pbr_metallic_roughness.metallic_roughness_texture);
            }
            out.material.samplers[slot(TextureSlotEXT::Normal)] =
                SamplerForTextureView(prim.material->normal_texture);
            out.material.samplers[slot(TextureSlotEXT::Emissive)] =
                SamplerForTextureView(prim.material->emissive_texture);
            out.material.samplers[slot(TextureSlotEXT::Occlusion)] =
                SamplerForTextureView(prim.material->occlusion_texture);
            if (prim.material->has_specular)
            {
                out.material.samplers[slot(TextureSlotEXT::Specular)] =
                    SamplerForTextureView(prim.material->specular.specular_texture);
                out.material.samplers[slot(TextureSlotEXT::SpecularColor)] =
                    SamplerForTextureView(prim.material->specular.specular_color_texture);
            }

            // GLTF-184/GLTF-336/GLTF-344: transforms follow the same source views as images, samplers
            // and texCoord selectors. The archived specular-glossiness conversion uses its
            // diffuse view as base colour here too, preventing that compatibility path from
            // choosing one view's image and another view's transform.
            if (const cgltf_texture_view* baseView = BaseColorTextureViewEXT(prim.material))
            {
                out.material.textureTransformsEXT[slot(TextureSlotEXT::BaseColor)] =
                    TextureTransformForViewEXT(*baseView);
            }
            out.material.textureTransformsEXT[slot(TextureSlotEXT::Normal)] =
                TextureTransformForViewEXT(prim.material->normal_texture);
            if (prim.material->has_pbr_metallic_roughness)
            {
                out.material.textureTransformsEXT[slot(TextureSlotEXT::MetallicRoughness)] =
                    TextureTransformForViewEXT(
                        prim.material->pbr_metallic_roughness.metallic_roughness_texture);
            }
            out.material.textureTransformsEXT[slot(TextureSlotEXT::Emissive)] =
                TextureTransformForViewEXT(prim.material->emissive_texture);
            out.material.textureTransformsEXT[slot(TextureSlotEXT::Occlusion)] =
                TextureTransformForViewEXT(prim.material->occlusion_texture);
            if (prim.material->has_specular)
            {
                out.material.textureTransformsEXT[slot(TextureSlotEXT::Specular)] =
                    TextureTransformForViewEXT(prim.material->specular.specular_texture);
                out.material.textureTransformsEXT[slot(TextureSlotEXT::SpecularColor)] =
                    TextureTransformForViewEXT(prim.material->specular.specular_color_texture);
            }

            // CNB-97 (Phase 14H): KHR_materials_emissive_strength extends EmissiveFactor's own
            // [0,1] range with a multiplier (real HDR-authored content routinely uses > 1), before
            // the emissive texture (if any) is applied -- glTF's own spec order.
            const float emissiveStrength = prim.material->has_emissive_strength
                ? prim.material->emissive_strength.emissive_strength : 1.0f;
            out.material.emissiveFactor = Vector3(prim.material->emissive_factor[0],
                                                   prim.material->emissive_factor[1],
                                                   prim.material->emissive_factor[2]) *
                                          emissiveStrength;
        }

        // occlusionImage is populated whenever eligible (unskinned, uncolored) regardless of
        // which effect ends up consuming it -- DualTextureEffect's own Texture2 approximation
        // (CNB-72/73) only when usePbr is false, or PbrEffect's own real OcclusionMap when true
        // (see the useDualTexture computation immediately below, and ExtractMesh's caller for the
        // PbrEffect wiring).
        // PBR + skinning combo: occlusionImage is now also extracted for skinned, uncolored
        // primitives (needed for SkinnedPbrEffect's own OcclusionMap), but useDualTexture stays
        // gated to unskinned primitives -- SkinnedEffect has no Texture2 slot, so a skinned
        // non-PBR mesh with an occlusion+base-color pair simply leaves occlusionImage unused.
        out.material.occlusionImage =
            (!out.colored) ? FindOcclusionImage(prim, &out.unsupportedTextureSourcesEXT) : nullptr;
        out.useDualTexture = (!out.usePbr) && (!out.skinned) &&
                             (out.material.occlusionImage != nullptr) &&
                             (out.material.baseColorImage != nullptr);

        // plan_gltf.md GLTF-206: glTF's ordinary PNG/JPEG inputs decode to one texture level, so
        // an authored *_MIPMAP_* minFilter cannot actually choose a lower-resolution level. Do not
        // silently synthesize one generic RGBA chain: base colour/emissive are sRGB data, normals
        // must be renormalised, and metallic-roughness/occlusion are linear packed channels. Name
        // only maps the chosen effect samples; an unsupported/downgraded map already has its own
        // more relevant report entry.
        const auto noteMissingMipChain = [&](TextureSlotEXT textureSlot,
                                             const cgltf_image* image,
                                             const char* mapName)
        {
            const std::size_t index = static_cast<std::size_t>(textureSlot);
            if (image != nullptr &&
                out.material.samplers[index].minFilterRequiresMipChain)
            {
                out.mipmappedSamplerMapsWithoutMipChainEXT.emplace_back(mapName);
            }
        };
        noteMissingMipChain(TextureSlotEXT::BaseColor,
                            out.material.baseColorImage, "baseColorTexture");
        if (out.usePbr)
        {
            noteMissingMipChain(TextureSlotEXT::Normal,
                                out.material.normalImage, "normalTexture");
            noteMissingMipChain(TextureSlotEXT::MetallicRoughness,
                                out.material.metallicRoughnessImage,
                                "metallicRoughnessTexture");
            noteMissingMipChain(TextureSlotEXT::Emissive,
                                out.material.emissiveImage, "emissiveTexture");
            noteMissingMipChain(TextureSlotEXT::Occlusion,
                                out.material.occlusionImage, "occlusionTexture");
            noteMissingMipChain(TextureSlotEXT::Specular,
                                out.material.specularImageEXT, "specularTexture");
            noteMissingMipChain(TextureSlotEXT::SpecularColor,
                                out.material.specularColorImageEXT, "specularColorTexture");
        }
        else if (out.useDualTexture)
        {
            noteMissingMipChain(TextureSlotEXT::Occlusion,
                                out.material.occlusionImage, "occlusionTexture");
        }

        // GLTF-182/183: choose at most two DISTINCT source TEXCOORD sets across the maps CNA will
        // really sample, then record each map's packed attribute selector. The base-colour view is
        // considered first to preserve the old one-channel layout as a byte-for-byte prefix. A
        // map whose image could not be decoded is excluded: its relevant diagnostic is the missing
        // image, not which coordinates an absent sample would have used.
        if (out.usePbr && prim.material)
        {
            const std::array<const cgltf_texture_view*, 7> views{{
                BaseColorTextureViewEXT(prim.material),
                &prim.material->normal_texture,
                prim.material->has_pbr_metallic_roughness
                    ? &prim.material->pbr_metallic_roughness.metallic_roughness_texture : nullptr,
                &prim.material->emissive_texture,
                &prim.material->occlusion_texture,
                prim.material->has_specular
                    ? &prim.material->specular.specular_texture : nullptr,
                prim.material->has_specular
                    ? &prim.material->specular.specular_color_texture : nullptr,
            }};
            const std::array<const cgltf_image*, 7> images{{
                out.material.baseColorImage,
                out.material.normalImage,
                out.material.metallicRoughnessImage,
                out.material.emissiveImage,
                out.material.occlusionImage,
                out.material.specularImageEXT,
                out.material.specularColorImageEXT,
            }};
            static constexpr const char* names[7] = {
                "baseColorTexture", "normalTexture", "metallicRoughnessTexture",
                "emissiveTexture", "occlusionTexture", "specularTexture",
                "specularColorTexture"};

            std::vector<int> packedSets;
            for (std::size_t slotIndex = 0; slotIndex < views.size(); ++slotIndex)
            {
                if (views[slotIndex] == nullptr || images[slotIndex] == nullptr) { continue; }
                const int sourceSet = EffectiveTexcoordSetEXT(*views[slotIndex]);
                if (std::find(packedSets.begin(), packedSets.end(), sourceSet) == packedSets.end() &&
                    packedSets.size() < 2)
                {
                    packedSets.push_back(sourceSet);
                }
            }
            if (packedSets.empty()) { packedSets.push_back(texcoordIndex); }
            texcoordIndex = packedSets.front();
            out.packedTexcoordSourceSetsEXT[0] = packedSets[0];
            if (packedSets.size() == 2)
            {
                out.hasSecondTexcoordEXT = true;
                out.packedTexcoordSourceSetsEXT[1] = packedSets[1];
            }

            for (std::size_t slotIndex = 0; slotIndex < views.size(); ++slotIndex)
            {
                if (views[slotIndex] == nullptr || images[slotIndex] == nullptr) { continue; }
                const int sourceSet = EffectiveTexcoordSetEXT(*views[slotIndex]);
                const auto found = std::find(packedSets.begin(), packedSets.end(), sourceSet);
                if (found == packedSets.end())
                {
                    out.uvSetMismatchedMapsEXT.emplace_back(names[slotIndex]);
                    continue;
                }
                out.material.textureCoordinateSetsEXT[slotIndex] =
                    static_cast<std::uint8_t>(std::distance(packedSets.begin(), found));
            }
        }
        // Unskinned colored meshes reuse the real XNA VertexPositionColorTexture layout (stride
        // 24, Position+Color+TextureCoordinate, no Normal) -- already fully supported end-to-end
        // by ModelTypeReader and every graphics renderer's existing VertexColorEnabled shader path.
        // Skinned colored meshes use the stride-56 layout instead (Position+Normal+
        // TextureCoordinate+BlendWeight+BlendIndices+Color). Skinned + PBR meshes use the new
        // stride-68 layout (Position+Normal+Tangent+TextureCoordinate+BlendWeight+BlendIndices),
        // widened to 60/76 when two sampled maps select distinct authored UV sets.
        const VertexLayoutRequestEXT layoutRequest{out.skinned, out.colored, out.usePbr,
                                                    out.useDualTexture,
                                                    out.hasSecondTexcoordEXT};
        const VertexLayoutRuleEXT& layoutRule = SelectVertexLayoutEXT(layoutRequest);
        out.stride = layoutRule.stride;
        // plan_gltf.md GLTF-100: what this row cannot carry, taken from the table rather than
        // re-derived at each site that cares. A downgrade decided by which ternary branch happened
        // to be taken is a downgrade nobody can enumerate.
        out.unrepresentableForStrideEXT = layoutRule.unrepresentable;

        const cgltf_size vertexCount = posAcc->count;

        // plan_gltf.md GLTF-060. §3.7.2.1 requires every attribute of a primitive to have the SAME
        // count, and CNA relies on it absolutely: POSITION's count drives the loop that indexes
        // every other decoded stream, so a shorter NORMAL reads past the end of its own vector. A
        // malformed file therefore had to be caught here or become undefined behaviour a few lines
        // down -- and it was not caught anywhere.
        //
        // Checked against every attribute the primitive declares, not only the ones this stride
        // happens to use: an attribute CNA ignores today is still evidence the file is malformed,
        // and refusing it now is better than refusing it the day a layout starts reading it.
        for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai)
        {
            const cgltf_attribute& attribute = prim.attributes[ai];
            if (attribute.data == nullptr || attribute.data->count == vertexCount) { continue; }
            throw std::runtime_error(
                "Primitive '" + name + "' declares attribute '" +
                std::string(attribute.name != nullptr ? attribute.name : "<unnamed>") +
                "' with " + std::to_string(attribute.data->count) +
                " elements, but POSITION has " + std::to_string(vertexCount) +
                ". glTF §3.7.2.1 requires every attribute of a primitive to have the same count "
                "(malformed file).");
        }

#ifdef CNA_DRACO_AVAILABLE
        if (dracoMesh && static_cast<cgltf_size>(dracoMesh->num_points()) != vertexCount)
        {
            throw std::runtime_error(
                "Primitive '" + name + "' has a Draco-decoded point count that does not match its "
                "declared POSITION accessor count (malformed file).");
        }
#endif

        ReserveOrRefuse(out.vertexBytes, static_cast<std::size_t>(vertexCount),
                         static_cast<std::size_t>(out.stride), name);

#ifdef CNA_DRACO_AVAILABLE
        // Unified per-semantic unpacking: reads from the decoded Draco mesh (via its own unique
        // attribute ID) when this is a Draco-compressed primitive, or from the regular accessor
        // otherwise -- every call site below is agnostic to which source actually backs it.
        const auto unpackSemantic = [&](cgltf_attribute_type type, int setIndex, const cgltf_accessor* acc,
                                         int numComponents, const char* context) -> std::vector<float>
        {
            if (dracoMesh)
            {
                const int uniqueId = FindDracoUniqueIdEXT(prim, data, type, setIndex);
                return UnpackDracoAttribute(*dracoMesh, uniqueId, numComponents, context);
            }
            return acc ? UnpackAccessor(acc, static_cast<cgltf_size>(numComponents), context) : std::vector<float>();
        };
#else
        const auto unpackSemantic = [&](cgltf_attribute_type /*type*/, int /*setIndex*/, const cgltf_accessor* acc,
                                         int numComponents, const char* context) -> std::vector<float>
        {
            return acc ? UnpackAccessor(acc, static_cast<cgltf_size>(numComponents), context) : std::vector<float>();
        };
#endif

        // Triangle connectivity as a flat, source-agnostic index list -- built once here (rather
        // than at the very end of this function, its own previous location) so ComputeTangentsEXT
        // below can use it too, matching CNB-91's own Draco fix: a Draco-compressed primitive's
        // real connectivity lives in the decoded mesh's own face list, not prim.indices (which has
        // no backing data in that case).
        std::vector<std::uint32_t> indices;
#ifdef CNA_DRACO_AVAILABLE
        if (dracoMesh)
        {
            indices.reserve(static_cast<std::size_t>(dracoMesh->num_faces()) * 3);
            for (draco::FaceIndex fi(0); fi < dracoMesh->num_faces(); ++fi)
            {
                const draco::Mesh::Face& face = dracoMesh->face(fi);
                for (int c = 0; c < 3; ++c)
                {
                    indices.push_back(static_cast<std::uint32_t>(face[static_cast<std::size_t>(c)].value()));
                }
            }
        }
        else
#endif
        if (prim.indices)
        {
            // GLTF-063: a CNA-side, sparse-aware, bounds-checked decode. See UnpackIndexAccessor.
            indices = UnpackIndexAccessor(*prim.indices, name);
        }
        else
        {
            // Non-indexed primitive: implicit sequential vertex order per the glTF spec --
            // Khronos's own "Fox" sample has exactly this shape.
            ReserveOrRefuse(indices, static_cast<std::size_t>(vertexCount),
                             sizeof(std::uint32_t), name);
            for (cgltf_size i = 0; i < vertexCount; ++i)
            {
                indices.push_back(static_cast<std::uint32_t>(i));
            }
        }

        // GLTF-063: every index must address a real vertex before anything consumes it. Two things
        // downstream depend on this and neither could detect a violation: ComputeTangentsEXT
        // indexes the position/normal/UV arrays directly, and the packing loop below narrows to
        // uint16 with a plain static_cast whenever the vertex count fits. An out-of-range index is
        // a malformed file either way, and turning it into a named error is the whole point --
        // silently truncating it produces wrong geometry with no diagnostic at all.
        for (std::size_t i = 0; i < indices.size(); ++i)
        {
            if (static_cast<cgltf_size>(indices[i]) >= vertexCount)
            {
                throw std::runtime_error(
                    "Primitive '" + name + "' has index " + std::to_string(i) + " = " +
                    std::to_string(indices[i]) + ", but the primitive declares only " +
                    std::to_string(static_cast<std::size_t>(vertexCount)) +
                    " vertices (its POSITION accessor's count).");
            }
        }

        // GLTF-079: an index count that is not a whole number of primitives for the declared mode.
        // The remainder cannot be drawn -- a fourth index is not a triangle -- and the two ways of
        // pretending otherwise are both worse than dropping it: reading it as a further primitive
        // walks past the end of the run, and dropping it without a word leaves an author's model
        // missing geometry with nothing to point at. So the incomplete tail goes, and it is
        // counted; the loaders report the count.
        {
            const std::size_t before = indices.size();
            switch (sourceTopology)
            {
                case PrimitiveTopology::Triangles:
                    indices.resize(indices.size() - (indices.size() % 3));
                    break;
                case PrimitiveTopology::Lines:
                    indices.resize(indices.size() - (indices.size() % 2));
                    break;
                // A strip, fan or loop is a single run rather than a sequence of independent
                // primitives, so there is no remainder to trim -- only a run too short to describe
                // one primitive at all, which draws nothing and is reported the same way.
                case PrimitiveTopology::TriangleStrip:
                case PrimitiveTopology::TriangleFan:
                    if (indices.size() < 3) { indices.clear(); }
                    break;
                case PrimitiveTopology::LineStrip:
                case PrimitiveTopology::LineLoop:
                    if (indices.size() < 2) { indices.clear(); }
                    break;
                case PrimitiveTopology::Points:
                    break;
            }
            out.droppedIncompleteIndicesEXT = before - indices.size();
        }

        // GLTF-072: a strip or fan becomes an equivalent triangle list here, winding preserved, so
        // everything downstream -- tangent generation, the packing loop, every renderer's
        // ApplyLayout, and the loaders' primitive count -- keeps working on triangle lists alone.
        // Deliberately after the bounds check, so an out-of-range index is reported against the
        // index the file actually authored rather than against a rewritten position.
        // GLTF-076: a LINE_LOOP becomes a LINE_STRIP carrying its own closing segment, before any
        // count is taken from the run.
        indices = CloseLineLoop(indices, sourceTopology);
        // GLTF-072: only the triangle topologies convert. A line or point run is already exactly
        // what its own draw consumes, and handing it to a triangle converter would be the
        // reinterpretation this whole track removed.
        if (ProducesTriangles(sourceTopology))
        {
            indices = NormalizeTriangleIndicesEXT(
                indices, sourceTopology, prim.has_draco_mesh_compression != 0);
        }

        const std::vector<float> positions = unpackSemantic(cgltf_attribute_type_position, 0, posAcc, 3, "POSITION");
        // Only the unskinned stride-24 (Position+Color+TextureCoordinate) and stride-20
        // (DualTextureEffect) layouts have no room for a per-vertex Normal.
        // plan_gltf.md GLTF-241: strides 24 and 20 have no Normal slot, so a primitive that lands
        // on one loses its authored NORMAL entirely -- and therefore cannot be lit at all, not
        // merely lit without a PBR material. That is the same limitation one layer deeper, and it
        // is recorded rather than left for a reader to deduce from a stride.
        const bool strideHasNormalSlot = (out.stride != 24 && out.stride != 20);
        if (normAcc != nullptr && !strideHasNormalSlot)
        {
            out.droppedNormalForStrideEXT = true;
        }
        std::vector<float> normals = (normAcc && strideHasNormalSlot)
            ? unpackSemantic(cgltf_attribute_type_normal, 0, normAcc, 3, "NORMAL") : std::vector<float>();

        // plan_gltf.md GLTF-173. §3.7.2.1: "When normals are not specified, client implementations
        // MUST calculate flat normals." CNA wrote a fabricated (0,0,1) for every vertex instead --
        // a surface facing +Z regardless of where it actually points, so a model lit from any other
        // direction was uniformly, silently wrong.
        //
        // What is computed here is the AREA-WEIGHTED VERTEX NORMAL: each triangle contributes its
        // own face normal, scaled by twice its area (the un-normalized cross product), to each of
        // its three vertices. For a mesh where no vertex is shared between faces of different
        // orientation -- which includes every faceted mesh whose author already split its edges --
        // that IS the flat normal, exactly. Where a vertex IS shared across differing faces, true
        // flat shading needs that vertex duplicated once per face, and duplication would change the
        // vertex count and every per-vertex stream including morph deltas; this extraction produces
        // one vertex array and cannot express it. Those vertices get the averaged normal and are
        // COUNTED, so the approximation is visible rather than assumed.
        //
        // Only for topologies that have faces at all. A point or line primitive has no surface, so
        // there is no normal to compute and the packing loop's own placeholder stands.
        if (normals.empty() && strideHasNormalSlot && ProducesTriangles(sourceTopology) &&
            !positions.empty() && indices.size() >= 3)
        {
            const std::size_t vertices = positions.size() / 3;
            std::vector<Vector3> accumulated(vertices);
            // -1 = untouched, -2 = shared across differing faces, otherwise the first face index.
            std::vector<int> firstFace(vertices, -1);
            std::vector<Vector3> faceNormal;
            faceNormal.reserve(indices.size() / 3);

            for (std::size_t f = 0; f + 2 < indices.size(); f += 3)
            {
                const std::uint32_t i0 = indices[f], i1 = indices[f + 1], i2 = indices[f + 2];
                if (i0 >= vertices || i1 >= vertices || i2 >= vertices) { continue; }
                const auto at = [&](std::uint32_t i) {
                    const std::size_t o = static_cast<std::size_t>(i) * 3;
                    return Vector3(positions[o], positions[o + 1], positions[o + 2]);
                };
                const Vector3 a = at(i0), b = at(i1), c = at(i2);
                // Un-normalized on purpose: its length is twice the triangle's area, which is the
                // weight a large face should carry over a sliver sharing the same vertex.
                const Vector3 weighted = Vector3::Cross(b - a, c - a);
                const int face = static_cast<int>(faceNormal.size());
                faceNormal.push_back(weighted);

                for (const std::uint32_t index : {i0, i1, i2})
                {
                    accumulated[index] = accumulated[index] + weighted;
                    int& first = firstFace[index];
                    if (first == -1) { first = face; }
                    else if (first >= 0)
                    {
                        const Vector3& other = faceNormal[static_cast<std::size_t>(first)];
                        const float lenProduct = other.Length() * weighted.Length();
                        // Degenerate faces have no orientation to disagree with.
                        if (lenProduct > 1e-12f &&
                            Vector3::Dot(other, weighted) / lenProduct < 0.99999f)
                        {
                            first = -2;
                        }
                    }
                }
            }

            normals.resize(vertices * 3);
            for (std::size_t v = 0; v < vertices; ++v)
            {
                Vector3 n = accumulated[v];
                const float length = n.Length();
                // A vertex touched by no face, or only by degenerate ones, has no computable
                // normal. glTF's own default up for such a case does not exist, so the packing
                // loop's placeholder is the honest answer and is reproduced here.
                n = (length > 1e-12f) ? Vector3(n.X / length, n.Y / length, n.Z / length)
                                      : Vector3(0.0f, 0.0f, 1.0f);
                normals[v * 3] = n.X;
                normals[v * 3 + 1] = n.Y;
                normals[v * 3 + 2] = n.Z;
                if (firstFace[v] == -2) { ++out.smoothedNormalVertexCountEXT; }
            }
            out.generatedNormalsEXT = true;
        }
        const cgltf_accessor* uvAcc = cgltf_find_accessor(
            &prim, cgltf_attribute_type_texcoord, out.packedTexcoordSourceSetsEXT[0]);
        std::vector<float> uvs = uvAcc
            ? unpackSemantic(cgltf_attribute_type_texcoord,
                             out.packedTexcoordSourceSetsEXT[0], uvAcc, 2, "TEXCOORD")
            : std::vector<float>();
        const cgltf_accessor* uv1Acc = out.hasSecondTexcoordEXT
            ? cgltf_find_accessor(&prim, cgltf_attribute_type_texcoord,
                                  out.packedTexcoordSourceSetsEXT[1])
            : nullptr;
        std::vector<float> uvs1 = uv1Acc
            ? unpackSemantic(cgltf_attribute_type_texcoord,
                             out.packedTexcoordSourceSetsEXT[1], uv1Acc, 2, "TEXCOORD")
            : std::vector<float>();
        std::vector<float> weights = out.skinned
            ? unpackSemantic(cgltf_attribute_type_weights, 0, weightsAcc, 4, "WEIGHTS_0") : std::vector<float>();

        // plan_gltf.md GLTF-256: RENORMALISE, and report when it was needed.
        //
        // §3.7.3.3 requires a vertex's joint weights to sum to 1, but a file is not guaranteed to
        // honour it and CNA never checked. The failure is not cosmetic: the skin equation is a
        // weighted sum of joint matrices, so weights summing to 0.75 produce 0.75 of the vertex's
        // transform -- which for a joint near the origin drags the vertex three-quarters of the way
        // toward it. That is H12, an independent collapse mechanism, and it looks exactly like the
        // defects this campaign exists to remove.
        //
        // Renormalising rather than refusing, because a slightly-off sum is what quantised
        // exporters routinely emit and refusing those files would be useless; and reporting,
        // because a sum that is *far* off is a different thing from float error and the caller
        // should be able to see the difference. An all-zero weight set is left alone: it means the
        // vertex is unweighted, and 0/0 is not a normalisation.
        // plan_gltf.md GLTF-095/GLTF-257: influence sets past the first.
        //
        // glTF allows any number of JOINTS_n/WEIGHTS_n sets, four joints each; XNA's BlendIndices
        // and BlendWeight carry exactly four, and no CNA vertex layout or shader has room for more.
        // So every set past the first is dropped -- which is a real decision, not an oversight, and
        // the only thing wrong with it before was that it happened in silence.
        //
        // The dropped share is measured here, BEFORE renormalisation, because that is the number
        // that says whether the truncation matters: a fifth influence weighted 0.002 is exporter
        // noise and one weighted 0.4 is a different pose. Renormalisation then rescales the four
        // retained weights to sum to 1, so the vertex ends up influenced by four joints instead of
        // eight rather than dragged toward the origin by the missing weight.
        if (out.skinned)
        {
            std::size_t extraSets = 0;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a)
            {
                const cgltf_attribute& attribute = prim.attributes[a];
                if ((attribute.type == cgltf_attribute_type_joints ||
                     attribute.type == cgltf_attribute_type_weights) &&
                    attribute.index > 0)
                {
                    ++extraSets;
                }
            }
            // JOINTS_n and WEIGHTS_n come in pairs (§3.7.3.3), so the pair count is the set count.
            out.extraInfluenceSetsEXT = (extraSets + 1) / 2;

            for (cgltf_size setIndex = 1; setIndex <= out.extraInfluenceSetsEXT; ++setIndex)
            {
                const cgltf_accessor* extra =
                    cgltf_find_accessor(&prim, cgltf_attribute_type_weights,
                                        static_cast<cgltf_int>(setIndex));
                if (extra == nullptr) { break; }
                const std::vector<float> dropped = UnpackAccessor(extra, 4, "WEIGHTS_n");
                for (std::size_t v = 0; v + 3 < dropped.size(); v += 4)
                {
                    const std::size_t vertex = v / 4;
                    const std::size_t base = vertex * 4;
                    if (base + 3 >= weights.size()) { break; }
                    const float kept = weights[base] + weights[base + 1] + weights[base + 2] +
                                       weights[base + 3];
                    const float lost = dropped[v] + dropped[v + 1] + dropped[v + 2] + dropped[v + 3];
                    const float total = kept + lost;
                    if (total > 0.0f)
                    {
                        out.worstDroppedInfluenceEXT =
                            std::max(out.worstDroppedInfluenceEXT, lost / total);
                    }
                }
            }
        }

        if (out.skinned && !weights.empty())
        {
            constexpr float kTolerance = 1e-4f;
            for (std::size_t v = 0; v + 3 < weights.size(); v += 4)
            {
                const float sum = weights[v] + weights[v + 1] + weights[v + 2] + weights[v + 3];
                if (sum <= 0.0f)
                {
                    ++out.zeroWeightVertexCountEXT;
                    continue;
                }
                if (std::fabs(sum - 1.0f) <= kTolerance) { continue; }
                // docs/gltf-conventions.md ("Where normals and tangents are renormalised")
                // records why this happens at import rather than in a shader: every renderer would
                // otherwise need the same correction, and one of them would eventually not have it.
                ++out.renormalisedWeightVertexCountEXT;
                out.worstWeightSumDeviationEXT =
                    std::max(out.worstWeightSumDeviationEXT, std::fabs(sum - 1.0f));
                const float inv = 1.0f / sum;
                weights[v] *= inv;
                weights[v + 1] *= inv;
                weights[v + 2] *= inv;
                weights[v + 3] *= inv;
            }
        }
        const std::vector<float> joints = out.skinned
            ? unpackSemantic(cgltf_attribute_type_joints, 0, jointsAcc, 4, "JOINTS_0") : std::vector<float>();
        // COLOR_0 may be VEC3 (RGB) or VEC4 (RGBA) per the glTF spec; a missing alpha defaults to
        // fully opaque. cgltf_accessor_unpack_floats/UnpackAccessor also transparently normalizes
        // whichever component type (FLOAT/normalized UBYTE/normalized USHORT) the file actually uses.
        const int colorComponents = colorAcc ? static_cast<int>(cgltf_num_components(colorAcc->type)) : 0;
        const std::vector<float> colors = out.colored
            ? unpackSemantic(cgltf_attribute_type_color, 0, colorAcc, colorComponents, "COLOR_0")
            : std::vector<float>();

        // plan_cnj.md CNB-57 (Phase 13A): PbrEffect's normal mapping needs a per-vertex tangent
        // basis -- use the file's own TANGENT accessor when present, or compute one (see
        // ComputeTangentsEXT's own doc comment for the algorithm and its documented divergence
        // from full MikkTSpace) when absent, exactly as the glTF spec itself recommends.
        std::vector<Vector4> tangents;
        // plan_gltf.md GLTF-086: only strides 48 and 68 have a tangent slot, and those are exactly
        // the PBR layouts -- so a file that authored a tangent basis for any other primitive has it
        // dropped. It cannot be carried: there is nowhere to put it. Reported instead, which is the
        // other outcome GLTF-086's acceptance allows, because a file that went to the trouble of
        // authoring tangents did so for a reason.
        if (!out.usePbr &&
            cgltf_find_accessor(&prim, cgltf_attribute_type_tangent, 0) != nullptr)
        {
            out.droppedTangentForStrideEXT = true;
        }
        if (out.usePbr)
        {
            const cgltf_accessor* tangentAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_tangent, 0);
            if (tangentAcc)
            {
                const std::vector<float> raw = unpackSemantic(cgltf_attribute_type_tangent, 0, tangentAcc, 4, "TANGENT");
                tangents.resize(vertexCount);
                for (cgltf_size v = 0; v < vertexCount; ++v)
                {
                    const std::size_t o = static_cast<std::size_t>(v) * 4;
                    tangents[v] = Vector4(raw[o], raw[o + 1], raw[o + 2], raw[o + 3]);
                }
            }
            else
            {
                const std::size_t normalSlot = static_cast<std::size_t>(TextureSlotEXT::Normal);
                const std::vector<float>& tangentUvs =
                    out.material.normalImage != nullptr &&
                            out.material.textureCoordinateSetsEXT[normalSlot] == 1
                        ? uvs1 : uvs;
                tangents = ComputeTangentsEXT(
                    positions, normals, tangentUvs, indices, vertexCount);
            }
        }

        for (cgltf_size i = 0; i < vertexCount; ++i)
        {
            const std::size_t i3 = static_cast<std::size_t>(i) * 3;
            const std::size_t i2 = static_cast<std::size_t>(i) * 2;
            const std::size_t i4 = static_cast<std::size_t>(i) * 4;

            const float px = positions[i3] * unitScale, py = positions[i3 + 1] * unitScale, pz = positions[i3 + 2] * unitScale;
            const float u = uvs.empty() ? 0.0f : uvs[i2];
            const float v = uvs.empty() ? 0.0f : uvs[i2 + 1];
            const float u1 = uvs1.empty() ? 0.0f : uvs1[i2];
            const float v1 = uvs1.empty() ? 0.0f : uvs1[i2 + 1];

            AppendFloat(out.vertexBytes, px); AppendFloat(out.vertexBytes, py); AppendFloat(out.vertexBytes, pz);

            const std::size_t co = static_cast<std::size_t>(i) * static_cast<std::size_t>(colorComponents);
            auto appendColor = [&]()
            {
                out.vertexBytes.push_back(ToByteColorChannel(colors[co]));
                out.vertexBytes.push_back(ToByteColorChannel(colors[co + 1]));
                out.vertexBytes.push_back(ToByteColorChannel(colors[co + 2]));
                out.vertexBytes.push_back(colorComponents >= 4 ? ToByteColorChannel(colors[co + 3]) : std::uint8_t{255});
            };
            // Shared by the usePbr (stride 68) and stride-32/52/56 branches below -- BlendWeight
            // (4 floats) followed by BlendIndices (4 bytes, remapped through skel->oldToNew).
            auto appendSkinning = [&]()
            {
                AppendFloat(out.vertexBytes, weights[i4]);     AppendFloat(out.vertexBytes, weights[i4 + 1]);
                AppendFloat(out.vertexBytes, weights[i4 + 2]); AppendFloat(out.vertexBytes, weights[i4 + 3]);

                for (int k = 0; k < 4; ++k)
                {
                    const std::size_t influence = i4 + static_cast<std::size_t>(k);
                    const int oldJointIdx = static_cast<int>(joints[influence] + 0.5f);
                    const bool inRange =
                        oldJointIdx >= 0 &&
                        static_cast<std::size_t>(oldJointIdx) < skel->oldToNew.size();

                    // plan_gltf.md GLTF-254. An index outside the skin's own joints array used to
                    // fall back to joint 0 unconditionally -- which binds the vertex to the root
                    // and drags it there, the same collapse this campaign exists to remove, from a
                    // malformed file rather than a bug.
                    //
                    // The two cases are genuinely different and are treated differently. A
                    // ZERO-WEIGHTED influence with a garbage index is the universal exporter
                    // padding pattern: an unused slot filled with whatever, contributing nothing to
                    // the skin equation. Refusing those would reject a large share of real assets
                    // for no gain, so they are clamped and left alone. A WEIGHTED influence naming
                    // a joint that does not exist is a broken file, and silently rebinding it to
                    // the root is the worst available answer.
                    if (!inRange && weights[influence] > 0.0f)
                    {
                        throw std::runtime_error(
                            "Primitive '" + name + "': vertex " + std::to_string(i4 / 4) +
                            " is weighted " + std::to_string(weights[influence]) + " to joint " +
                            std::to_string(oldJointIdx) + ", but the skin declares only " +
                            std::to_string(skel->oldToNew.size()) +
                            " joints. Binding it to the root instead would drag the vertex there, "
                            "so the file is refused (plan_gltf.md GLTF-254).");
                    }

                    const int newJointIdx =
                        inRange ? skel->oldToNew[static_cast<std::size_t>(oldJointIdx)] : 0;
                    // BlendIndices is one byte per influence. GLTF-261 caps a skin at 72 joints, so
                    // this cannot wrap -- but the two limits live far apart, and a silent wrap here
                    // would rebind a vertex to an arbitrary joint. Coupling them explicitly costs a
                    // comparison and removes the possibility.
                    if (newJointIdx < 0 || newJointIdx > 255)
                    {
                        throw std::runtime_error(
                            "Primitive '" + name + "': remapped joint index " +
                            std::to_string(newJointIdx) +
                            " does not fit the one-byte BlendIndices element.");
                    }
                    out.vertexBytes.push_back(static_cast<std::uint8_t>(newJointIdx));
                }
            };

            if (out.colored && !out.skinned)
            {
                // stride 24: Position + Color + TextureCoordinate.
                appendColor();
                AppendFloat(out.vertexBytes, u); AppendFloat(out.vertexBytes, v);
                continue;
            }

            if (out.usePbr)
            {
                // stride 48 (unskinned) / 68 (skinned): Position + Normal + Tangent +
                // TextureCoordinate [+ BlendWeight + BlendIndices].
                const float nx = normals.empty() ? 0.0f : normals[i3];
                const float ny = normals.empty() ? 0.0f : normals[i3 + 1];
                const float nz = normals.empty() ? 1.0f : normals[i3 + 2];
                AppendFloat(out.vertexBytes, nx); AppendFloat(out.vertexBytes, ny); AppendFloat(out.vertexBytes, nz);
                const Vector4& tan = tangents[i];
                AppendFloat(out.vertexBytes, tan.X); AppendFloat(out.vertexBytes, tan.Y);
                AppendFloat(out.vertexBytes, tan.Z); AppendFloat(out.vertexBytes, tan.W);
                AppendFloat(out.vertexBytes, u); AppendFloat(out.vertexBytes, v);
                if (out.skinned) { appendSkinning(); }
                if (out.hasSecondTexcoordEXT)
                {
                    AppendFloat(out.vertexBytes, u1); AppendFloat(out.vertexBytes, v1);
                    if (!out.skinned)
                    {
                        // The naturally 56-byte rigid record collides with skinned+colour. The
                        // canonical stride-60 layout reserves these four bytes as a discriminator.
                        AppendFloat(out.vertexBytes, 0.0f);
                    }
                }
                continue;
            }

            if (out.useDualTexture)
            {
                // stride 20: Position + TextureCoordinate.
                AppendFloat(out.vertexBytes, u); AppendFloat(out.vertexBytes, v);
                continue;
            }

            // stride 32/52/56: Position + Normal + TextureCoordinate [+ BlendWeight +
            // BlendIndices] [+ Color] -- Color, when present, is always appended last.
            const float nx = normals.empty() ? 0.0f : normals[i3];
            const float ny = normals.empty() ? 0.0f : normals[i3 + 1];
            const float nz = normals.empty() ? 1.0f : normals[i3 + 2];
            AppendFloat(out.vertexBytes, nx); AppendFloat(out.vertexBytes, ny); AppendFloat(out.vertexBytes, nz);
            AppendFloat(out.vertexBytes, u);  AppendFloat(out.vertexBytes, v);

            if (out.skinned)
            {
                appendSkinning();

                if (out.colored) { appendColor(); }
            }
        }

        // The narrowing below is safe only because every index was proved < vertexCount above: when
        // vertexCount <= 65535 the value therefore fits uint16 by construction, so the cast can no
        // longer silently wrap a large index into a small, wrong one.
        out.use32BitIndices = vertexCount > 65535;
        out.indexBytes.reserve(indices.size() *
                                (out.use32BitIndices ? sizeof(std::uint32_t) : sizeof(std::uint16_t)));
        for (std::uint32_t v : indices)
        {
            if (out.use32BitIndices) { AppendUint32(out.indexBytes, v); }
            else { AppendUint16(out.indexBytes, static_cast<std::uint16_t>(v)); }
        }

        // CNB-64 (Phase 13B): morph target position/normal deltas. plan_gltf.md GLTF-279 adds the
        // tangent ones: the scope cut that skipped them predated PbrEffect's real tangent-space
        // normal mapping, so a morphed PBR surface kept its rest-pose tangent basis while its
        // positions and normals moved -- normal mapping then lit the deformed surface with the
        // undeformed basis.
        // plan_gltf.md GLTF-290: the same sanity bound the .cnj reader has applied since CNB-83.
        // The two paths load the same content and had different limits: a target count the offline
        // reader refuses as impossible was accepted here and then allocated, one delta array per
        // target, from a number the file chose. Refusing at the same threshold on both paths is
        // what makes the limit a property of CNA rather than of whichever loader was used.
        constexpr cgltf_size kMaxSaneTargetCount = 100000;
        if (prim.targets_count > kMaxSaneTargetCount)
        {
            throw std::runtime_error(
                "Primitive '" + name + "' declares " + std::to_string(prim.targets_count) +
                " morph targets, above the " + std::to_string(kMaxSaneTargetCount) +
                " CNA accepts on either load path.");
        }

        out.morphPositionDeltas.resize(prim.targets_count);
        out.morphNormalDeltas.resize(prim.targets_count);
        out.morphTangentDeltas.resize(prim.targets_count);
        for (cgltf_size ti = 0; ti < prim.targets_count; ++ti)
        {
            const cgltf_morph_target& target = prim.targets[ti];

            // plan_gltf.md GLTF-292. The resize is INSIDE the branch, and that is the fix rather
            // than a detail: it used to run unconditionally, so a target authoring no POSITION got
            // a zero-filled delta array indistinguishable from one authoring zeros. Two things
            // followed. `MorphReportEXT::targetsWithoutPositions` could never fire -- a diagnostic
            // that structurally cannot report -- and the blend added a zero vector per vertex per
            // such target. Normals and tangents already used emptiness to mean "this target
            // contributes nothing"; positions now do too, which is one rule instead of two.
            const cgltf_accessor* posDeltaAcc = FindMorphTargetAttribute(target, cgltf_attribute_type_position);
            if (posDeltaAcc)
            {
                out.morphPositionDeltas[ti].resize(vertexCount);
                const std::vector<float> deltas = UnpackAccessor(posDeltaAcc, 3, "morph target POSITION delta");
                for (cgltf_size v = 0; v < vertexCount; ++v)
                {
                    const std::size_t o = static_cast<std::size_t>(v) * 3;
                    out.morphPositionDeltas[ti][v] = Vector3(
                        deltas[o] * unitScale, deltas[o + 1] * unitScale, deltas[o + 2] * unitScale);
                }
            }
            // else: leave the zero-initialized Vector3 default (a target with no POSITION delta
            // at all is unusual but spec-legal).

            const cgltf_accessor* normDeltaAcc = FindMorphTargetAttribute(target, cgltf_attribute_type_normal);
            if (normDeltaAcc)
            {
                const std::vector<float> deltas = UnpackAccessor(normDeltaAcc, 3, "morph target NORMAL delta");
                out.morphNormalDeltas[ti].resize(vertexCount);
                for (cgltf_size v = 0; v < vertexCount; ++v)
                {
                    const std::size_t o = static_cast<std::size_t>(v) * 3;
                    out.morphNormalDeltas[ti][v] = Vector3(deltas[o], deltas[o + 1], deltas[o + 2]);
                }
            }
            // else: leave out.morphNormalDeltas[ti] empty -- signals "no normal delta for this
            // target" to SetMorphWeightsEXT, which then leaves the base normal unchanged.

            // GLTF-279. A morph TANGENT delta is VEC3, never VEC4: §3.7.2.2 morphs the tangent
            // DIRECTION only, because the handedness `w` describes the UV winding and cannot be
            // interpolated meaningfully -- blending +1 and -1 would pass through 0, which is not a
            // handedness at all. Unpacked as 3 components so the base vertex's own `w` survives by
            // construction rather than by a rule someone has to remember.
            const cgltf_accessor* tangentDeltaAcc =
                FindMorphTargetAttribute(target, cgltf_attribute_type_tangent);
            if (tangentDeltaAcc)
            {
                const std::vector<float> deltas =
                    UnpackAccessor(tangentDeltaAcc, 3, "morph target TANGENT delta");
                out.morphTangentDeltas[ti].resize(vertexCount);
                for (cgltf_size v = 0; v < vertexCount; ++v)
                {
                    const std::size_t o = static_cast<std::size_t>(v) * 3;
                    out.morphTangentDeltas[ti][v] = Vector3(deltas[o], deltas[o + 1], deltas[o + 2]);
                }
            }
        }

        return out;
    }

    std::vector<MaterialVariantOutEXT> ExtractMaterialVariantsEXT(
        const cgltf_data* data, const cgltf_primitive& prim, const std::string& name,
        const SkeletonResult* skel, float unitScale)
    {
        std::vector<MaterialVariantOutEXT> result;
        if (data == nullptr || prim.mappings_count == 0) { return result; }

        std::vector<bool> seen(static_cast<std::size_t>(data->variants_count), false);
        result.reserve(static_cast<std::size_t>(prim.mappings_count));
        for (cgltf_size i = 0; i < prim.mappings_count; ++i)
        {
            const cgltf_material_mapping& mapping = prim.mappings[i];
            if (mapping.variant >= data->variants_count)
            {
                throw std::runtime_error(
                    "Primitive '" + name + "' maps material variant index " +
                    std::to_string(mapping.variant) + ", but the file declares only " +
                    std::to_string(data->variants_count) + " variant(s).");
            }
            if (mapping.material == nullptr)
            {
                throw std::runtime_error(
                    "Primitive '" + name + "' has a material-variant mapping with no material.");
            }
            if (seen[static_cast<std::size_t>(mapping.variant)])
            {
                throw std::runtime_error(
                    "Primitive '" + name + "' maps material variant index " +
                    std::to_string(mapping.variant) +
                    " more than once; one selected variant cannot choose two materials.");
            }
            seen[static_cast<std::size_t>(mapping.variant)] = true;

            cgltf_primitive variantPrimitive = prim;
            variantPrimitive.material = mapping.material;
            // The copied mappings describe choices *from* the default primitive and are not part
            // of the selected material state itself. Clearing them also makes accidental recursive
            // extraction impossible if this helper is reused later.
            variantPrimitive.mappings = nullptr;
            variantPrimitive.mappings_count = 0;

            MaterialVariantOutEXT out;
            out.variantIndex = static_cast<std::size_t>(mapping.variant);
            out.mesh = ExtractMesh(data, variantPrimitive, name, skel, unitScale);
            result.push_back(std::move(out));
        }
        return result;
    }

    SceneGraphOut BuildSceneGraph(const cgltf_data* data)
    {
        SceneGraphOut graph;

        // Index 0 is a synthetic identity root. glTF scenes may have several root nodes while CNA's
        // Model has exactly one Root bone, so inventing an identity parent is the only mapping that
        // adds no transform of its own.
        SceneNodeOut root;
        root.name = "Root";
        graph.nodes.push_back(root);

        const cgltf_scene* scene = data->scene
            ? data->scene
            : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);

        // Explicit stack rather than recursion: a pathological file may nest nodes thousands deep,
        // and CollectSceneReachableNodes already established that convention for this traversal.
        // Pushing children in reverse keeps the visit order equal to the file's own child order.
        struct PendingNode { const cgltf_node* node; int parentIndex; };
        std::vector<PendingNode> stack;
        stack.reserve(data->nodes_count + 1);
        if (scene != nullptr)
        {
            for (cgltf_size i = scene->nodes_count; i > 0; --i)
            {
                stack.push_back(PendingNode{scene->nodes[i - 1], 0});
            }
        }
        else
        {
            // plan_gltf.md GLTF-399 / `scene-no-scenes`. §3.5 permits a file with no `scenes` array
            // and says nothing is *required* to be rendered -- which is not "nothing may be", and
            // CNA's own decision (docs/gltf-conventions.md) is to import everything. This used to
            // return an EMPTY graph here, so the caller's "fall back to every mesh" imported the
            // geometry with **no placement at all**: every mesh at the origin, node transforms
            // discarded. That is defect D1's failure mode surviving in the one corner the scene
            // traversal never covered. Walking the roots instead keeps the fallback's reach and
            // gives every mesh the transform the file authored.
            for (cgltf_size i = data->nodes_count; i > 0; --i)
            {
                if (data->nodes[i - 1].parent == nullptr)
                {
                    stack.push_back(PendingNode{&data->nodes[i - 1], 0});
                }
            }
        }
        if (stack.empty()) { return graph; }

        while (!stack.empty())
        {
            const PendingNode pending = stack.back();
            stack.pop_back();
            const cgltf_node* node = pending.node;
            if (!node) { continue; }
            // A node reachable by two paths is imported once, at the first path found -- matching
            // CollectSceneReachableNodes' own de-duplication.
            if (graph.indexOfNode.find(node) != graph.indexOfNode.end()) { continue; }

            SceneNodeOut out;
            out.name = node->name ? node->name : ("Node" + std::to_string(graph.nodes.size()));
            out.parentIndex = pending.parentIndex;
            out.node = node;
            out.gltfNodeIndex = static_cast<int>(node - data->nodes);

            // cgltf_node_transform_local already applies the spec's own "matrix, or else TRS"
            // exclusivity rule, so this needs no separate branch of its own.
            float localMat[16];
            cgltf_node_transform_local(node, localMat);
            out.localTransform = ConvertGltfMatrix(localMat);
            // Row-vector composition: the parent is always already finalized, because a node is
            // only ever pushed with a parent index that was assigned before it.
            out.worldTransform = out.localTransform * graph.nodes[static_cast<std::size_t>(pending.parentIndex)].worldTransform;

            const int index = static_cast<int>(graph.nodes.size());
            graph.indexOfNode[node] = index;
            graph.nodes.push_back(std::move(out));

            for (cgltf_size c = node->children_count; c > 0; --c)
            {
                stack.push_back(PendingNode{node->children[c - 1], index});
            }
        }

        return graph;
    }

    std::vector<CameraOut> ExtractCamerasEXT(const cgltf_data* data, const SceneGraphOut& scene,
                                              float unitScale)
    {
        std::vector<CameraOut> out;
        if (data == nullptr) { return out; }

        // Walk the SCENE graph, not data->cameras: a camera is only in the render if a node in the
        // default scene instances it, and the same camera may be instanced by several nodes -- each
        // of which is a distinct camera placement. Walking the camera array instead would both
        // import cameras nobody placed and collapse the multi-instance case to one.
        for (std::size_t i = 0; i < scene.nodes.size(); ++i)
        {
            const SceneNodeOut& node = scene.nodes[i];
            if (node.node == nullptr || node.node->camera == nullptr) { continue; }
            const cgltf_camera& camera = *node.node->camera;

            CameraOut record;
            record.sceneNodeIndex = static_cast<int>(i);
            record.name = camera.name != nullptr ? camera.name : node.name;
            record.worldTransform = ScaleTranslation(node.worldTransform, unitScale);

            if (camera.type == cgltf_camera_type_orthographic)
            {
                record.perspective = false;
                record.xmag  = camera.data.orthographic.xmag;
                record.ymag  = camera.data.orthographic.ymag;
                record.znear = camera.data.orthographic.znear;
                record.zfar  = camera.data.orthographic.zfar;
            }
            else
            {
                record.perspective = true;
                record.yfov  = camera.data.perspective.yfov;
                record.znear = camera.data.perspective.znear;
                // Both of these are genuinely optional in the file, and both are carried as 0
                // rather than guessed: an absent aspectRatio means "the viewport's", which the
                // importer cannot know, and an absent zfar means an INFINITE projection (§3.10.3),
                // which is a different matrix rather than a large number (GLTF-319).
                record.aspectRatio = camera.data.perspective.has_aspect_ratio != 0
                                         ? camera.data.perspective.aspect_ratio : 0.0f;
                record.zfar = camera.data.perspective.has_zfar != 0
                                  ? camera.data.perspective.zfar : 0.0f;
            }
            out.push_back(std::move(record));
        }
        return out;
    }

    SamplerOut MapGltfSamplerEXT(int magFilter, int minFilter, int wrapS, int wrapT)
    {
        using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
        using Microsoft::Xna::Framework::Graphics::TextureFilter;

        // glTF's minFilter packs two independent decisions into one enum: how minification filters,
        // and whether/how it blends between mip levels. Splitting them is what makes the mapping a
        // table lookup instead of nine special cases.
        bool minLinear = true;   // §3.8.4's default when undefined
        bool mipLinear = true;
        bool noMipStage = false;
        bool requiresMipChain = false;
        switch (minFilter)
        {
            case 9728: minLinear = false; noMipStage = true;  break;  // NEAREST
            case 9729: minLinear = true;  noMipStage = true;  break;  // LINEAR
            case 9984: minLinear = false; mipLinear = false; requiresMipChain = true; break;
            case 9985: minLinear = true;  mipLinear = false; requiresMipChain = true; break;
            case 9986: minLinear = false; mipLinear = true;  requiresMipChain = true; break;
            case 9987: minLinear = true;  mipLinear = true;  requiresMipChain = true; break;
            default:   break;                                          // undefined
        }
        // A minFilter with no mip stage means "use the base level only", which XNA expresses
        // through the texture's level count rather than through TextureFilter. The mip mode is
        // therefore arbitrary here; point is chosen as the least-blending option, and the caller
        // is told so via SamplerOut::minFilterHasNoMipStage rather than left to infer it.
        if (noMipStage) { mipLinear = false; }

        const bool magLinear = (magFilter != 9728);   // NEAREST is the only non-linear value

        SamplerOut out;
        // All eight min x mag x mip combinations, each to the XNA value that means exactly it.
        // XNA covers glTF's filter space completely, so none of these is an approximation.
        if (minLinear && magLinear)
        {
            out.filter = mipLinear ? TextureFilter::Linear : TextureFilter::LinearMipPoint;
        }
        else if (!minLinear && !magLinear)
        {
            out.filter = mipLinear ? TextureFilter::PointMipLinear : TextureFilter::Point;
        }
        else if (minLinear && !magLinear)
        {
            out.filter = mipLinear ? TextureFilter::MinLinearMagPointMipLinear
                                   : TextureFilter::MinLinearMagPointMipPoint;
        }
        else
        {
            out.filter = mipLinear ? TextureFilter::MinPointMagLinearMipLinear
                                   : TextureFilter::MinPointMagLinearMipPoint;
        }
        out.minFilterHasNoMipStage = noMipStage;
        out.minFilterRequiresMipChain = requiresMipChain;

        const auto address = [](int wrap) {
            switch (wrap)
            {
                case 33071: return TextureAddressMode::Clamp;    // CLAMP_TO_EDGE
                case 33648: return TextureAddressMode::Mirror;   // MIRRORED_REPEAT
                case 10497:                                       // REPEAT
                default:    return TextureAddressMode::Wrap;      // §3.8.4's default
            }
        };
        out.addressU = address(wrapS);
        out.addressV = address(wrapT);
        return out;
    }

    std::vector<MeshGroup> CollectMeshGroups(const cgltf_data* data, const SceneGraphOut& scene)
    {
        std::vector<MeshGroup> groups;
        std::unordered_map<const cgltf_skin*, std::size_t> indexOfSkin;

        // Iterating data->nodes (rather than the scene graph's own order) keeps this function's
        // established group ordering: groups appear in glTF node-array order, which several
        // existing tests and the offline tool's own "_static"/"_<skinName>" output naming rely on.
        for (cgltf_size i = 0; i < data->nodes_count; ++i)
        {
            const cgltf_node& node = data->nodes[i];
            if (!node.mesh) { continue; }
            const auto placed = scene.indexOfNode.find(&node);
            // A node outside the default scene is not imported. The one exception is a file with
            // no scenes at all, where the graph is just the root and the fallback below applies.
            if (placed == scene.indexOfNode.end()) { continue; }

            MeshInstanceOut instance;
            instance.node = &node;
            instance.mesh = node.mesh;
            instance.sceneNodeIndex = placed->second;
            instance.worldTransform = scene.nodes[static_cast<std::size_t>(placed->second)].worldTransform;
            instance.skinned = node.skin != nullptr;
            // GLTF-116/GLTF-117: mirroring is a property of the COMPOSED transform. The 3x3
            // determinant is the whole test -- an odd number of mirroring ancestors flips the
            // handedness, an even number restores it, and the node's own scale says nothing on its
            // own. A skinned instance is deliberately measured the same way even though its node
            // transform is ignored for placement, so the flag never silently means two things.
            instance.mirroredEXT = Determinant3x3(instance.worldTransform) < 0.0f;

            auto it = indexOfSkin.find(node.skin);
            if (it == indexOfSkin.end())
            {
                indexOfSkin[node.skin] = groups.size();
                groups.push_back(MeshGroup{node.skin, {std::move(instance)}});
            }
            else
            {
                groups[it->second].instances.push_back(std::move(instance));
            }
        }

        // Fallback for files where meshes exist but no scene node references them (unusual but
        // not invalid) -- treat every mesh in the file as one unskinned group placed at the
        // identity root, matching this library's original node-graph-independent behavior.
        if (groups.empty())
        {
            MeshGroup g;
            for (cgltf_size i = 0; i < data->meshes_count; ++i)
            {
                MeshInstanceOut instance;
                instance.mesh = &data->meshes[i];
                g.instances.push_back(std::move(instance));
            }
            if (!g.instances.empty()) { groups.push_back(std::move(g)); }
        }

        return groups;
    }

    std::vector<MeshGroup> CollectMeshGroups(const cgltf_data* data)
    {
        return CollectMeshGroups(data, BuildSceneGraph(data));
    }

    SkinReportEXT BuildSkinReportEXT(const MeshOut& mesh, const SkeletonResult* skeleton)
    {
        SkinReportEXT report;
        report.jointCount = skeleton != nullptr ? static_cast<int>(skeleton->bones.size()) : 0;
        report.droppedInfluenceSets = static_cast<int>(mesh.extraInfluenceSetsEXT);
        report.worstDroppedInfluence = mesh.worstDroppedInfluenceEXT;
        report.renormalisedVertexCount = mesh.renormalisedWeightVertexCountEXT;
        report.worstWeightSumDeviation = mesh.worstWeightSumDeviationEXT;
        report.hasDeclaredSkeletonRoot =
            skeleton != nullptr && skeleton->declaredSkeletonRootNodeIndex >= 0;
        return report;
    }

    MorphReportEXT BuildMorphReportEXT(const MeshOut& mesh,
                                        const std::vector<float>& defaultWeights)
    {
        MorphReportEXT report;
        report.targetCount = static_cast<int>(mesh.morphPositionDeltas.size());
        for (std::size_t t = 0; t < mesh.morphPositionDeltas.size(); ++t)
        {
            if (mesh.morphPositionDeltas[t].empty()) { ++report.targetsWithoutPositions; }
            if (t >= mesh.morphNormalDeltas.size() || mesh.morphNormalDeltas[t].empty())
            {
                ++report.targetsWithoutNormals;
            }
            if (t >= mesh.morphTangentDeltas.size() || mesh.morphTangentDeltas[t].empty())
            {
                ++report.targetsWithoutTangents;
            }
        }
        report.hasNonZeroDefaultWeights =
            std::any_of(defaultWeights.begin(), defaultWeights.end(),
                        [](float w) { return w != 0.0f; });
        return report;
    }

    NodeGraphReportEXT BuildNodeGraphReportEXT(const SceneGraphOut& scene,
                                               const std::vector<MeshGroup>& groups)
    {
        NodeGraphReportEXT report;

        // Index 0 is the synthetic identity root, which is CNA's own invention rather than
        // anything the file authored -- counting it would make an empty scene report one node.
        report.nodeCount = scene.nodes.empty() ? 0 : static_cast<int>(scene.nodes.size()) - 1;

        // Depth is computed by walking each node's parent chain rather than during the traversal,
        // because the traversal is iterative and its stack depth is not the graph's depth.
        std::vector<int> depth(scene.nodes.size(), 0);
        for (std::size_t i = 1; i < scene.nodes.size(); ++i)
        {
            const int parent = scene.nodes[i].parentIndex;
            depth[i] = (parent >= 0 ? depth[static_cast<std::size_t>(parent)] : 0) + 1;
            report.maxDepth = std::max(report.maxDepth, depth[i]);

            const cgltf_node* node = scene.nodes[i].node;
            if (node == nullptr) { continue; }
            if (node->camera != nullptr) { ++report.cameraNodeCount; }
            if (node->light != nullptr) { ++report.lightNodeCount; }
            if (node->has_mesh_gpu_instancing) { ++report.gpuInstancedNodeCount; }
        }

        std::unordered_map<const cgltf_mesh*, int> placementsOfMesh;
        for (const MeshGroup& group : groups)
        {
            for (const MeshInstanceOut& instance : group.instances)
            {
                ++report.meshInstanceCount;
                if (instance.mesh != nullptr) { ++placementsOfMesh[instance.mesh]; }
            }
        }
        report.distinctMeshCount = static_cast<int>(placementsOfMesh.size());
        for (const auto& [mesh, count] : placementsOfMesh)
        {
            (void)mesh;
            if (count > 1) { ++report.sharedMeshCount; }
        }
        return report;
    }

    // plan_gltf.md GLTF-099. §2.3's stride table as DATA. It was a nested ternary chain, which has
    // three problems a table does not: it cannot be enumerated (so no test can walk its rows), it
    // cannot be asked what a row loses (GLTF-100), and every renderer's ApplyLayout is an implicit
    // restatement of the same rule with no way to check the two agree.
    //
    // Order matters: the first matching row wins, and `skinned` is tested before everything else
    // because the skinned layouts are a different family rather than a variation.
    const std::vector<VertexLayoutRuleEXT>& VertexLayoutTableEXT()
    {
        static const std::vector<VertexLayoutRuleEXT> table = {
            // Skinned. A skinned primitive always carries a Normal, so nothing here loses one.
            {{true, true, false, false, false}, 56,
             "the material's PBR factors and maps: no PBR shader reads a colour stream"},
            {{true, true, true, false, false}, 56,
             "the material's PBR factors and maps: no PBR shader reads a colour stream"},
            {{true, false, true, false, true}, 76, ""},
            {{true, false, true, false, false}, 68, ""},
            {{true, false, false, false, false}, 52, ""},
            // Unskinned, coloured. Stride 24 is XNA's own VertexPositionColorTexture, which has no
            // Normal slot at all -- so a coloured primitive loses its normals whatever its
            // material, and its PBR material on top of that (GLTF-241/GLTF-085).
            {{false, true, true, false, false}, 24,
             "the Normal stream and the material's PBR factors and maps: stride 24 has no normal "
             "slot and no PBR shader reads a colour stream"},
            {{false, true, false, false, false}, 24,
             "the Normal stream: stride 24 (Position+Color+TextureCoordinate) has no normal slot"},
            // Unskinned, uncoloured.
            {{false, false, true, false, true}, 60, ""},
            {{false, false, true, false, false}, 48, ""},
            {{false, false, false, true, false}, 20,
             "the Normal and Tangent streams: DualTextureEffect's layout is "
             "Position+TextureCoordinate only, so the primitive cannot be lit"},
            {{false, false, false, false, false}, 32, ""},
        };
        return table;
    }

    const VertexLayoutRuleEXT& SelectVertexLayoutEXT(const VertexLayoutRequestEXT& request)
    {
        const std::vector<VertexLayoutRuleEXT>& table = VertexLayoutTableEXT();
        for (const VertexLayoutRuleEXT& rule : table)
        {
            // useDualTexture is only ever set for an unskinned, uncoloured, non-PBR primitive, so
            // matching on the other three and letting the dual-texture row sit ahead of the plain
            // stride-32 one keeps the table readable without a don't-care column.
            if (rule.request.skinned == request.skinned &&
                rule.request.colored == request.colored &&
                rule.request.usePbr == request.usePbr &&
                rule.request.hasSecondTexcoord == request.hasSecondTexcoord &&
                (rule.request.useDualTexture == request.useDualTexture ||
                 (!rule.request.useDualTexture && !request.useDualTexture)))
            {
                return rule;
            }
        }
        // Total by construction -- the table covers every reachable combination -- but a fallback
        // that silently produced a plausible stride is exactly the failure GLTF-100 is about, so
        // the last row (the plain unskinned layout) is returned explicitly rather than by accident.
        return table.back();
    }

    std::string GltfExtensionSupportNameEXT(GltfExtensionSupportEXT support)
    {
        switch (support)
        {
        case GltfExtensionSupportEXT::Implemented:               return "IMPLEMENTED_AND_TESTED";
        case GltfExtensionSupportEXT::ImplementedWithNamedLimit: return "IMPLEMENTED_WITH_A_NAMED_LIMIT";
        case GltfExtensionSupportEXT::Approximated:              return "APPROXIMATED_AND_REPORTED";
        case GltfExtensionSupportEXT::ParsedButIgnored:          return "PARSED_BUT_IGNORED";
        case GltfExtensionSupportEXT::Unsupported:               return "UNSUPPORTED";
        case GltfExtensionSupportEXT::NotDesired:                return "NOT_DESIRED";
        }
        return "UNSUPPORTED";
    }

    // plan_gltf.md GLTF-334. One table, and everything about extensions reads from it: the
    // extensionsRequired gate (IsGltfExtensionSupportedEXT below), §19's own classification table
    // (asserted against this by GltfExtensionRegistry), docs/gltf-limitations.md §1 (asserted by
    // GltfLimitationsDoc, which additionally compares the `claimed` column -- the one a caller
    // acts on), and the fixture-coverage rule of GLTF-335.
    //
    // Two hand-maintained lists is the failure this replaces. The support list and §19's table
    // already disagreed in spirit -- §19 called KHR_texture_transform PARTIAL while the gate
    // claimed it -- and neither said why. The `claimed` column now carries that decision
    // explicitly, per record, instead of leaving it implicit in which of two files you read.
    const std::vector<GltfExtensionRecordEXT>& GltfExtensionRegistryEXT()
    {
        static const std::vector<GltfExtensionRecordEXT> registry = [] {
            std::vector<GltfExtensionRecordEXT> out = {
                {"KHR_texture_transform", GltfExtensionSupportEXT::Implemented, true,
                 "All supported PBR maps retain independent offset/rotation/scale and texCoord state "
                 "through direct and offline loading. Every PBR renderer applies the resulting "
                 "affine rows, and a discriminating EasyGL L7 fixture proves two maps sharing one "
                 "authored UV stream can use different transforms.",
                 "GLTF-336"},
                {"KHR_mesh_quantization", GltfExtensionSupportEXT::Implemented, true,
                 "Integer mesh attributes are decoded through the same normalized-accessor path "
                 "as core formats and repacked into CNA's float vertex layouts. BYTE and SHORT "
                 "normal witnesses pin the signed clamp and the extension's 4-byte VEC3 element "
                 "alignment.",
                 "GLTF-084"},
                {"KHR_materials_emissive_strength", GltfExtensionSupportEXT::ImplementedWithNamedLimit,
                 true,
                 "Applied on the PBR path. A non-PBR material has no emissive term to scale, so "
                 "the strength has nowhere to go there.",
                 "GLTF-222"},
                {"KHR_lights_punctual", GltfExtensionSupportEXT::Approximated, true,
                 "Up to three directional lights, which is XNA's whole lighting model. Point and "
                 "spot become directional lights aimed at the origin, ranges and cones are "
                 "ignored, and an out-of-gamut intensity clamps -- every loss counted.",
                 "GLTF-325"},
                {"KHR_draco_mesh_compression", GltfExtensionSupportEXT::Implemented,
                 kDracoAvailable,
                 "Decoded when the build has libdraco. Claimed only in such a build: claiming it "
                 "without the decoder would accept a file whose geometry then arrives empty.",
                 "GLTF-353"},
                {"KHR_materials_transmission", GltfExtensionSupportEXT::Approximated, false,
                 "alpha = 1 - transmissionFactor, multiplied into the material's own alpha. Not "
                 "physical in four named ways, so a file that REQUIRES transmission is refused "
                 "rather than loaded with its glass drawn as tinted alpha.",
                 "GLTF-339"},
                {"KHR_texture_basisu", GltfExtensionSupportEXT::Unsupported, false,
                 "No KTX2 decoder. A texture's plain PNG/JPEG fallback is used when the file "
                 "provides one, and the loss is named per map when it does not.",
                 "GLTF-350"},
                {"EXT_texture_webp", GltfExtensionSupportEXT::Unsupported, false,
                 "No WebP decoder; same three outcomes as KHR_texture_basisu.",
                 "GLTF-350"},
                {"KHR_materials_unlit", GltfExtensionSupportEXT::ImplementedWithNamedLimit, true,
                 "Maps to LightingEnabled = false on BasicEffect, with baseColorFactor as the "
                 "diffuse colour. SkinnedEffect has no such flag -- real XNA's has none either -- "
                 "so a skinned unlit material is approximated with an all-white ambient and no "
                 "directional light, which is unlit apart from any specular term.",
                 "GLTF-337"},
                {"KHR_materials_pbrSpecularGlossiness", GltfExtensionSupportEXT::Approximated,
                 false,
                 "Archived by Khronos but present in older assets, so converted rather than "
                 "refused: diffuse becomes the base colour, metallic 0, roughness 1 - glossiness. "
                 "Not claimed, because specularFactor -- a coloured specular reflection -- has no "
                 "metallic-roughness equivalent, so a file REQUIRING the extension is asking for "
                 "something the conversion cannot deliver.",
                 "GLTF-349"},
                {"KHR_materials_variants", GltfExtensionSupportEXT::Implemented, true,
                 "The source-order variant table and sparse primitive mappings are preserved. "
                 "Model's CNAEXT selection API swaps the complete material-dependent part state, "
                 "including effect, vertex layout, textures and samplers, on both direct glTF and "
                 "offline .cnj paths while leaving the default mapping unchanged.",
                 "GLTF-341"},
                {"KHR_materials_ior", GltfExtensionSupportEXT::Implemented, true,
                 "IOR is converted to dielectric F0/F90 and consumed by rigid and skinned PBR "
                 "shaders on all 15 PBR renderers. Analytic factor-only and grazing pixel "
                 "witnesses cover the core default and authored endpoints.",
                 "GLTF-343"},
                {"KHR_materials_specular", GltfExtensionSupportEXT::ImplementedWithNamedLimit,
                 false,
                 "Factor and colour are converted to dielectric F0/F90 and consumed by all 15 "
                 "PBR renderers. The optional specularTexture and specularColorTexture now survive "
                 "direct import and offline .cnj with independent UV, transform, sampler and "
                 "colour-space state. EasyGL, OpenGL2, OpenGL4, DirectX9, DirectX11, DirectX12, "
                 "Bgfx, Diligent, Magnum, SDL GPU and Vulkan sample both maps; the remaining 4 PBR renderer shader bindings are pending. "
                 "Required use remains refused and optional use is warned by name.",
                 "GLTF-344"},
                {"KHR_materials_clearcoat", GltfExtensionSupportEXT::ParsedButIgnored, false,
                 "A second specular lobe -- a large shader change.",
                 "GLTF-345"},
                {"KHR_materials_sheen", GltfExtensionSupportEXT::ParsedButIgnored, false,
                 "A third BRDF lobe, same shape of change as clearcoat.",
                 "GLTF-346"},
                {"KHR_materials_volume", GltfExtensionSupportEXT::ParsedButIgnored, false,
                 "Meaningless without a real transmission pass, which CNA does not have.",
                 "GLTF-347"},
                {"EXT_meshopt_compression", GltfExtensionSupportEXT::Unsupported, false,
                 "cgltf validates the compression metadata but decoding needs a caller-supplied "
                 "hook CNA does not provide, and without one an accessor over a compressed view "
                 "reads undefined bytes rather than failing. Refused at validation instead.",
                 "GLTF-351"},
                {"EXT_mesh_gpu_instancing", GltfExtensionSupportEXT::Unsupported, false,
                 "Each node's own single placement is imported and the per-instance transforms are "
                 "not, so the file renders one copy where it describes many. Reported per file.",
                 "GLTF-352"},
                // GLTF-348. NOT_DESIRED is a decision, not a backlog entry: each of these is a
                // thin-film or directional-scattering term that needs a BRDF CNA's stock effects
                // do not have, and adding one would change the shading of every PBR material to
                // serve an extension almost no asset uses. Recorded so a future reader finds a
                // reason rather than an omission -- and, because none is claimed, a file that
                // REQUIRES one is still refused by name rather than loading and looking wrong.
                {"KHR_materials_iridescence", GltfExtensionSupportEXT::NotDesired, false,
                 "A thin-film interference term with no counterpart in any CNA stock effect. Not "
                 "planned: the shader cost falls on every PBR material to serve a rare one.",
                 "GLTF-348"},
                {"KHR_materials_anisotropy", GltfExtensionSupportEXT::NotDesired, false,
                 "Needs a tangent-aligned specular lobe, and therefore a reliable tangent basis on "
                 "every affected primitive -- which GLTF-086 shows CNA cannot carry at most strides.",
                 "GLTF-348"},
                {"KHR_materials_dispersion", GltfExtensionSupportEXT::NotDesired, false,
                 "Wavelength-dependent refraction, which presupposes the refraction pass "
                 "KHR_materials_transmission is explicitly approximated instead of implementing.",
                 "GLTF-348"},
            };
            return out;
        }();
        return registry;
    }

    const GltfExtensionRecordEXT* FindGltfExtensionEXT(const std::string& extension)
    {
        for (const GltfExtensionRecordEXT& record : GltfExtensionRegistryEXT())
        {
            if (record.name == extension) { return &record; }
        }
        return nullptr;
    }

    bool IsGltfExtensionSupportedEXT(const std::string& extension)
    {
        // A lookup, not a second list (GLTF-334). Only what the importer actually implements the
        // semantics of. Detecting an extension so it cannot mis-select an effect is NOT
        // implementing it: KHR_materials_unlit and KHR_materials_pbrSpecularGlossiness are both
        // read by ExtractMesh to keep such a material off the metallic-roughness path (GLTF-215),
        // yet an unlit surface still goes through a lit effect and the specular-glossiness
        // parameters are dropped, so a file that *requires* either is asking for something CNA
        // cannot deliver.
        const GltfExtensionRecordEXT* record = FindGltfExtensionEXT(extension);
        return record != nullptr && record->claimed;
    }

    void CrossCheckAccessorBoundsEXT(const cgltf_data* data, std::vector<std::string>& warnings)
    {
        if (data == nullptr) { return; }

        for (cgltf_size i = 0; i < data->accessors_count; ++i)
        {
            const cgltf_accessor& accessor = data->accessors[i];
            if (accessor.has_min == 0 && accessor.has_max == 0) { continue; }
            // A normalized accessor's declared bounds are in RAW units while the decode produces
            // unit-range values, so comparing the two would report every normalized accessor in
            // every file. An integer accessor's bounds are exact by construction and carry no
            // information a decode error could contradict.
            if (accessor.component_type != cgltf_component_type_r_32f || accessor.normalized != 0)
            {
                continue;
            }

            const cgltf_size components = cgltf_num_components(accessor.type);
            if (components == 0 || components > 16) { continue; }

            std::vector<float> values;
            try
            {
                values = UnpackAccessor(&accessor, components, "bounds cross-check");
            }
            catch (const std::exception&)
            {
                // A decode failure is somebody else's diagnostic; this one only speaks about
                // values it actually has.
                continue;
            }

            // A generous epsilon: the bounds are authored as decimal text and the values as
            // binary floats, so an exporter's own rounding routinely puts the extreme value a few
            // ULPs outside its stated bound. What this check exists to catch is a decode that
            // produced something else entirely -- D4's all-zeros, a mis-strided read, a swapped
            // component -- not a last-digit disagreement.
            const auto tolerance = [](float bound) {
                return std::max(1e-5f, std::fabs(bound) * 1e-4f);
            };

            for (cgltf_size c = 0; c < components; ++c)
            {
                float lo = std::numeric_limits<float>::infinity();
                float hi = -std::numeric_limits<float>::infinity();
                for (cgltf_size v = 0; v < accessor.count; ++v)
                {
                    const float value = values[static_cast<std::size_t>(v * components + c)];
                    if (!std::isfinite(value)) { continue; }
                    lo = std::min(lo, value);
                    hi = std::max(hi, value);
                }
                if (!std::isfinite(lo) || !std::isfinite(hi)) { continue; }

                if (accessor.has_min != 0 && lo < accessor.min[c] - tolerance(accessor.min[c]))
                {
                    warnings.push_back(
                        "Accessor " + std::to_string(i) + " component " + std::to_string(c) +
                        " decodes to a minimum of " + std::to_string(lo) +
                        ", below its own declared min of " + std::to_string(accessor.min[c]) +
                        ". The file's bounds and the decoded data disagree (GLTF-061).");
                }
                if (accessor.has_max != 0 && hi > accessor.max[c] + tolerance(accessor.max[c]))
                {
                    warnings.push_back(
                        "Accessor " + std::to_string(i) + " component " + std::to_string(c) +
                        " decodes to a maximum of " + std::to_string(hi) +
                        ", above its own declared max of " + std::to_string(accessor.max[c]) +
                        ". The file's bounds and the decoded data disagree (GLTF-061).");
                }
            }
        }
    }

    std::filesystem::path ResolveExternalUriEXT(const std::filesystem::path& gltfDir,
                                                const std::string& uri, const char* what)
    {
        namespace fs = std::filesystem;

        const std::string subject = std::string(what) + " URI '" + uri + "'";

        // A single-letter prefix is a Windows drive, not a scheme -- and it is an absolute
        // reference on every platform, not only the one whose std::filesystem agrees. Rejecting it
        // here rather than leaving it to (2) keeps the answer the same on Linux and Windows.
        const std::size_t colon = uri.find(':');
        if (colon == 1 && std::isalpha(static_cast<unsigned char>(uri.front())) != 0)
        {
            throw std::runtime_error(
                "Refusing " + subject + ": it names a drive-absolute path, and a glTF file may "
                "only reference files inside its own directory.");
        }

        // (1) A scheme means this is not a path relative to the asset at all. Checked on the raw
        // URI, before percent-decoding, because `%68ttp:` is not a scheme and must not become one.
        if (colon != std::string::npos && colon > 1)
        {
            const std::string scheme = uri.substr(0, colon);
            const bool schemeLike = std::all_of(scheme.begin(), scheme.end(), [](unsigned char c) {
                return std::isalnum(c) != 0 || c == '+' || c == '-' || c == '.';
            });
            if (schemeLike && (std::isalpha(static_cast<unsigned char>(scheme.front())) != 0))
            {
                throw std::runtime_error(
                    "Refusing " + subject + ": CNA resolves only relative file paths and 'data:' "
                    "URIs, and '" + scheme + ":' is neither.");
            }
        }

        std::vector<char> decoded(uri.begin(), uri.end());
        decoded.push_back('\0');
        cgltf_decode_uri(decoded.data());
        // RFC 3986 uses '/' for an absolute-path reference independently of the host platform.
        // std::filesystem::path("/etc/passwd").is_absolute() is false with Windows path
        // semantics, where it is only root-relative to the current drive, so relying on the
        // filesystem below made the diagnostic platform-dependent. A leading backslash is the
        // equivalent Windows root-relative spelling and is no more valid as a glTF relative URI.
        if (decoded.front() == '/' || decoded.front() == '\\')
        {
            throw std::runtime_error(
                "Refusing " + subject + ": it is an absolute path, and a glTF file may only "
                "reference files inside its own directory.");
        }
        const fs::path relative(decoded.data());

        // (2) An absolute path ignores the asset directory by construction, so containment is not
        // even a question -- it is simply not the kind of reference glTF's relative URIs are.
        if (relative.is_absolute() || relative.has_root_name())
        {
            throw std::runtime_error(
                "Refusing " + subject + ": it is an absolute path, and a glTF file may only "
                "reference files inside its own directory.");
        }

        // Component-wise, never a string prefix: '/asset-evil' must not read as inside '/asset'.
        const auto contains = [](const fs::path& base, const fs::path& candidate) {
            auto b = base.begin();
            auto c = candidate.begin();
            for (; b != base.end(); ++b, ++c)
            {
                if (c == candidate.end() || *c != *b) { return false; }
            }
            return true;
        };

        // (3) Lexical containment. 'a/../../b' escapes even though no single component does.
        const fs::path lexicalBase = (gltfDir.empty() ? fs::path(".") : gltfDir).lexically_normal();
        const fs::path lexical = (lexicalBase / relative).lexically_normal();
        if (!contains(lexicalBase, lexical))
        {
            throw std::runtime_error(
                "Refusing " + subject + ": it resolves outside the asset's own directory.");
        }

        // (4) The same question again after resolving symlinks in whatever part of the path
        // already exists. weakly_canonical, not canonical: the target legitimately may not exist
        // yet, and "missing file" is the caller's error to report, not this one's.
        std::error_code ec;
        const fs::path realBase = fs::weakly_canonical(lexicalBase, ec);
        if (!ec)
        {
            const fs::path realPath = fs::weakly_canonical(lexical, ec);
            if (!ec && !contains(realBase, realPath))
            {
                throw std::runtime_error(
                    "Refusing " + subject + ": it resolves through a symbolic link to '" +
                    realPath.string() + "', outside the asset's own directory.");
            }
        }

        return lexical;
    }

    void ValidateExternalUriContainmentEXT(const cgltf_data* data,
                                           const std::filesystem::path& gltfDir)
    {
        if (data == nullptr) { return; }

        // An absent uri is a GLB's own BIN chunk or a bufferView-backed image -- no path involved.
        // A data: URI carries its bytes inline, so there is nothing to resolve or contain.
        const auto isExternal = [](const char* uri) {
            return uri != nullptr && std::string(uri).rfind("data:", 0) != 0 && *uri != '\0';
        };

        for (cgltf_size i = 0; i < data->buffers_count; ++i)
        {
            if (isExternal(data->buffers[i].uri))
            {
                ResolveExternalUriEXT(gltfDir, data->buffers[i].uri, "buffer");
            }
        }
        for (cgltf_size i = 0; i < data->images_count; ++i)
        {
            if (isExternal(data->images[i].uri))
            {
                ResolveExternalUriEXT(gltfDir, data->images[i].uri, "image");
            }
        }
    }

    void ValidateGltfEXT(const cgltf_data* data, const std::string& sourceName,
                         std::vector<std::string>& warnings)
    {
        if (data == nullptr) { throw std::runtime_error("'" + sourceName + "' produced no glTF data."); }

        // (1) GLTF-036 / §3.6.2.4 data alignment. Checked FIRST, before cgltf_validate, and the
        // order is not cosmetic -- see the note at the end of this block. cgltf_validate does NOT
        // check alignment itself, and the
        // omission is not cosmetic: cgltf reads a component with a raw `*(const float*)` cast, so
        // a bufferView whose effective offset is not a multiple of the component size makes the
        // parser perform a misaligned load. That is undefined behaviour by the standard and a
        // genuine fault on targets without unaligned access -- UBSan reports it as
        // `load of misaligned address ... which requires 4 byte alignment`, which is how this was
        // found: a hand-authored fixture in this repository put a sparse VEC3<float> values
        // bufferView at byteOffset 62.
        //
        // Rejection rather than a warning: CNA cannot make the vendored parser safe on such a
        // file, and a warning would leave the undefined behaviour in place while claiming the file
        // loaded. The alternative considered -- read misaligned data through memcpy and carry on --
        // would mean replacing cgltf's whole element reader, which GLTF-041 exists to prevent.
        //
        // WHY THIS RUNS BEFORE cgltf_validate (plan_gltf.md GLTF-040's second finding). It used to
        // run after, which is one call too late: `cgltf_validate` walks an index accessor's actual
        // BYTES through `cgltf_calc_index_bound` to bound its maximum index, and does so with a
        // raw `*(const uint16_t*)` cast. A file whose index bufferView is oddly offset therefore
        // performed a misaligned 16-bit load *inside the validator that was supposed to protect
        // us*, before this block ever ran. The container fuzz found it -- UBSan, cgltf.h:1566,
        // reached from cgltf_validate -- which is exactly the class REMED-NA-016 established and
        // exactly what a fuzz under sanitizers is for. Metadata-only checks (this one and the span
        // check below) read offsets and sizes, never buffer contents, so they are safe to run on
        // an unvalidated document and must.
        {
            const auto componentSize = [](cgltf_component_type type) -> cgltf_size {
                return cgltf_component_size(type);
            };
            const auto requireAligned = [&](cgltf_size offset, cgltf_size size, const char* what,
                                            cgltf_size index) {
                if (size == 0 || offset % size == 0) { return; }
                throw std::runtime_error(
                    "'" + sourceName + "' violates glTF §3.6.2.4 data alignment: the " +
                    std::string(what) + " of accessor " + std::to_string(index) +
                    " begins at byte offset " + std::to_string(offset) +
                    ", which is not a multiple of its " + std::to_string(size) +
                    "-byte component size. Reading it performs a misaligned load, which is "
                    "undefined behaviour and faults outright on targets without unaligned access, "
                    "so the file is rejected rather than imported.");
            };

            for (cgltf_size i = 0; i < data->accessors_count; ++i)
            {
                const cgltf_accessor& accessor = data->accessors[i];
                const cgltf_size size = componentSize(accessor.component_type);
                if (accessor.buffer_view != nullptr)
                {
                    requireAligned(accessor.buffer_view->offset + accessor.offset, size,
                                   "base bufferView", i);
                    // §3.6.2.1: a declared byteStride must itself be a multiple of 4, or every
                    // element after the first inherits the misalignment.
                    if (accessor.buffer_view->stride != 0 && accessor.buffer_view->stride % 4 != 0)
                    {
                        throw std::runtime_error(
                            "'" + sourceName + "' violates glTF §3.6.2.1: the bufferView backing "
                            "accessor " + std::to_string(i) + " declares byteStride " +
                            std::to_string(accessor.buffer_view->stride) +
                            ", which is not a multiple of 4.");
                    }
                }
                if (accessor.is_sparse == 0) { continue; }
                const cgltf_accessor_sparse& sparse = accessor.sparse;
                if (sparse.indices_buffer_view != nullptr)
                {
                    requireAligned(sparse.indices_buffer_view->offset + sparse.indices_byte_offset,
                                   componentSize(sparse.indices_component_type),
                                   "sparse indices array", i);
                }
                if (sparse.values_buffer_view != nullptr)
                {
                    requireAligned(sparse.values_buffer_view->offset + sparse.values_byte_offset,
                                   size, "sparse values array", i);
                }
            }
        }

        // (2) GLTF-039. The spans cgltf_validate is about to check, recomputed so that an
        // *overflow* is an error rather than a smaller number.
        //
        // cgltf computes `offset + stride * (count - 1) + elementSize` in `cgltf_size`, and
        // `offset + size` for a bufferView, both in unsigned arithmetic that wraps silently. A
        // file declaring `"count": 6148914691236517206` on a VEC3<float> accessor makes
        // `stride * (count - 1)` wrap to a small value, so the comparison against the bufferView's
        // size *passes* -- the file is admitted, and every later read walks off the end of the
        // buffer using the enormous count the file actually asked for. The wrap is what makes it
        // dangerous: an unchecked-but-honest 2^62 would simply fail the bounds check.
        //
        // `RequiredSpan` is the same arithmetic with each step guarded, and it is what the index
        // decode path already uses (§8.1); this brings every accessor in the file under it,
        // including the two sparse arrays, before anything reads a byte. Rejection, for the same
        // reason as (1): a wrapped span is not a file CNA can decode at all.
        {
            // Unquoted, because `RequiredSpan` quotes whatever it is handed; the throws below
            // quote it themselves so every message in this block reads the same way.
            const auto describe = [&sourceName](cgltf_size index, const char* what) {
                return sourceName + " accessor " + std::to_string(index) + " (" + what + ")";
            };
            for (cgltf_size i = 0; i < data->accessors_count; ++i)
            {
                const cgltf_accessor& accessor = data->accessors[i];
                const cgltf_size elementSize =
                    cgltf_calc_size(accessor.type, accessor.component_type);
                if (accessor.buffer_view != nullptr)
                {
                    const std::size_t span = RequiredSpan(
                        static_cast<std::size_t>(accessor.offset),
                        static_cast<std::size_t>(accessor.count),
                        static_cast<std::size_t>(accessor.stride != 0 ? accessor.stride
                                                                       : elementSize),
                        static_cast<std::size_t>(elementSize), describe(i, "base"));
                    if (span > static_cast<std::size_t>(accessor.buffer_view->size))
                    {
                        throw std::runtime_error(
                            "Accessor '" + describe(i, "base") + "' reads " + std::to_string(span) +
                            " bytes from a bufferView of " +
                            std::to_string(static_cast<std::size_t>(accessor.buffer_view->size)) +
                            " bytes.");
                    }
                }
                if (accessor.is_sparse == 0) { continue; }
                const cgltf_accessor_sparse& sparse = accessor.sparse;
                const std::size_t sparseCount = static_cast<std::size_t>(sparse.count);
                if (sparse.indices_buffer_view != nullptr)
                {
                    const std::size_t indexSize =
                        static_cast<std::size_t>(cgltf_component_size(sparse.indices_component_type));
                    // §3.6.2.3: both sparse arrays are tightly packed, so the element size is the
                    // stride -- never the base accessor's, which may be interleaved.
                    const std::size_t span = RequiredSpan(
                        static_cast<std::size_t>(sparse.indices_byte_offset), sparseCount,
                        indexSize, indexSize, describe(i, "sparse indices"));
                    if (span > static_cast<std::size_t>(sparse.indices_buffer_view->size))
                    {
                        throw std::runtime_error("Accessor '" + describe(i, "sparse indices") +
                                                  "' reads past its own bufferView.");
                    }
                }
                if (sparse.values_buffer_view != nullptr)
                {
                    const std::size_t span = RequiredSpan(
                        static_cast<std::size_t>(sparse.values_byte_offset), sparseCount,
                        static_cast<std::size_t>(elementSize),
                        static_cast<std::size_t>(elementSize), describe(i, "sparse values"));
                    if (span > static_cast<std::size_t>(sparse.values_buffer_view->size))
                    {
                        throw std::runtime_error("Accessor '" + describe(i, "sparse values") +
                                                  "' reads past its own bufferView.");
                    }
                }
            }
            // The same wrap one level down: a bufferView whose `byteOffset + byteLength` exceeds
            // `SIZE_MAX` wraps into a range that fits inside its buffer, so cgltf's own
            // `offset + size <= buffer->size` test passes on a view that starts past the end.
            //
            // No .gltf file can reach this today, and the corpus deliberately holds no fixture for
            // it: cgltf parses every integer through `atoll`, so a JSON value above `LLONG_MAX`
            // saturates, and two saturated values still sum to `2^64 - 2`. It is asserted directly
            // instead -- `GltfContainerRobustness.ValidationRejectsABufferViewWhoseRangeWraps`
            // hands `ValidateGltfEXT` a `cgltf_data` built in the test -- because the guard's job
            // is to make the *computation* safe rather than to trust one parser's limits, and a
            // check nothing exercises is a check that has already stopped working.
            for (cgltf_size i = 0; i < data->buffer_views_count; ++i)
            {
                const cgltf_buffer_view& view = data->buffer_views[i];
                const std::size_t offset = static_cast<std::size_t>(view.offset);
                const std::size_t size = static_cast<std::size_t>(view.size);
                if (offset > std::numeric_limits<std::size_t>::max() - size)
                {
                    throw std::runtime_error(
                        "'" + sourceName + "' bufferView " + std::to_string(i) +
                        " declares byteOffset " + std::to_string(offset) + " and byteLength " +
                        std::to_string(size) + ", whose sum overflows. The file is rejected rather "
                        "than imported: the wrapped range would pass every later bounds check.");
                }
            }
        }

        // (3) GLTF-021/GLTF-022. Every constraint cgltf_validate checks is one whose violation
        // makes decoding unsafe or meaningless -- an accessor reaching past its bufferView, a
        // bufferView past its buffer, a sparse index outside the accessor's own range, attribute
        // counts disagreeing within a primitive, an undefined component or primitive type. It
        // checks nothing outside that class, so there is no "cosmetic" violation to downgrade to a
        // warning and the severity policy is simply: failure rejects.
        const cgltf_result validation = cgltf_validate(const_cast<cgltf_data*>(data));
        if (validation != cgltf_result_success)
        {
            const char* reason = validation == cgltf_result_data_too_short
                ? "a buffer, bufferView or accessor range extends past the data backing it"
                : "a structural constraint of the glTF object model is violated";
            throw std::runtime_error(
                "'" + sourceName + "' fails glTF structural validation: " + std::string(reason) +
                " (cgltf_validate code " + std::to_string(static_cast<int>(validation)) +
                "). Decoding it could read outside the file's own buffers, so it is rejected "
                "rather than imported.");
        }

        // (4) GLTF-061. The one piece of redundancy glTF gives a reader: the author states each
        // accessor's bounds, and a decoder producing values outside them has decoded something
        // other than what was written. Nothing read them before, which is why D4 -- a sparse index
        // accessor decoding to all zeros -- collapsed a quad to a point with every layer reporting
        // success. A warning rather than a rejection: stale bounds are common and harmless, while
        // the values themselves may still be exactly what the file contains.
        CrossCheckAccessorBoundsEXT(data, warnings);

        // (5) GLTF-351. EXT_meshopt_compression, refused here rather than left to produce
        // whatever bytes happen to lie at the view's offset.
        //
        // cgltf *parses* the extension -- it validates the compression metadata thoroughly, which
        // is what makes this so easy to mistake for support -- but decoding needs
        // `meshopt_decodeVertexBuffer` and friends, supplied by the caller, which CNA does not
        // provide. Without a decoder `cgltf_buffer_view_data` falls through to
        // `buffer->data + view->offset`, so every accessor over such a view reads the wrong bytes:
        // not an error, not empty, just undefined geometry that renders as a mangled or invisible
        // mesh. That is the single failure mode this row's acceptance names -- "never silently
        // empty geometry" -- and the only way to honour it without a decoder is to refuse.
        //
        // Refused even when the extension is merely *used* rather than *required*, which is the
        // one place the GLTF-024 severity rule does not apply: an ignorable extension is one whose
        // absence leaves the file readable, and this one rewrites where the geometry lives.
        for (cgltf_size i = 0; i < data->buffer_views_count; ++i)
        {
            if (data->buffer_views[i].has_meshopt_compression == 0) { continue; }
            throw std::runtime_error(
                "'" + sourceName + "' uses EXT_meshopt_compression (bufferView " +
                std::to_string(i) + "). CNA has no meshopt decoder, and reading such a view "
                "without one yields undefined bytes rather than an error -- geometry that renders "
                "mangled or invisible with nothing to indicate why. The file is refused instead "
                "(GLTF-351).");
        }

        // (6) GLTF-023. The author declared the file cannot be interpreted correctly without these.
        for (cgltf_size i = 0; i < data->extensions_required_count; ++i)
        {
            const char* name = data->extensions_required[i];
            if (name == nullptr) { continue; }
            if (!IsGltfExtensionSupportedEXT(name))
            {
                throw std::runtime_error(
                    "'" + sourceName + "' lists '" + name + "' in extensionsRequired, which CNA "
                    "does not implement. The file declares it cannot be interpreted correctly "
                    "without that extension, so importing it would produce geometry or shading "
                    "its author already said would be wrong.");
            }
        }

        // (3) GLTF-024. extensionsUsed is by definition optional, so an unimplemented entry is
        // reported and the import continues -- but it is never silent, because "loaded fine" and
        // "loaded as authored" are different claims.
        for (cgltf_size i = 0; i < data->extensions_used_count; ++i)
        {
            const char* name = data->extensions_used[i];
            if (name == nullptr) { continue; }
            if (!IsGltfExtensionSupportedEXT(name))
            {
                const GltfExtensionRecordEXT* record = FindGltfExtensionEXT(name);
                const std::string reason = record != nullptr
                    ? record->note
                    : "The extension is not present in CNA's support registry.";
                warnings.push_back(
                    "'" + sourceName + "' uses extension '" + std::string(name) + "', which CNA "
                    "does not fully implement. " + reason);
            }
        }
    }

    std::vector<LightOut> ExtractPunctualLightsEXT(const cgltf_data* data)
    {
        LightReportEXT ignored;
        return ExtractPunctualLightsEXT(data, ignored);
    }

    std::vector<LightOut> ExtractPunctualLightsEXT(const cgltf_data* data, LightReportEXT& report)
    {
        report = LightReportEXT{};
        std::vector<LightOut> result;
        if (data->lights_count == 0) { return result; }

        const std::unordered_set<const cgltf_node*> reachable = CollectSceneReachableNodes(data);

        // plan_gltf.md GLTF-326. XNA's stock effects light with three directional lights and
        // nothing else, so importing a glTF light rig is lossy by construction -- and until this
        // report existed, invisibly so: a scene lit by six point lights imported as three
        // directionals aimed at the origin and said nothing. The loop below is unchanged; every
        // addition to it records a place the loss happens.
        //
        // Note the loop no longer stops at three. It must keep walking to count what it is
        // dropping, which is the whole point -- "3 of 6 lights imported" is actionable and
        // "3 lights imported" is not.
        for (cgltf_size i = 0; i < data->nodes_count; ++i)
        {
            const cgltf_node& node = data->nodes[i];
            if (!node.light) { continue; }
            if (!reachable.empty() && reachable.find(&node) == reachable.end()) { continue; }
            if (result.size() >= 3)
            {
                ++report.droppedLightCount;
                continue;
            }

            float worldMat[16];
            cgltf_node_transform_world(&node, worldMat);
            // Column-major 4x4: translation is the 4th column; a light's own local -Z axis
            // (glTF's own convention for the direction it travels) is the 3rd column, negated.
            const Vector3 worldPos(worldMat[12], worldMat[13], worldMat[14]);
            const Vector3 forward(-worldMat[8], -worldMat[9], -worldMat[10]);

            const cgltf_light& src = *node.light;
            Vector3 direction;
            switch (src.type)
            {
            case cgltf_light_type_directional:
                direction = (forward.LengthSquared() > 1e-12f) ? Vector3::Normalize(forward) : Vector3(0.0f, -1.0f, 0.0f);
                break;
            case cgltf_light_type_point:
            case cgltf_light_type_spot:
            default:
                // No point/spot light support in any CNA stock effect shader -- approximated as a
                // directional light pointing from the light's own world position toward the scene
                // origin (see ExtractPunctualLightsEXT's own doc comment for the full rationale).
                // A spot additionally loses its cone entirely, which is why the two are counted
                // apart: the approximation is materially worse for one than the other.
                if (src.type == cgltf_light_type_spot) { ++report.approximatedSpotLightCount; }
                else { ++report.approximatedPointLightCount; }
                // GLTF-327: range and cone angles, counted where they are lost. Both bound the
                // light's REACH, and a directional light has no bounds at all, so an unreported
                // loss here lights the whole scene with a lamp the author scoped to one room --
                // an error that grows with distance from the light, which is where it is least
                // likely to be noticed while authoring.
                if (src.range > 0.0f) { ++report.ignoredRangeCount; }
                if (src.type == cgltf_light_type_spot) { ++report.ignoredConeAngleCount; }
                direction = (worldPos.LengthSquared() > 1e-12f)
                    ? Vector3::Normalize(Vector3(-worldPos.X, -worldPos.Y, -worldPos.Z))
                    : Vector3(0.0f, -1.0f, 0.0f);
                break;
            }

            const float intensity = std::max(src.intensity, 0.0f);
            // glTF intensity is photometric and unbounded -- lux for a directional light, candela
            // for the other two -- while DiffuseColor is a [0,1] colour. An authored intensity of
            // 683 clamps to white, which is not a bug but is exactly what an author comparing
            // renders needs to be told rather than left to deduce.
            const float pre[3] = {src.color[0] * intensity, src.color[1] * intensity,
                                  src.color[2] * intensity};
            if (pre[0] > 1.0f || pre[1] > 1.0f || pre[2] > 1.0f)
            {
                ++report.clampedIntensityLightCount;
                report.worstPreClampChannelEXT = std::max(
                    report.worstPreClampChannelEXT, std::max(pre[0], std::max(pre[1], pre[2])));
            }
            LightOut light;
            light.direction = direction;
            light.diffuseColor = Vector3(std::clamp(pre[0], 0.0f, 1.0f),
                                         std::clamp(pre[1], 0.0f, 1.0f),
                                         std::clamp(pre[2], 0.0f, 1.0f));
            result.push_back(light);
        }

        return result;
    }

    std::vector<float> GetMeshDefaultWeights(const cgltf_mesh* mesh, std::size_t targetCount,
                                              const cgltf_node* instancingNode)
    {
        std::vector<float> weights(targetCount, 0.0f);
        if (mesh == nullptr) { return weights; }

        // plan_gltf.md GLTF-281. §3.7.2.2: `node.weights` OVERRIDES `mesh.weights` -- it does not
        // merge with it and does not fill in only the entries it names. That distinction matters:
        // a node declaring [1,0] for a mesh whose own weights are [0,1] must produce [1,0], not
        // [1,1]. So the node's array is used INSTEAD of the mesh's when present, and a node array
        // shorter than the target count leaves the remainder at zero rather than at the mesh's
        // value. `node.weights` was read by nobody before this, so an instanced mesh posed for one
        // node's expression got every node's.
        const cgltf_float* source = mesh->weights;
        std::size_t available = static_cast<std::size_t>(mesh->weights_count);
        if (instancingNode != nullptr && instancingNode->weights_count > 0)
        {
            source = instancingNode->weights;
            available = static_cast<std::size_t>(instancingNode->weights_count);
        }
        if (source == nullptr) { return weights; }

        const std::size_t n = std::min(available, targetCount);
        for (std::size_t i = 0; i < n; ++i) { weights[i] = source[i]; }
        return weights;
    }

    std::optional<MorphWeightTrackOut> ExtractMorphWeightTrack(const cgltf_data* data, const cgltf_mesh* mesh,
                                                                std::size_t targetCount)
    {
        if (targetCount == 0) { return std::nullopt; }

        const cgltf_node* meshNode = nullptr;
        for (cgltf_size i = 0; i < data->nodes_count; ++i)
        {
            if (data->nodes[i].mesh == mesh) { meshNode = &data->nodes[i]; break; }
        }
        if (!meshNode) { return std::nullopt; }

        for (cgltf_size a = 0; a < data->animations_count; ++a)
        {
            const cgltf_animation& anim = data->animations[a];
            for (cgltf_size c = 0; c < anim.channels_count; ++c)
            {
                const cgltf_animation_channel& ch = anim.channels[c];
                if (ch.target_node != meshNode || ch.target_path != cgltf_animation_path_type_weights)
                {
                    continue;
                }

                const std::vector<float> times = UnpackAccessor(ch.sampler->input, 1, "weights channel time");
                const bool cubicSpline = ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
                // CUBICSPLINE: [in-tangent, value, out-tangent] triplets per keyframe -- all three
                // thirds are read below and carried into MorphWeightKeyframeOut, so
                // EvaluateMorphWeightsEXT can evaluate the real Hermite curve at playback time
                // (see MorphWeightTrackOut's own doc comment).
                const std::size_t tripletStride = cubicSpline ? 3 : 1;

                // The output accessor is SCALAR-typed per component (glTF's own "weights"
                // channel convention -- targetCount is external context, not encoded in the
                // accessor's own declared type), so this reads it as one flat array via
                // cgltf_accessor_unpack_floats directly rather than through UnpackAccessor's
                // per-element component-count validation (which assumes componentsPerValue
                // divides evenly via the accessor's own type -- not true here).
                std::vector<float> flat(static_cast<std::size_t>(ch.sampler->output->count));
                const cgltf_size unpacked =
                    cgltf_accessor_unpack_floats(ch.sampler->output, flat.data(), flat.size());
                // Reading the accessor flat does not exempt it from GLTF-062: the sparse override
                // rule is a property of the accessor's storage, not of how the caller groups the
                // components afterwards. One SCALAR component per element here, by construction.
                ApplySparseOverridesTightly(ch.sampler->output, flat.data(), 1,
                                            "morph weight animation output");
                if (unpacked != flat.size() ||
                    flat.size() != times.size() * targetCount * tripletStride)
                {
                    throw std::runtime_error(
                        "Failed to unpack morph weight animation channel output (malformed data, "
                        "or its size does not match keyframe count * target count).");
                }

                MorphWeightTrackOut track;
                track.stepInterpolation = ch.sampler->interpolation == cgltf_interpolation_type_step;
                track.cubicSpline = cubicSpline;
                track.keys.reserve(times.size());
                for (std::size_t k = 0; k < times.size(); ++k)
                {
                    MorphWeightKeyframeOut key;
                    key.time = times[k];
                    key.weights.resize(targetCount);
                    const std::size_t valueBase = (k * tripletStride + (cubicSpline ? 1 : 0)) * targetCount;
                    for (std::size_t t = 0; t < targetCount; ++t) { key.weights[t] = flat[valueBase + t]; }

                    if (cubicSpline)
                    {
                        key.inTangent.resize(targetCount);
                        key.outTangent.resize(targetCount);
                        const std::size_t inBase = (k * tripletStride + 0) * targetCount;
                        const std::size_t outBase = (k * tripletStride + 2) * targetCount;
                        for (std::size_t t = 0; t < targetCount; ++t)
                        {
                            key.inTangent[t] = flat[inBase + t];
                            key.outTangent[t] = flat[outBase + t];
                        }
                    }

                    track.keys.push_back(std::move(key));
                }
                return track;
            }
        }
        return std::nullopt;
    }
}
