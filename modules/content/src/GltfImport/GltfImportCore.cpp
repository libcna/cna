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
// so every stbi_*/stbiw_* symbol has internal linkage in this one translation unit -- other parts
// of the CNA tree (SDL_image's own vendored copy) also compile stb_image.h's implementation, and
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

// plan_cnj.md CNB-91 (Phase 14F): KHR_draco_mesh_compression decoding. Optional -- see
// CNA_DRACO_AVAILABLE's own doc comment in cmake/CnaLibrary.cmake for why this is a real system
// dependency rather than a vendored single-header library like cgltf.h/stb_image.h.
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
            std::vector<float> out(static_cast<std::size_t>(accessor->count) *
                                    static_cast<std::size_t>(expectedComponents));
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
            std::vector<std::uint32_t> out(count, 0u);

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
        };

        SampledChannel LoadChannel(const cgltf_animation_channel& ch, cgltf_size componentsPerValue,
                                    const char* context)
        {
            SampledChannel result;
            result.componentsPerValue = static_cast<int>(componentsPerValue);
            result.cubicSpline = ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
            result.stepInterpolation = ch.sampler->interpolation == cgltf_interpolation_type_step;

            const std::vector<float> times = UnpackAccessor(ch.sampler->input, 1, context);
            result.times.assign(times.begin(), times.end());
            result.values = UnpackAccessor(ch.sampler->output, componentsPerValue, context);
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
        // This is NOT a bit-for-bit port of Morten Mikkelsen's own reference `mikktspace.c`
        // implementation (not available to vendor in this environment, unlike cgltf.h/stb_image.h,
        // for which genuine unmodified upstream copies were found locally) -- in particular, real
        // MikkTSpace additionally welds corners sharing the same position+normal (but not
        // necessarily the same UV, e.g. across a hard-seam boundary) into shared "TSpace" groups
        // before angle-weighted accumulation, which this simpler per-glTF-vertex accumulation does
        // not replicate. A documented, deliberate scope cut, not an oversight.
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
        std::unordered_map<int, BoneChannels> GatherChannels(
            const cgltf_animation& anim, float unitScale,
            const std::function<int(const cgltf_node*)>& resolve, double& maxTime,
            bool& sawUnsupportedPath, std::size_t& skippedTargets)
        {
            std::unordered_map<int, BoneChannels> byBone;
            for (cgltf_size c = 0; c < anim.channels_count; ++c)
            {
                const cgltf_animation_channel& ch = anim.channels[c];
                const int boneIdx = resolve(ch.target_node);
                if (boneIdx < 0) { ++skippedTargets; continue; }

                if (ch.target_path == cgltf_animation_path_type_translation)
                {
                    byBone[boneIdx].translation = LoadChannel(ch, 3, "translation channel");
                    // Translation values (and, for CUBICSPLINE, their in/out tangents -- both are
                    // position-derived quantities) must track the same unit-scale correction
                    // already applied to bind-pose translations, or an animated bone would jump
                    // back to unscaled-space offsets mid-clip.
                    for (float& component : byBone[boneIdx].translation->values) { component *= unitScale; }
                }
                else if (ch.target_path == cgltf_animation_path_type_rotation)
                {
                    byBone[boneIdx].rotation = LoadChannel(ch, 4, "rotation channel");
                }
                else if (ch.target_path == cgltf_animation_path_type_scale)
                {
                    byBone[boneIdx].scale = LoadChannel(ch, 3, "scale channel");
                }
                else { sawUnsupportedPath = true; continue; } // e.g. morph target weights

                if (ch.sampler->input->count > 0)
                {
                    const std::vector<float> t = UnpackAccessor(ch.sampler->input, 1, "sampler input");
                    maxTime = std::max(maxTime, static_cast<double>(t.back()));
                }
            }
            return byBone;
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
                                                std::vector<std::string>& warnings)
    {
        // GLTF-293: rigid (non-joint) node animation. Before the real ModelBone hierarchy existed
        // there was nothing for such a channel to drive, which is why D6 waited on GLTF-103/113/114
        // rather than on the animation layer.
        std::vector<ClipOut> clips;
        if (data == nullptr) { return clips; }

        for (cgltf_size a = 0; a < data->animations_count; ++a)
        {
            const cgltf_animation& anim = data->animations[a];
            const std::string clipName = anim.name ? anim.name : ("Clip" + std::to_string(a));

            double maxTime = 0.0;
            bool sawUnsupportedPath = false;
            std::size_t skippedTargets = 0;
            const std::unordered_map<int, BoneChannels> byBone = GatherChannels(
                anim, unitScale,
                [&scene](const cgltf_node* node) {
                    const auto it = scene.indexOfNode.find(node);
                    return it == scene.indexOfNode.end() ? -1 : it->second;
                },
                maxTime, sawUnsupportedPath, skippedTargets);

            if (sawUnsupportedPath)
            {
                warnings.push_back(
                    "Clip '" + clipName + "' targets a channel path this tool does not import "
                    "(e.g. morph target weights) -- skipped.");
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
            for (const auto& [boneIdx, channels] : byBone)
            {
                const Matrix& bindPose =
                    scene.nodes[static_cast<std::size_t>(boneIdx)].localTransform;
                if (std::optional<TrackOut> track = BuildTrack(boneIdx, channels, bindPose))
                {
                    clip.tracks.push_back(std::move(*track));
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
                                       float unitScale, std::vector<std::string>& warnings)
    {
        std::vector<ClipOut> clips;

        for (cgltf_size a = 0; a < data->animations_count; ++a)
        {
            const cgltf_animation& anim = data->animations[a];

            std::unordered_map<int, BoneChannels> byBone;
            double maxTime = 0.0;
            bool sawUnsupportedTarget = false;

            for (cgltf_size c = 0; c < anim.channels_count; ++c)
            {
                const cgltf_animation_channel& ch = anim.channels[c];
                auto it = skel.nodeToNewIndex.find(ch.target_node);
                if (it == skel.nodeToNewIndex.end()) { continue; } // targets a non-joint node -- skip
                const int boneIdx = it->second;

                if (ch.target_path == cgltf_animation_path_type_translation)
                {
                    byBone[boneIdx].translation = LoadChannel(ch, 3, "translation channel");
                    // Translation values (and, for CUBICSPLINE, their in/out tangents -- both are
                    // position-derived quantities) must track the same unit-scale correction
                    // already applied to the skeleton's own bind-pose translations, or an
                    // animated bone would jump back to unscaled-space offsets mid-clip.
                    for (float& component : byBone[boneIdx].translation->values) { component *= unitScale; }
                }
                else if (ch.target_path == cgltf_animation_path_type_rotation)
                {
                    byBone[boneIdx].rotation = LoadChannel(ch, 4, "rotation channel");
                }
                else if (ch.target_path == cgltf_animation_path_type_scale)
                {
                    byBone[boneIdx].scale = LoadChannel(ch, 3, "scale channel");
                }
                else { sawUnsupportedTarget = true; continue; } // e.g. morph target weights

                if (ch.sampler->input->count > 0)
                {
                    const std::vector<float> t = UnpackAccessor(ch.sampler->input, 1, "sampler input");
                    maxTime = std::max(maxTime, static_cast<double>(t.back()));
                }
            }

            if (sawUnsupportedTarget)
            {
                warnings.push_back(
                    "Clip '" + std::string(anim.name ? anim.name : "") +
                    "' targets a channel path this tool does not import (e.g. morph target "
                    "weights) -- skipped.");
            }

            ClipOut clip;
            clip.name = anim.name ? anim.name : ("Clip" + std::to_string(a));
            clip.duration = maxTime;

            for (const auto& [boneIdx, channels] : byBone)
            {
                // Same resampling as the scene-node path -- shared so the two cannot diverge.
                if (std::optional<TrackOut> track = BuildTrack(
                        boneIdx, channels, skel.bones[static_cast<std::size_t>(boneIdx)].bindPoseLocal))
                {
                    clip.tracks.push_back(std::move(*track));
                }
            }

            clips.push_back(std::move(clip));
        }

        return clips;
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
            result.extension = ext.empty() ? "png" : ext;
            return result;
        }

        if (image->uri)
        {
            const std::string uri = image->uri;

            if (uri.rfind("data:", 0) == 0)
            {
                const auto comma = uri.find(',');
                if (comma == std::string::npos) { throw std::runtime_error("Malformed data: URI for image."); }
                if (ext.empty()) { ext = (uri.find("image/jpeg") != std::string::npos) ? "jpg" : "png"; }

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
                result.extension = ext;
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
                ext = realExt.empty() ? "png" : realExt;
            }
            result.extension = ext;
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
        if (!prim.material || !prim.material->has_pbr_metallic_roughness) { return nullptr; }
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

    // Recovers a Draco-compressed primitive's own attribute unique ID for the given glTF semantic
    // (type + set index), from cgltf's own already-fixed-up cgltf_attribute::data pointer.
    // KHR_draco_mesh_compression's "attributes" object maps each semantic name to a small integer
    // that is the *Draco stream's own unique attribute ID* -- NOT an accessor index -- but cgltf
    // parses it through the exact same generic attribute-list code as regular (accessor-backed)
    // primitive attributes, storing the raw integer as a pointer-index placeholder that its own
    // fixup pass later resolves into `&data->accessors[N]`. Recovering the original integer N is
    // then just pointer arithmetic against that same array's base -- the standard, well-known
    // convention every cgltf + Draco integration uses (cgltf itself has no Draco decoding of its
    // own, so this reinterpretation is left entirely to the consumer).
    int FindDracoUniqueId(const cgltf_primitive& prim, const cgltf_data* data,
                           cgltf_attribute_type type, int index)
    {
        for (cgltf_size k = 0; k < prim.draco_mesh_compression.attributes_count; ++k)
        {
            const cgltf_attribute& a = prim.draco_mesh_compression.attributes[k];
            if (a.type == type && a.index == index && a.data)
            {
                return static_cast<int>(a.data - data->accessors);
            }
        }
        return -1;
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
    /// triangle-list equivalent to be converted into (GLTF-072), and whether Draco's triangle-only
    /// encoder could have produced it (GLTF-080). Two rules on one partition, which is why this is
    /// declared in the header rather than kept local.
    bool ProducesTriangles(PrimitiveTopology topology)
    {
        return topology == PrimitiveTopology::Triangles
            || topology == PrimitiveTopology::TriangleStrip
            || topology == PrimitiveTopology::TriangleFan;
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

        // plan_gltf.md GLTF-080: Draco's mesh encoder is a TRIANGLE encoder -- a decoded
        // draco::Mesh has a face list and nothing else -- so a Draco primitive declaring a line or
        // point mode is a contradiction the file cannot mean. Refused rather than silently drawn
        // as triangles, and checked BEFORE decoding so the diagnostic names the contradiction
        // instead of some later symptom of it. Independent of whether this build has libdraco: the
        // file is self-contradictory either way.
        if (prim.has_draco_mesh_compression && !ProducesTriangles(sourceTopology))
        {
            throw std::runtime_error(
                "Primitive '" + name + "' declares mode " +
                std::string(PrimitiveTopologyName(sourceTopology)) +
                " together with KHR_draco_mesh_compression, which encodes triangles only. The two "
                "cannot both be true, so the primitive is refused rather than drawn as something "
                "the file did not ask for (plan_gltf.md GLTF-080).");
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

        // Use whichever TEXCOORD set the base-color texture actually references (glTF allows a
        // texture reference to select TEXCOORD_1/2/... via its own "texcoord" index, defaulting
        // to 0) -- a hardcoded TEXCOORD_0 would silently mismatch a texture authored against a
        // different UV set. CNB-97 (Phase 14H): KHR_texture_transform's own "texcoord" (when
        // present) overrides the base-color texture view's own material-level one, per spec.
        int texcoordIndex = 0;
        const cgltf_texture_transform* baseColorTransform = nullptr;
        if (prim.material && prim.material->has_pbr_metallic_roughness &&
            prim.material->pbr_metallic_roughness.base_color_texture.texture)
        {
            const cgltf_texture_view& baseColorView = prim.material->pbr_metallic_roughness.base_color_texture;
            texcoordIndex = baseColorView.texcoord;
            if (baseColorView.has_transform)
            {
                baseColorTransform = &baseColorView.transform;
                if (baseColorTransform->has_texcoord) { texcoordIndex = baseColorTransform->texcoord; }
            }
        }
        const cgltf_accessor* uvAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_texcoord, texcoordIndex);

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
        out.baseColorImage = FindBaseColorImage(prim, &out.unsupportedTextureSourcesEXT);
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
        out.normalImage = FindNormalImage(prim, &out.unsupportedTextureSourcesEXT);
        out.metallicRoughnessImage = FindMetallicRoughnessImage(prim, &out.unsupportedTextureSourcesEXT);
        out.emissiveImage = FindEmissiveImage(prim, &out.unsupportedTextureSourcesEXT);
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
        const bool metallicRoughnessMaterial =
            (prim.material == nullptr) ||
            (!prim.material->has_pbr_specular_glossiness && !prim.material->unlit);
        out.usePbr = (!out.colored) && metallicRoughnessMaterial;
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
        // factor-only material left them at MeshOut's defaults -- the second half of D7, and the
        // reason f8 lost its metallic/roughness/emissive as well as its base colour.
        if (prim.material && prim.material->has_pbr_metallic_roughness)
        {
            out.metallicFactor  = prim.material->pbr_metallic_roughness.metallic_factor;
            out.roughnessFactor = prim.material->pbr_metallic_roughness.roughness_factor;
            // GLTF-216: baseColorFactor multiplies the base-colour texture (or stands alone when
            // there is none). Never read anywhere before this.
            const cgltf_float* base = prim.material->pbr_metallic_roughness.base_color_factor;
            out.baseColorFactor = Vector4(base[0], base[1], base[2], base[3]);
        }
        if (prim.material)
        {
            // plan_gltf.md GLTF-228/GLTF-229/GLTF-231: the alpha and sidedness state, read for any
            // material rather than only a PBR-selected one. These are the last three fields of the
            // factor-only material D7 recorded as entirely lost.
            using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
            switch (prim.material->alpha_mode)
            {
                case cgltf_alpha_mode_mask:  out.alphaMode = AlphaModeEXT::Mask;  break;
                case cgltf_alpha_mode_blend: out.alphaMode = AlphaModeEXT::Blend; break;
                case cgltf_alpha_mode_opaque:
                default:                     out.alphaMode = AlphaModeEXT::Opaque; break;
            }
            out.alphaCutoff = prim.material->alpha_cutoff;
            out.doubleSided = prim.material->double_sided != 0;

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
                    out.alphaMode = AlphaModeEXT::Blend;
                    // Multiplied into whatever alpha the material already asked for, rather than
                    // replacing it: a material that is both partly transparent and transmissive
                    // should end up more transparent than either alone, not lose one of them.
                    out.baseColorFactor.W *= (1.0f - out.transmissionFactorEXT);
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
            out.normalScale = viewScalar(prim.material->normal_texture);
            out.occlusionStrength = viewScalar(prim.material->occlusion_texture);

            // plan_gltf.md GLTF-202: one sampler per texture slot, read from the texture each view
            // names. A slot with no texture keeps glTF's default with `declared` false.
            const auto slot = [](TextureSlotEXT s) { return static_cast<std::size_t>(s); };
            if (prim.material->has_pbr_metallic_roughness)
            {
                out.samplers[slot(TextureSlotEXT::BaseColor)] =
                    SamplerForTextureView(prim.material->pbr_metallic_roughness.base_color_texture);
                out.samplers[slot(TextureSlotEXT::MetallicRoughness)] =
                    SamplerForTextureView(
                        prim.material->pbr_metallic_roughness.metallic_roughness_texture);
            }
            out.samplers[slot(TextureSlotEXT::Normal)] =
                SamplerForTextureView(prim.material->normal_texture);
            out.samplers[slot(TextureSlotEXT::Emissive)] =
                SamplerForTextureView(prim.material->emissive_texture);
            out.samplers[slot(TextureSlotEXT::Occlusion)] =
                SamplerForTextureView(prim.material->occlusion_texture);

            // CNB-97 (Phase 14H): KHR_materials_emissive_strength extends EmissiveFactor's own
            // [0,1] range with a multiplier (real HDR-authored content routinely uses > 1), before
            // the emissive texture (if any) is applied -- glTF's own spec order.
            const float emissiveStrength = prim.material->has_emissive_strength
                ? prim.material->emissive_strength.emissive_strength : 1.0f;
            out.emissiveFactor = Vector3(prim.material->emissive_factor[0],
                                          prim.material->emissive_factor[1],
                                          prim.material->emissive_factor[2]) * emissiveStrength;
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
        out.occlusionImage =
            (!out.colored) ? FindOcclusionImage(prim, &out.unsupportedTextureSourcesEXT) : nullptr;
        out.useDualTexture = (!out.usePbr) && (!out.skinned) &&
                              (out.occlusionImage != nullptr) && (out.baseColorImage != nullptr);

        // Each of a glTF material's texture references (baseColorTexture, normalTexture,
        // metallicRoughnessTexture, emissiveTexture, occlusionTexture) can independently select
        // its own TEXCOORD set via its own "texcoord" index -- but PbrEffect/SkinnedPbrEffect only
        // sample from ONE shared UV channel (the one baked into TextureCoordinate, itself always
        // taken from the base-color texture's own texcoord, `texcoordIndex` above). Detect (but do
        // not attempt to fix -- see MeshOut::pbrUv2Mismatch's own doc comment) any present map
        // that disagrees, so the caller can at least warn instead of silently mis-rendering it.
        if (out.usePbr && prim.material)
        {
            const auto usesDifferentTexcoord = [&](const cgltf_texture_view& view)
            {
                return view.texture != nullptr && view.texcoord != texcoordIndex;
            };
            out.pbrUv2Mismatch =
                usesDifferentTexcoord(prim.material->normal_texture) ||
                usesDifferentTexcoord(prim.material->emissive_texture) ||
                usesDifferentTexcoord(prim.material->occlusion_texture) ||
                (prim.material->has_pbr_metallic_roughness &&
                 usesDifferentTexcoord(prim.material->pbr_metallic_roughness.metallic_roughness_texture));
        }
        // Unskinned colored meshes reuse the real XNA VertexPositionColorTexture layout (stride
        // 24, Position+Color+TextureCoordinate, no Normal) -- already fully supported end-to-end
        // by ModelTypeReader and every graphics renderer's existing VertexColorEnabled shader path.
        // Skinned colored meshes use the stride-56 layout instead (Position+Normal+
        // TextureCoordinate+BlendWeight+BlendIndices+Color). Skinned + PBR meshes use the new
        // stride-68 layout (Position+Normal+Tangent+TextureCoordinate+BlendWeight+BlendIndices).
        out.stride = out.skinned ? (out.colored ? 56 : (out.usePbr ? 68 : 52))
                                 : (out.colored ? 24 : (out.usePbr ? 48 : (out.useDualTexture ? 20 : 32)));

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

        out.vertexBytes.reserve(static_cast<std::size_t>(vertexCount) * static_cast<std::size_t>(out.stride));

#ifdef CNA_DRACO_AVAILABLE
        if (dracoMesh && static_cast<cgltf_size>(dracoMesh->num_points()) != vertexCount)
        {
            throw std::runtime_error(
                "Primitive '" + name + "' has a Draco-decoded point count that does not match its "
                "declared POSITION accessor count (malformed file).");
        }
        // Unified per-semantic unpacking: reads from the decoded Draco mesh (via its own unique
        // attribute ID) when this is a Draco-compressed primitive, or from the regular accessor
        // otherwise -- every call site below is agnostic to which source actually backs it.
        const auto unpackSemantic = [&](cgltf_attribute_type type, int setIndex, const cgltf_accessor* acc,
                                         int numComponents, const char* context) -> std::vector<float>
        {
            if (dracoMesh)
            {
                const int uniqueId = FindDracoUniqueId(prim, data, type, setIndex);
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
            indices.reserve(static_cast<std::size_t>(vertexCount));
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
            indices = ConvertToTriangleList(indices, sourceTopology);
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
        std::vector<float> uvs = uvAcc
            ? unpackSemantic(cgltf_attribute_type_texcoord, texcoordIndex, uvAcc, 2, "TEXCOORD") : std::vector<float>();
        // plan_gltf.md GLTF-184/GLTF-336. Exactly ONE transform can be baked, because there is
        // exactly one UV channel to bake it into -- so every map whose transform differs from the
        // base colour's is sampled with the wrong texture coordinates, and said nothing about it.
        //
        // Reported rather than fixed, and the reason is worth separating from GLTF-181's. That one
        // (a real second UV *channel*) needs a vertex attribute, which means a new stride, and the
        // stride it needs is already taken. This one needs only a per-map uniform in the PBR
        // programs: the shared UV is transformed in the shader before each sample. That is a
        // strictly smaller change -- no ABI, no VertexDeclaration -- but it is still a shader and
        // uniform change in every renderer that has a PBR program, which is not something this
        // task can land and verify. Naming the maps is what makes it a known limit rather than a
        // mystery in someone's render comparison.
        {
            const auto sameTransform = [](const cgltf_texture_transform& a,
                                          const cgltf_texture_transform* b) {
                if (b == nullptr) { return false; }
                return a.offset[0] == b->offset[0] && a.offset[1] == b->offset[1] &&
                       a.scale[0] == b->scale[0] && a.scale[1] == b->scale[1] &&
                       a.rotation == b->rotation;
            };
            const auto checkView = [&](const cgltf_texture_view& view, const char* mapName) {
                if (view.texture == nullptr || view.has_transform == 0) { return; }
                if (sameTransform(view.transform, baseColorTransform)) { return; }
                out.unbakedTextureTransformsEXT.emplace_back(mapName);
            };
            if (prim.material != nullptr)
            {
                if (prim.material->has_pbr_metallic_roughness)
                {
                    checkView(prim.material->pbr_metallic_roughness.metallic_roughness_texture,
                              "metallic-roughness");
                }
                checkView(prim.material->normal_texture, "normal");
                checkView(prim.material->occlusion_texture, "occlusion");
                checkView(prim.material->emissive_texture, "emissive");
            }
        }

        // CNB-97 (Phase 14H): KHR_texture_transform, applied to the shared UV channel baked into
        // TextureCoordinate (matching PbrEffect's own single-shared-UV-channel limitation --
        // see MeshOut::pbrUv2Mismatch's own doc comment) -- the glTF spec's own reference formula
        // (scale, then rotate, then translate).
        if (baseColorTransform)
        {
            const float ox = baseColorTransform->offset[0], oy = baseColorTransform->offset[1];
            const float sx = baseColorTransform->scale[0],  sy = baseColorTransform->scale[1];
            const float rot = baseColorTransform->rotation;
            const float cosR = std::cos(rot), sinR = std::sin(rot);
            for (std::size_t i = 0; i + 1 < uvs.size(); i += 2)
            {
                const float u = uvs[i], v = uvs[i + 1];
                uvs[i]     = cosR * u * sx - sinR * v * sy + ox;
                uvs[i + 1] = sinR * u * sx + cosR * v * sy + oy;
            }
        }
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
                tangents = ComputeTangentsEXT(positions, normals, uvs, indices, vertexCount);
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
        out.morphPositionDeltas.resize(prim.targets_count);
        out.morphNormalDeltas.resize(prim.targets_count);
        out.morphTangentDeltas.resize(prim.targets_count);
        for (cgltf_size ti = 0; ti < prim.targets_count; ++ti)
        {
            const cgltf_morph_target& target = prim.targets[ti];

            const cgltf_accessor* posDeltaAcc = FindMorphTargetAttribute(target, cgltf_attribute_type_position);
            out.morphPositionDeltas[ti].resize(vertexCount);
            if (posDeltaAcc)
            {
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
        if (!scene) { return graph; } // no scenes at all -- caller falls back to "every mesh"

        // Explicit stack rather than recursion: a pathological file may nest nodes thousands deep,
        // and CollectSceneReachableNodes already established that convention for this traversal.
        // Pushing children in reverse keeps the visit order equal to the file's own child order.
        struct PendingNode { const cgltf_node* node; int parentIndex; };
        std::vector<PendingNode> stack;
        stack.reserve(data->nodes_count + 1);
        for (cgltf_size i = scene->nodes_count; i > 0; --i)
        {
            stack.push_back(PendingNode{scene->nodes[i - 1], 0});
        }

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
        switch (minFilter)
        {
            case 9728: minLinear = false; noMipStage = true;  break;  // NEAREST
            case 9729: minLinear = true;  noMipStage = true;  break;  // LINEAR
            case 9984: minLinear = false; mipLinear = false;  break;  // NEAREST_MIPMAP_NEAREST
            case 9985: minLinear = true;  mipLinear = false;  break;  // LINEAR_MIPMAP_NEAREST
            case 9986: minLinear = false; mipLinear = true;   break;  // NEAREST_MIPMAP_LINEAR
            case 9987: minLinear = true;  mipLinear = true;   break;  // LINEAR_MIPMAP_LINEAR
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

    bool IsGltfExtensionSupportedEXT(const std::string& extension)
    {
        // Only what the importer actually implements the semantics of. Detecting an extension so
        // it cannot mis-select an effect is NOT implementing it: KHR_materials_unlit and
        // KHR_materials_pbrSpecularGlossiness are both read by ExtractMesh to keep such a material
        // off the metallic-roughness path (GLTF-215), yet an unlit surface still goes through a
        // lit effect and the specular-glossiness parameters are dropped, so a file that *requires*
        // either is asking for something CNA cannot deliver.
        if (extension == "KHR_texture_transform") { return true; }
        if (extension == "KHR_materials_emissive_strength") { return true; }
        if (extension == "KHR_lights_punctual") { return true; }
#ifdef CNA_DRACO_AVAILABLE
        if (extension == "KHR_draco_mesh_compression") { return true; }
#endif
        return false;
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

        // (1) GLTF-021/GLTF-022. Every constraint cgltf_validate checks is one whose violation
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

        // (1b) GLTF-036 / §3.6.2.4 data alignment. cgltf_validate does NOT check this, and the
        // omission is not cosmetic: cgltf reads a component with a raw `*(const float*)` cast, so
        // a bufferView whose effective offset is not a multiple of the component size makes the
        // parser perform a misaligned load. That is undefined behaviour by the standard and a
        // genuine fault on targets without unaligned access -- UBSan reports it as
        // `load of misaligned address ... which requires 4 byte alignment`, which is how this was
        // found: a hand-authored fixture in this repository put a sparse VEC3<float> values
        // bufferView at byteOffset 62.
        //
        // Rejection rather than a warning, for the same reason as (1): CNA cannot make the
        // vendored parser safe on such a file, and a warning would leave the undefined behaviour
        // in place while claiming the file loaded. The alternative considered -- read misaligned
        // data through memcpy and carry on -- would mean replacing cgltf's whole element reader,
        // which GLTF-041 exists to prevent.
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

        // (1c) GLTF-061. The one piece of redundancy glTF gives a reader: the author states each
        // accessor's bounds, and a decoder producing values outside them has decoded something
        // other than what was written. Nothing read them before, which is why D4 -- a sparse index
        // accessor decoding to all zeros -- collapsed a quad to a point with every layer reporting
        // success. A warning rather than a rejection: stale bounds are common and harmless, while
        // the values themselves may still be exactly what the file contains.
        CrossCheckAccessorBoundsEXT(data, warnings);

        // (2) GLTF-023. The author declared the file cannot be interpreted correctly without these.
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
                warnings.push_back(
                    "'" + sourceName + "' uses extension '" + std::string(name) + "', which CNA "
                    "does not implement -- it is ignored, so anything it contributes is absent "
                    "from the import.");
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
