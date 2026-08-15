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
            if (amount <= 0.0f || ch->stepInterpolation || lo + 1 >= ch->times.size())
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
            if (amount <= 0.0f || ch->stepInterpolation || lo + 1 >= ch->times.size())
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
        const std::size_t n = skin->joints_count;
        if (n == 0) { return result; }

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

    std::vector<ClipOut> ExtractClips(const cgltf_data* data, const SkeletonResult& skel,
                                       float unitScale, std::vector<std::string>& warnings)
    {
        std::vector<ClipOut> clips;

        for (cgltf_size a = 0; a < data->animations_count; ++a)
        {
            const cgltf_animation& anim = data->animations[a];

            struct BoneChannels
            {
                std::optional<SampledChannel> translation, rotation, scale;
            };
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

            for (auto& [boneIdx, channels] : byBone)
            {
                std::vector<double> unionTimes;
                for (const auto* ch : {&channels.translation, &channels.rotation, &channels.scale})
                {
                    if (*ch) { unionTimes.insert(unionTimes.end(), ch->value().times.begin(), ch->value().times.end()); }
                }
                if (unionTimes.empty()) { continue; }
                std::sort(unionTimes.begin(), unionTimes.end());
                unionTimes.erase(
                    std::unique(unionTimes.begin(), unionTimes.end(),
                                [](double x, double y) { return std::fabs(x - y) < 1e-9; }),
                    unionTimes.end());

                Vector3 bindScale{1.0f, 1.0f, 1.0f};
                Quaternion bindRotation = Quaternion::Identity;
                Vector3 bindTranslation;
                (void)skel.bones[static_cast<std::size_t>(boneIdx)].bindPoseLocal.Decompose(
                    bindScale, bindRotation, bindTranslation);

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
                clip.tracks.push_back(std::move(track));
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

            std::vector<char> uriBuf(uri.begin(), uri.end());
            uriBuf.push_back('\0');
            cgltf_decode_uri(uriBuf.data());
            const std::filesystem::path imgPath = gltfDir / uriBuf.data();
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

    const cgltf_image* FindBaseColorImage(const cgltf_primitive& prim)
    {
        if (!prim.material || !prim.material->has_pbr_metallic_roughness) { return nullptr; }
        const cgltf_texture_view& view = prim.material->pbr_metallic_roughness.base_color_texture;
        if (!view.texture) { return nullptr; }
        return view.texture->image;
    }

    const cgltf_image* FindOcclusionImage(const cgltf_primitive& prim)
    {
        if (!prim.material || !prim.material->occlusion_texture.texture) { return nullptr; }
        return prim.material->occlusion_texture.texture->image;
    }

    const cgltf_image* FindNormalImage(const cgltf_primitive& prim)
    {
        if (!prim.material || !prim.material->normal_texture.texture) { return nullptr; }
        return prim.material->normal_texture.texture->image;
    }

    const cgltf_image* FindMetallicRoughnessImage(const cgltf_primitive& prim)
    {
        if (!prim.material || !prim.material->has_pbr_metallic_roughness) { return nullptr; }
        const cgltf_texture_view& view = prim.material->pbr_metallic_roughness.metallic_roughness_texture;
        if (!view.texture) { return nullptr; }
        return view.texture->image;
    }

    const cgltf_image* FindEmissiveImage(const cgltf_primitive& prim)
    {
        if (!prim.material || !prim.material->emissive_texture.texture) { return nullptr; }
        return prim.material->emissive_texture.texture->image;
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
        return topology == PrimitiveTopology::Triangles;
    }

    MeshOut ExtractMesh(const cgltf_data* data, const cgltf_primitive& prim, const std::string& name,
                         const SkeletonResult* skel, float unitScale)
    {
        // plan_gltf.md GLTF-071: read mesh.primitive.mode before anything else, and never
        // reinterpret it. Until GLTF-072 implements the per-mode conversion policy (strip/fan to a
        // triangle list, line topologies carried, points decided per renderer), a non-TRIANGLES
        // primitive is rejected here with its real mode named. That is a deliberate, visible
        // limitation; the alternative this replaces was decoding a strip's index run as a triangle
        // list -- dropping every triangle after the first and drawing a chaotic tangle -- with no
        // diagnostic anywhere.
        const PrimitiveTopology topology = ClassifyPrimitiveTopology(prim, name);
        if (!IsPrimitiveTopologySupported(topology))
        {
            throw std::runtime_error(
                "Primitive '" + name + "' uses glTF primitive mode " +
                std::to_string(PrimitiveTopologyMode(topology)) + " (" +
                PrimitiveTopologyName(topology) + "), which CNA does not import yet. Only mode " +
                std::to_string(PrimitiveTopologyMode(PrimitiveTopology::Triangles)) + " (" +
                PrimitiveTopologyName(PrimitiveTopology::Triangles) + ") is supported; the other "
                "topologies are rejected rather than silently reinterpreted as a triangle list.");
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
        // one. Only TRIANGLES reaches this line today, which is what makes the assumption safe --
        // rather than merely conventional, as it was before GLTF-071.
        out.topology = topology;
        out.skinned = (jointsAcc != nullptr) && (weightsAcc != nullptr);
        // A skinned+colored primitive uses a stride-56 layout (the stride-52 GPU-skinned layout
        // with a per-vertex Color appended at the end) and SkinnedEffect's own CNAEXT
        // VertexColorEnabled addition (real XNA's SkinnedEffect has no such property).
        out.colored = (colorAcc != nullptr);
        out.baseColorImage = FindBaseColorImage(prim);
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
        out.normalImage = FindNormalImage(prim);
        out.metallicRoughnessImage = FindMetallicRoughnessImage(prim);
        out.emissiveImage = FindEmissiveImage(prim);
        // PBR + skinning combo: a skinned primitive with a normal/metallic-roughness map is
        // imported through SkinnedPbrEffect (stride 68) instead of plain SkinnedEffect -- the
        // vertex-color combo (usePbr && colored) is still not attempted, matching PbrEffect's own
        // unskinned scope cut (no PBR shader currently reads a vertex Color stream).
        out.usePbr = (!out.colored) &&
                     (out.normalImage != nullptr || out.metallicRoughnessImage != nullptr);
        if (out.usePbr && prim.material && prim.material->has_pbr_metallic_roughness)
        {
            out.metallicFactor  = prim.material->pbr_metallic_roughness.metallic_factor;
            out.roughnessFactor = prim.material->pbr_metallic_roughness.roughness_factor;
        }
        if (out.usePbr && prim.material)
        {
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
        out.occlusionImage = (!out.colored) ? FindOcclusionImage(prim) : nullptr;
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

        const std::vector<float> positions = unpackSemantic(cgltf_attribute_type_position, 0, posAcc, 3, "POSITION");
        // Only the unskinned stride-24 (Position+Color+TextureCoordinate) and stride-20
        // (DualTextureEffect) layouts have no room for a per-vertex Normal.
        const std::vector<float> normals = (normAcc && out.stride != 24 && out.stride != 20)
            ? unpackSemantic(cgltf_attribute_type_normal, 0, normAcc, 3, "NORMAL") : std::vector<float>();
        std::vector<float> uvs = uvAcc
            ? unpackSemantic(cgltf_attribute_type_texcoord, texcoordIndex, uvAcc, 2, "TEXCOORD") : std::vector<float>();
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
        const std::vector<float> weights = out.skinned
            ? unpackSemantic(cgltf_attribute_type_weights, 0, weightsAcc, 4, "WEIGHTS_0") : std::vector<float>();
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
                    const int oldJointIdx = static_cast<int>(joints[i4 + static_cast<std::size_t>(k)] + 0.5f);
                    int newJointIdx = 0;
                    if (oldJointIdx >= 0 && static_cast<std::size_t>(oldJointIdx) < skel->oldToNew.size())
                    {
                        newJointIdx = skel->oldToNew[static_cast<std::size_t>(oldJointIdx)];
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

        // CNB-64 (Phase 13B): morph target position/normal deltas. TANGENT deltas are not
        // extracted (CNA's stock effects have no tangent-space normal mapping, matching PBR
        // normal-map's own documented scope cut).
        out.morphPositionDeltas.resize(prim.targets_count);
        out.morphNormalDeltas.resize(prim.targets_count);
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

    std::vector<LightOut> ExtractPunctualLightsEXT(const cgltf_data* data)
    {
        std::vector<LightOut> result;
        if (data->lights_count == 0) { return result; }

        const std::unordered_set<const cgltf_node*> reachable = CollectSceneReachableNodes(data);

        for (cgltf_size i = 0; i < data->nodes_count && result.size() < 3; ++i)
        {
            const cgltf_node& node = data->nodes[i];
            if (!node.light) { continue; }
            if (!reachable.empty() && reachable.find(&node) == reachable.end()) { continue; }

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
                direction = (worldPos.LengthSquared() > 1e-12f)
                    ? Vector3::Normalize(Vector3(-worldPos.X, -worldPos.Y, -worldPos.Z))
                    : Vector3(0.0f, -1.0f, 0.0f);
                break;
            }

            const float intensity = std::max(src.intensity, 0.0f);
            LightOut light;
            light.direction = direction;
            light.diffuseColor = Vector3(
                std::clamp(src.color[0] * intensity, 0.0f, 1.0f),
                std::clamp(src.color[1] * intensity, 0.0f, 1.0f),
                std::clamp(src.color[2] * intensity, 0.0f, 1.0f));
            result.push_back(light);
        }

        return result;
    }

    std::vector<float> GetMeshDefaultWeights(const cgltf_mesh* mesh, std::size_t targetCount)
    {
        std::vector<float> weights(targetCount, 0.0f);
        const std::size_t n = std::min(static_cast<std::size_t>(mesh->weights_count), targetCount);
        for (std::size_t i = 0; i < n; ++i) { weights[i] = mesh->weights[i]; }
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
