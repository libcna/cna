#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "SoftwareTextureFormat.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorMatrixEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Internal::Renderers::Software
{
    namespace
    {
        using Vector3 = Microsoft::Xna::Framework::Vector3;
        using Vector4 = Microsoft::Xna::Framework::Vector4;

        struct MultiSamplePosition
        {
            float x;
            float y;
        };

        // SOFTWARE-319: FNA3D's D3D11 path requests D3D11_STANDARD_MULTISAMPLE_PATTERN.
        // Keep its canonical 4x locations in one table so every CPU primitive route evaluates
        // coverage and depth at the same positions.
        constexpr std::array<MultiSamplePosition, 4> kStandardFourSamplePositions{{
            {3.0f / 8.0f, 1.0f / 8.0f},
            {7.0f / 8.0f, 3.0f / 8.0f},
            {1.0f / 8.0f, 5.0f / 8.0f},
            {5.0f / 8.0f, 7.0f / 8.0f},
        }};

        // ---- Phase S4 rasterizer core ----
        //
        // Works entirely in CNA's own native row-major/row-vector Matrix convention (matching
        // IGraphicsRenderer.hpp's own documented "combined = world * view * projection" order) --
        // no GPU/shader column-major conversion is needed since this renderer never talks to a
        // real GPU at all.
        //
        // Depth convention: CNA's Matrix::CreatePerspectiveFieldOfView/CreateOrthographic (like
        // real XNA/FNA/D3D) already produce a clip.Z/clip.W range of 0..1 after the perspective
        // divide (not OpenGL's -1..1), so the post-divide Z is used directly as the depth-buffer
        // value with no extra remapping.

        /// One vertex, fully transformed into screen space and ready to rasterize. `invW` and the
        /// color channels are already perspective-divided (color premultiplied by invW) so
        /// barycentric interpolation across a triangle is perspective-correct with a single divide
        /// at the end -- the standard technique.
        struct RasterVertex
        {
            float x = 0.0f, y = 0.0f;   ///< Screen-space pixel coordinates.
            float depth = 0.0f;         ///< Post-divide Z, 0..1 (D3D/XNA convention).
            float invW = 1.0f;          ///< 1 / clip.W, used to un-premultiply interpolated attributes.
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;  ///< Vertex color * invW, 0..1 range.
            float u = 0.0f, v = 0.0f;   ///< TextureCoordinate0 * invW (Phase S5).
            float u1 = 0.0f, v1 = 0.0f; ///< TextureCoordinate1 * invW (SOFTWARE-116).
            float fogKeep = 1.0f;       ///< XNA stock-effect fog keep factor * invW.
            /// Per-vertex BasicEffect specular result * invW (SOFTWARE-113).
            float sr = 0.0f, sg = 0.0f, sb = 0.0f;
            /// EnvironmentMapEffect vertex reflection direction/blend factor * invW.
            float envx = 0.0f, envy = 0.0f, envz = 1.0f, envBlend = 0.0f;
            /// World-space position/normal * invW (SOFTWARE-82/113) --
            /// same premultiply-then-divide perspective-correct interpolation treatment as color/uv.
            float wpx = 0.0f, wpy = 0.0f, wpz = 0.0f;
            float nx = 0.0f, ny = 0.0f, nz = 1.0f;
        };

        /// REMED-GFX-030: complete public depth state captured for one Software draw. Keeping all
        /// three fields together prevents the rasterizer from consulting a later live state and
        /// makes DepthRead (test on, write off) distinct from both Default and None.
        struct RasterDepthState
        {
            bool testEnabled = true;
            bool writeEnabled = true;
            int compareFunction = 3; // CompareFunction::LessEqual
        };

        /// REMED-GFX-148: one component of an XNA Blend factor vector. SourceColor and
        /// DestinationColor naturally select alpha when channel==3; SourceAlphaSaturation's alpha
        /// component is One, matching the public Blend definition and native renderer mappings.
        float BlendFactorComponent(int factor, int channel,
                                   const std::array<float, 4>& source,
                                   const std::array<float, 4>& destination,
                                   const std::array<float, 4>& constant)
        {
            switch (factor)
            {
                case 0: return 1.0f;                              // One
                case 1: return 0.0f;                              // Zero
                case 2: return source[channel];                   // SourceColor
                case 3: return 1.0f - source[channel];            // InverseSourceColor
                case 4: return source[3];                         // SourceAlpha
                case 5: return 1.0f - source[3];                  // InverseSourceAlpha
                case 6: return destination[channel];              // DestinationColor
                case 7: return 1.0f - destination[channel];       // InverseDestinationColor
                case 8: return destination[3];                    // DestinationAlpha
                case 9: return 1.0f - destination[3];             // InverseDestinationAlpha
                case 10: return constant[channel];                // BlendFactor
                case 11: return 1.0f - constant[channel];         // InverseBlendFactor
                case 12: return channel == 3                      // SourceAlphaSaturation
                    ? 1.0f
                    : std::min(source[3], 1.0f - destination[3]);
                default:
                    throw std::runtime_error(
                        "SoftwareRenderer: unsupported Blend factor ordinal");
            }
        }

        /// REMED-GFX-148: applies the independently selected colour/alpha BlendFunction after
        /// both terms have been multiplied by their own factors. Clamping deliberately remains at
        /// the caller, after the function and before the established RGBA8 conversion.
        float ApplyBlendFunction(int function, float sourceTerm, float destinationTerm)
        {
            switch (function)
            {
                case 0: return sourceTerm + destinationTerm;                 // Add
                case 1: return sourceTerm - destinationTerm;                 // Subtract
                case 2: return destinationTerm - sourceTerm;                 // ReverseSubtract
                case 3: return std::max(sourceTerm, destinationTerm);        // Max
                case 4: return std::min(sourceTerm, destinationTerm);        // Min
                default:
                    throw std::runtime_error(
                        "SoftwareRenderer: unsupported BlendFunction ordinal");
            }
        }

        float BlendComponent(int channel, int sourceFactor, int destinationFactor,
                             int function,
                             const std::array<float, 4>& source,
                             const std::array<float, 4>& destination,
                             const std::array<float, 4>& constant)
        {
            // D3D9/11 and OpenGL define MIN/MAX on the unmodified source and destination; both
            // factors are ignored for these operations. FNA3D maps the XNA functions directly to
            // those native operations, so factoring first would diverge whenever either factor is
            // not One (zero-factor cases are pinned by the shared blend matrix).
            if (function == 3)
                return std::max(source[channel], destination[channel]);
            if (function == 4)
                return std::min(source[channel], destination[channel]);
            const float sourceTerm = source[channel] *
                BlendFactorComponent(sourceFactor, channel, source, destination, constant);
            const float destinationTerm = destination[channel] *
                BlendFactorComponent(destinationFactor, channel, source, destination, constant);
            return ApplyBlendFunction(function, sourceTerm, destinationTerm);
        }

        /// One CPU stencil state snapshot. The shared framebuffer stores one unsigned 8-bit value
        /// per sample (one per pixel without MSAA). GDI uses this through SpriteBatch for 2D
        /// masking; retaining it in the shared rasterizer also keeps a target switch from losing
        /// its stencil image.
        struct RasterStencilState
        {
            bool testEnabled = false;
            int compareFunction = 0; // CompareFunction::Always
            int passOperation = 0; // StencilOperation::Keep
            int failOperation = 0;
            int depthFailOperation = 0;
            std::uint8_t readMask = 0xFF;
            std::uint8_t writeMask = 0xFF;
            std::uint8_t reference = 0;
            bool twoSided = false;
            int ccwCompareFunction = 0;
            int ccwPassOperation = 0;
            int ccwFailOperation = 0;
            int ccwDepthFailOperation = 0;
        };

        /// REMED-GFX-030: XNA/FNA compares the incoming fragment depth (left operand) with the
        /// currently stored depth (right operand). Every public CompareFunction is mapped explicitly;
        /// an invalid ordinal is rejected instead of silently falling back to a different relation.
        bool DepthComparisonPasses(float incoming, float stored, int compareFunction)
        {
            switch (compareFunction)
            {
                case 0: return true;                 // Always
                case 1: return false;                // Never
                case 2: return incoming <  stored;   // Less
                case 3: return incoming <= stored;   // LessEqual
                case 4: return incoming == stored;   // Equal
                case 5: return incoming >= stored;   // GreaterEqual
                case 6: return incoming >  stored;   // Greater
                case 7: return incoming != stored;   // NotEqual
                default:
                    throw std::runtime_error(
                        "SoftwareRenderer: unsupported depth CompareFunction ordinal");
            }
        }

        bool DepthFragmentPasses(const RasterDepthState& state, float incoming, float stored)
        {
            return !state.testEnabled ||
                   DepthComparisonPasses(incoming, stored, state.compareFunction);
        }

        void WritePassingDepth(float& storedDepth, const RasterDepthState& state, float depth)
        {
            // As on the correct EasyGL/D3D paths, disabling the depth test disables the complete
            // depth operation even if a custom state happens to retain writeEnable=true.
            if (state.testEnabled && state.writeEnabled)
                storedDepth = depth;
        }

        bool StencilComparisonPasses(std::uint8_t reference, std::uint8_t stored,
                                     std::uint8_t readMask, int compareFunction)
        {
            const int incoming = reference & readMask;
            const int destination = stored & readMask;
            switch (compareFunction)
            {
                case 0: return true;                    // Always
                case 1: return false;                   // Never
                case 2: return incoming <  destination; // Less
                case 3: return incoming <= destination; // LessEqual
                case 4: return incoming == destination; // Equal
                case 5: return incoming >= destination; // GreaterEqual
                case 6: return incoming >  destination; // Greater
                case 7: return incoming != destination; // NotEqual
                default:
                    throw std::runtime_error(
                        "SoftwareRenderer: unsupported stencil CompareFunction ordinal");
            }
        }

        std::uint8_t ApplyStencilOperation(std::uint8_t stored, std::uint8_t reference,
                                           int operation)
        {
            switch (operation)
            {
                case 0: return stored; // Keep
                case 1: return 0; // Zero
                case 2: return reference; // Replace
                case 3: return static_cast<std::uint8_t>(stored + 1u); // Increment (wrap)
                case 4: return static_cast<std::uint8_t>(stored - 1u); // Decrement (wrap)
                case 5: return stored == 0xFF ? 0xFF : static_cast<std::uint8_t>(stored + 1u);
                case 6: return stored == 0 ? 0 : static_cast<std::uint8_t>(stored - 1u);
                case 7: return static_cast<std::uint8_t>(~stored); // Invert
                default:
                    throw std::runtime_error(
                        "SoftwareRenderer: unsupported StencilOperation ordinal");
            }
        }

        void WriteStencil(std::uint8_t& storedValue, const RasterStencilState& state,
                          int operation)
        {
            const std::uint8_t oldValue = storedValue;
            const std::uint8_t operationValue =
                ApplyStencilOperation(oldValue, state.reference, operation);
            storedValue = static_cast<std::uint8_t>(
                (oldValue & static_cast<std::uint8_t>(~state.writeMask)) |
                (operationValue & state.writeMask));
        }

        /// SOFTWARE-121: snapshots both face-specific operation tuples once per public draw.
        /// ReferenceStencil and both masks are device properties shared by the two faces.
        RasterStencilState SnapshotStencilState(const SoftwareRenderer& renderer)
        {
            return RasterStencilState{
                renderer.IsStencilTestEnabled(), renderer.GetStencilCompareFunction(),
                renderer.GetStencilPassOperation(), renderer.GetStencilFailOperation(),
                renderer.GetStencilDepthFailOperation(),
                static_cast<std::uint8_t>(renderer.GetStencilReadMask()),
                static_cast<std::uint8_t>(renderer.GetStencilWriteMask()),
                static_cast<std::uint8_t>(renderer.GetReferenceStencil()),
                renderer.IsTwoSidedStencilEnabled(),
                renderer.GetCounterClockwiseStencilCompareFunction(),
                renderer.GetCounterClockwiseStencilPassOperation(),
                renderer.GetCounterClockwiseStencilFailOperation(),
                renderer.GetCounterClockwiseStencilDepthFailOperation()};
        }

        /// The public CounterClockwiseStencil* tuple belongs to XNA back faces. In Software's
        /// top-left framebuffer coordinates those have negative signed area (the same convention
        /// used by ShouldCullTriangle); non-triangle primitives use the ordinary/front tuple.
        RasterStencilState SelectStencilFace(const RasterStencilState& state,
                                              bool counterClockwiseFace)
        {
            RasterStencilState selected = state;
            if (state.twoSided && counterClockwiseFace)
            {
                selected.compareFunction = state.ccwCompareFunction;
                selected.passOperation = state.ccwPassOperation;
                selected.failOperation = state.ccwFailOperation;
                selected.depthFailOperation = state.ccwDepthFailOperation;
            }
            return selected;
        }

        /// SOFTWARE-110: applies depth and stencil independently to each covered sample and
        /// returns the subset that survives. Single-sample storage keeps its original bit-0 path;
        /// the optional depth array supplies plane-evaluated depths at the four coverage locations.
        unsigned int ApplyFragmentTests(
            SoftwareFramebuffer& fb, const RasterDepthState& depthState,
            const RasterStencilState& stencilState, std::size_t pixelIndex,
            unsigned int activeSamples, float centerDepth,
            const std::array<float, 4>* sampleDepths,
            SoftwareOcclusionQueryRenderer* occlusionQuery)
        {
            if (!fb.HasMultiSampleColor())
            {
                if ((activeSamples & 1u) == 0u)
                    return 0u;
                const bool stencilAvailable =
                    stencilState.testEnabled && !fb.stencilBuffer.empty();
                const bool depthAvailable =
                    depthState.testEnabled && !fb.depthBuffer.empty();
                if (stencilAvailable)
                {
                    std::uint8_t& stencil = fb.stencilBuffer[pixelIndex];
                    if (!StencilComparisonPasses(stencilState.reference, stencil,
                                                 stencilState.readMask,
                                                 stencilState.compareFunction))
                    {
                        WriteStencil(stencil, stencilState, stencilState.failOperation);
                        return 0u;
                    }
                }
                if (depthAvailable)
                {
                    if (!DepthFragmentPasses(depthState, centerDepth,
                                             fb.depthBuffer[pixelIndex]))
                    {
                        if (stencilAvailable)
                            WriteStencil(fb.stencilBuffer[pixelIndex], stencilState,
                                         stencilState.depthFailOperation);
                        return 0u;
                    }
                }
                if (stencilAvailable)
                    WriteStencil(fb.stencilBuffer[pixelIndex], stencilState,
                                 stencilState.passOperation);
                if (depthAvailable)
                    WritePassingDepth(fb.depthBuffer[pixelIndex], depthState, centerDepth);
                if (occlusionQuery != nullptr)
                    occlusionQuery->RecordPassingSamples(1u);
                return 1u;
            }

            activeSamples &= 0xFu;
            if (activeSamples == 0u)
                return 0u;
            const bool stencilAvailable =
                stencilState.testEnabled && !fb.multiSampleStencilBuffer.empty();
            const bool depthAvailable =
                depthState.testEnabled && !fb.multiSampleDepthBuffer.empty();

            unsigned int passingSamples = 0u;
            for (int sample = 0; sample < 4; ++sample)
            {
                const unsigned int sampleBit = 1u << sample;
                if ((activeSamples & sampleBit) == 0u)
                    continue;
                const std::size_t sampleIndex = pixelIndex * 4u +
                                                static_cast<std::size_t>(sample);
                if (stencilAvailable)
                {
                    std::uint8_t& stencil = fb.multiSampleStencilBuffer[sampleIndex];
                    if (!StencilComparisonPasses(stencilState.reference, stencil,
                                                 stencilState.readMask,
                                                 stencilState.compareFunction))
                    {
                        WriteStencil(stencil, stencilState, stencilState.failOperation);
                        continue;
                    }
                }
                const float sampleDepth = sampleDepths != nullptr
                    ? (*sampleDepths)[static_cast<std::size_t>(sample)] : centerDepth;
                if (depthAvailable && !DepthFragmentPasses(
                        depthState, sampleDepth, fb.multiSampleDepthBuffer[sampleIndex]))
                {
                    if (stencilAvailable)
                        WriteStencil(fb.multiSampleStencilBuffer[sampleIndex], stencilState,
                                     stencilState.depthFailOperation);
                    continue;
                }
                if (stencilAvailable)
                    WriteStencil(fb.multiSampleStencilBuffer[sampleIndex], stencilState,
                                 stencilState.passOperation);
                if (depthAvailable)
                    WritePassingDepth(fb.multiSampleDepthBuffer[sampleIndex], depthState,
                                      sampleDepth);
                passingSamples |= sampleBit;
            }
            if (occlusionQuery != nullptr)
                occlusionQuery->RecordPassingSamples(passingSamples);
            return passingSamples;
        }

        /// One vertex in clip space (before the perspective divide), attributes NOT premultiplied
        /// by W (SOFTWARE-83/106). Clip space is still linear -- position and attributes can both be
        /// interpolated with a plain lerp here, unlike the post-divide RasterVertex above.
        struct ClipVertex
        {
            float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
            float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
            float u = 0.0f, v = 0.0f;
            float u1 = 0.0f, v1 = 0.0f;
            float fogKeep = 1.0f;
            /// Per-vertex BasicEffect specular result (SOFTWARE-113).
            float sr = 0.0f, sg = 0.0f, sb = 0.0f;
            /// EnvironmentMapEffect vertex reflection direction/blend factor (SOFTWARE-114).
            float envx = 0.0f, envy = 0.0f, envz = 1.0f, envBlend = 0.0f;
            /// World-space position/normal (SOFTWARE-82/113).
            float wpx = 0.0f, wpy = 0.0f, wpz = 0.0f;
            float nx = 0.0f, ny = 0.0f, nz = 1.0f;
            /// SpriteBatch viewport-local homogeneous position before its orthographic projection.
            float spriteX = 0.0f, spriteY = 0.0f;
        };

        /// Reads a packed little-endian RGBA8 Color (Microsoft::Xna::Framework::Color's own
        /// documented layout: R in bits 0-7, G in 8-15, B in 16-23, A in 24-31) directly from raw
        /// vertex bytes -- avoids depending on the Color type's public API just to unpack 4 bytes.
        void UnpackColorBytes(const std::uint8_t* bytes, float& r, float& g, float& b, float& a)
        {
            r = bytes[0] / 255.0f;
            g = bytes[1] / 255.0f;
            b = bytes[2] / 255.0f;
            a = bytes[3] / 255.0f;
        }

        /// Transforms a VertexPositionColor vertex (Position at offset 0, Color at offset 12 --
        /// DrawColoredPrimitives/DrawIndexedColoredPrimitives's own fixed layout, matching the
        /// interface's documented "equivalent to BasicEffect with VertexColorEnabled = true")
        /// into clip space. Attributes are left un-premultiplied -- frustum clipping (SOFTWARE-106)
        /// happens on ClipVertex, before the perspective divide.
        ClipVertex BuildPositionColorClipVertex(const std::uint8_t* raw, const Matrix& combined)
        {
            Vector3 position;
            std::memcpy(&position, raw, sizeof(Vector3));
            const Vector4 clip = Vector4::Transform(position, combined);

            ClipVertex out;
            out.x = clip.X; out.y = clip.Y; out.z = clip.Z; out.w = clip.W;
            UnpackColorBytes(raw + sizeof(Vector3), out.r, out.g, out.b, out.a);
            return out;
        }

        /// Linearly interpolates two clip-space vertices -- valid because clip space (unlike
        /// screen space) is still linear; the perspective divide is exactly the step that makes
        /// interpolation non-linear, and it hasn't happened yet here.
        ClipVertex LerpClipVertex(const ClipVertex& a, const ClipVertex& b, float t)
        {
            ClipVertex out;
            out.x = a.x + t * (b.x - a.x);
            out.y = a.y + t * (b.y - a.y);
            out.z = a.z + t * (b.z - a.z);
            out.w = a.w + t * (b.w - a.w);
            out.r = a.r + t * (b.r - a.r);
            out.g = a.g + t * (b.g - a.g);
            out.b = a.b + t * (b.b - a.b);
            out.a = a.a + t * (b.a - a.a);
            out.u = a.u + t * (b.u - a.u);
            out.v = a.v + t * (b.v - a.v);
            out.u1 = a.u1 + t * (b.u1 - a.u1);
            out.v1 = a.v1 + t * (b.v1 - a.v1);
            out.fogKeep = a.fogKeep + t * (b.fogKeep - a.fogKeep);
            out.sr = a.sr + t * (b.sr - a.sr);
            out.sg = a.sg + t * (b.sg - a.sg);
            out.sb = a.sb + t * (b.sb - a.sb);
            out.envx = a.envx + t * (b.envx - a.envx);
            out.envy = a.envy + t * (b.envy - a.envy);
            out.envz = a.envz + t * (b.envz - a.envz);
            out.envBlend = a.envBlend + t * (b.envBlend - a.envBlend);
            out.wpx = a.wpx + t * (b.wpx - a.wpx);
            out.wpy = a.wpy + t * (b.wpy - a.wpy);
            out.wpz = a.wpz + t * (b.wpz - a.wpz);
            out.nx = a.nx + t * (b.nx - a.nx);
            out.ny = a.ny + t * (b.ny - a.ny);
            out.nz = a.nz + t * (b.nz - a.nz);
            out.spriteX = a.spriteX + t * (b.spriteX - a.spriteX);
            out.spriteY = a.spriteY + t * (b.spriteY - a.spriteY);
            return out;
        }

        /// SOFTWARE-106: XNA uses the Direct3D homogeneous clip volume: -W <= X,Y <= W and
        /// 0 <= Z <= W. A non-negative signed distance therefore means that the vertex is on the
        /// visible side of the selected plane. Clipping happens before the perspective divide.
        enum class HomogeneousClipPlane
        {
            Left,
            Right,
            Bottom,
            Top,
            Near,
            Far
        };

        constexpr std::array<HomogeneousClipPlane, 6> kHomogeneousClipPlanes = {
            HomogeneousClipPlane::Left,
            HomogeneousClipPlane::Right,
            HomogeneousClipPlane::Bottom,
            HomogeneousClipPlane::Top,
            HomogeneousClipPlane::Near,
            HomogeneousClipPlane::Far,
        };

        constexpr int kMaxClippedTriangleVertices = 9;

        float ClipPlaneDistance(const ClipVertex& vertex, HomogeneousClipPlane plane)
        {
            switch (plane)
            {
                case HomogeneousClipPlane::Left:   return vertex.x + vertex.w;
                case HomogeneousClipPlane::Right:  return vertex.w - vertex.x;
                case HomogeneousClipPlane::Bottom: return vertex.y + vertex.w;
                case HomogeneousClipPlane::Top:    return vertex.w - vertex.y;
                case HomogeneousClipPlane::Near:   return vertex.z;
                case HomogeneousClipPlane::Far:    return vertex.w - vertex.z;
            }
            return -1.0f;
        }

        bool IsInsideClipVolume(const ClipVertex& vertex)
        {
            for (const HomogeneousClipPlane plane : kHomogeneousClipPlanes)
            {
                if (!(ClipPlaneDistance(vertex, plane) >= 0.0f))
                    return false;
            }
            // The six D3D inequalities imply W >= 0. At the singular W=0 apex the perspective
            // divide is undefined, so a point exactly there has no rasterizable sample.
            return vertex.w > 0.0f;
        }

        /// Clips a triangle against all six XNA/D3D clip planes using Sutherland-Hodgman. A convex
        /// triangle can gain at most one vertex per plane, hence the nine-vertex bound. Every
        /// varying is interpolated in homogeneous space and polygon order is retained.
        int ClipTriangleToFrustum(
            const ClipVertex verts[3],
            std::array<ClipVertex, kMaxClippedTriangleVertices>& out)
        {
            std::array<ClipVertex, kMaxClippedTriangleVertices> input{};
            input[0] = verts[0];
            input[1] = verts[1];
            input[2] = verts[2];
            int inputCount = 3;

            for (const HomogeneousClipPlane plane : kHomogeneousClipPlanes)
            {
                int outputCount = 0;
                ClipVertex previous = input[static_cast<std::size_t>(inputCount - 1)];
                float previousDistance = ClipPlaneDistance(previous, plane);
                bool previousInside = previousDistance >= 0.0f;

                for (int i = 0; i < inputCount; ++i)
                {
                    const ClipVertex current = input[static_cast<std::size_t>(i)];
                    const float currentDistance = ClipPlaneDistance(current, plane);
                    const bool currentInside = currentDistance >= 0.0f;
                    if (currentInside != previousInside)
                    {
                        const float t = previousDistance / (previousDistance - currentDistance);
                        out[static_cast<std::size_t>(outputCount++)] =
                            LerpClipVertex(previous, current, t);
                    }
                    if (currentInside)
                        out[static_cast<std::size_t>(outputCount++)] = current;

                    previous = current;
                    previousDistance = currentDistance;
                    previousInside = currentInside;
                }

                if (outputCount == 0)
                    return 0;
                input = out;
                inputCount = outputCount;
            }

            out = input;
            return inputCount;
        }

        /// Clips a segment against the complete homogeneous frustum. Updating the outside endpoint
        /// at each plane is the segment equivalent of the polygon clip and preserves all varyings.
        bool ClipLineToFrustum(ClipVertex& a, ClipVertex& b)
        {
            for (const HomogeneousClipPlane plane : kHomogeneousClipPlanes)
            {
                const float aDistance = ClipPlaneDistance(a, plane);
                const float bDistance = ClipPlaneDistance(b, plane);
                const bool aInside = aDistance >= 0.0f;
                const bool bInside = bDistance >= 0.0f;
                if (!aInside && !bInside)
                    return false;
                if (aInside && bInside)
                    continue;

                const float t = aDistance / (aDistance - bDistance);
                const ClipVertex intersection = LerpClipVertex(a, b, t);
                if (!aInside)
                    a = intersection;
                else
                    b = intersection;
            }
            return a.w > 0.0f && b.w > 0.0f;
        }

        /// REMED-GFX-079: the XNA/FNA viewport transform parameters used to map a post-perspective-
        /// divide NDC position into framebuffer pixel coordinates -- Viewport.X/Y (pixel origin of
        /// the viewport within the active target), Viewport.Width/Height (pixel extent the NDC
        /// [-1,1] range is mapped over), and the MinDepth/MaxDepth depth range. A default full-target
        /// viewport is {0, 0, framebufferWidth, framebufferHeight, 0, 1}, for which the transform in
        /// ClipVertexToRasterVertex reduces to the pre-GFX-079 full-framebuffer mapping byte-for-byte.
        struct ViewportTransform
        {
            float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
            float minDepth = 0.0f, maxDepth = 1.0f;
            bool multisampledDestination = false;
        };

        /// Converts one clip-space vertex into a screen-space RasterVertex: perspective divide, the
        /// XNA/FNA viewport transform, and premultiplying color/UV by invW for perspective-correct
        /// barycentric interpolation later. REMED-GFX-079: the viewport transform matches FNA's
        /// Viewport.Project exactly --
        ///   screenX = (ndcX*0.5 + 0.5) * Viewport.Width  + Viewport.X
        ///   screenY = (1 - (ndcY*0.5 + 0.5)) * Viewport.Height + Viewport.Y
        ///   depth   = Viewport.MinDepth + ndcZ * (Viewport.MaxDepth - Viewport.MinDepth)
        /// -- so a custom GraphicsDevice.Viewport positions (X/Y), sub-scales (Width/Height), and
        /// depth-range-remaps 3D geometry, instead of the old mapping over the full framebuffer.
        RasterVertex ClipVertexToRasterVertexWithOffset(
            const ClipVertex& cv, const ViewportTransform& vp, float pixelCenterOffset)
        {
            const float invW = 1.0f / cv.w;
            const float ndcX = cv.x * invW;
            const float ndcY = cv.y * invW;
            const float ndcZ = cv.z * invW;

            RasterVertex out;
            out.x = (ndcX * 0.5f + 0.5f) * vp.width + vp.x + pixelCenterOffset;
            out.y = (1.0f - (ndcY * 0.5f + 0.5f)) * vp.height + vp.y + pixelCenterOffset;
            out.depth = vp.minDepth + ndcZ * (vp.maxDepth - vp.minDepth);
            out.invW = invW;
            out.r = cv.r * invW;
            out.g = cv.g * invW;
            out.b = cv.b * invW;
            out.a = cv.a * invW;
            out.u = cv.u * invW;
            out.v = cv.v * invW;
            out.u1 = cv.u1 * invW;
            out.v1 = cv.v1 * invW;
            out.fogKeep = cv.fogKeep * invW;
            out.sr = cv.sr * invW;
            out.sg = cv.sg * invW;
            out.sb = cv.sb * invW;
            out.envx = cv.envx * invW;
            out.envy = cv.envy * invW;
            out.envz = cv.envz * invW;
            out.envBlend = cv.envBlend * invW;
            out.wpx = cv.wpx * invW;
            out.wpy = cv.wpy * invW;
            out.wpz = cv.wpz * invW;
            out.nx = cv.nx * invW;
            out.ny = cv.ny * invW;
            out.nz = cv.nz * invW;
            return out;
        }

        RasterVertex ClipVertexToRasterVertex(const ClipVertex& cv, const ViewportTransform& vp)
        {
            // SOFTWARE-131: XNA 4.0's Direct3D 9 raster coordinates place pixel centers at integer
            // screen coordinates. This CPU rasterizer samples in the conventional corner-origin
            // space at pixel + 0.5, so translate geometry by Wine/MonoGame/EasyGL's established
            // 63/128-pixel amount. Staying just below one half selects the XNA side of exact fill
            // edges after finite subpixel precision instead of leaving the result on the tie.
            // REMED-GFX-235/SOFTWARE-134: the single-sample correction is deliberately absent
            // from multisampled destinations. Moving four subpixel sample locations by
            // almost half a pixel drops three samples at the outer corner and two on each outer
            // edge. EasyGL established the same distinction against the shared render-target
            // readback corpus; XNA's measured one-pixel triangle remains protected on ordinary
            // single-sample destinations.
            constexpr float kXnaPixelCenterOffset = 63.0f / 128.0f;
            const float pixelCenterOffset =
                vp.multisampledDestination ? 0.0f : kXnaPixelCenterOffset;
            return ClipVertexToRasterVertexWithOffset(cv, vp, pixelCenterOffset);
        }

        /// SOFTWARE-338: SpriteBatch's existing screen-space route already aligns rectangle edges
        /// to pixel corners. Preserve that placement while adding the same homogeneous clipping,
        /// perspective divide and viewport depth mapping used by the 3D routes.
        RasterVertex SpriteClipVertexToRasterVertex(
            const ClipVertex& cv, const ViewportTransform& vp)
        {
            RasterVertex out = ClipVertexToRasterVertexWithOffset(cv, vp, 0.0f);
            const float invW = 1.0f / cv.w;
            // The orthographic projection and viewport transform algebraically cancel for X/Y.
            // Recover the preserved pre-projection coordinates directly so an ordinary W=1
            // SpriteBatch transform does not acquire a rounding displacement at half-pixel edges.
            // Clipped W!=1 vertices still receive the required perspective divide.
            out.x = cv.spriteX * invW + vp.x;
            out.y = cv.spriteY * invW + vp.y;
            return out;
        }

        float EdgeFunction(float ax, float ay, float bx, float by, float px, float py)
        {
            return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
        }

        /// The effective per-triangle depth offset added to every fragment's post-viewport depth.
        /// XNA's public constant bias is already expressed in normalized window-depth units. FNA3D
        /// therefore multiplies it by `(2^depthBits)-1` before passing it to APIs whose native constant
        /// is measured in minimum-resolvable-depth units (GL polygon-offset units and D3D11's integer
        /// DepthBias). Software stores normalized float depth directly, so applying that conversion and
        /// then multiplying by a reciprocal here would be redundant: the public value is the offset.
        ///   offset = slopeScaleDepthBias * m + depthBias
        /// where m = max(|dz/dx|, |dz/dy|) is the triangle's maximum depth slope in raster space (screen
        /// x/y pixels, post-viewport window depth) -- computed ONCE per triangle from the depth plane,
        /// never per fragment.
        /// SIGN: a POSITIVE bias INCREASES depth == pushes the polygon AWAY from the camera (toward the far
        /// plane), the standard XNA/D3D/GL/Vulkan convention. Returns exactly 0.0f when both biases are 0
        /// (the overwhelmingly common case) with no slope math, so the zero-bias path stays byte-identical
        /// to pre-GFX-083. The det (== -EdgeFunction(v0,v1,v2)) is guaranteed nonzero here: the callers
        /// reject the exact zero-area triangle before invoking this, so no divide-by-zero (guarded anyway).
        float ComputeDepthBiasOffset(const RasterVertex& v0, const RasterVertex& v1, const RasterVertex& v2,
                                     float depthBias, float slopeScaleDepthBias)
        {
            if (depthBias == 0.0f && slopeScaleDepthBias == 0.0f)
                return 0.0f;
            float maxDepthSlope = 0.0f;
            if (slopeScaleDepthBias != 0.0f)
            {
                const float dx1 = v1.x - v0.x, dy1 = v1.y - v0.y, dz1 = v1.depth - v0.depth;
                const float dx2 = v2.x - v0.x, dy2 = v2.y - v0.y, dz2 = v2.depth - v0.depth;
                const float det = dx1 * dy2 - dx2 * dy1;
                if (det != 0.0f)
                {
                    const float dzdx = (dz1 * dy2 - dz2 * dy1) / det;
                    const float dzdy = (dx1 * dz2 - dx2 * dz1) / det;
                    maxDepthSlope = std::max(std::fabs(dzdx), std::fabs(dzdy));
                }
            }
            return slopeScaleDepthBias * maxDepthSlope + depthBias;
        }

        /// REMED-GFX-073/079: an inclusive pixel clip rectangle for the rasterizer. Pre-intersected
        /// with the framebuffer bounds by its ViewportClip() factory, so RasterizeTriangle /
        /// RasterizeTriangleShaded only have to clamp each triangle's bounding box against it. An
        /// empty rectangle (minX>maxX or minY>maxY) draws nothing -- the raster loops simply do not
        /// execute. GFX-080 will later intersect this same rectangle with the ScissorRectangle when
        /// scissor testing is enabled, reusing this exact clip path.
        struct RasterClipRect
        {
            int minX = 0, minY = 0, maxX = -1, maxY = -1;
        };

        /**
         * Converts floating-point geometry bounds into clipped inclusive candidate-pixel bounds.
         * Comparisons against the framebuffer-sized clip happen before float-to-int conversion,
         * avoiding undefined casts for huge coordinates and infinities. NaN and empty or wholly
         * clipped geometry deterministically produce no bounds.
         */
        bool CalculateRasterBounds(float minXf, float minYf, float maxXf, float maxYf,
                                   const RasterClipRect& clip,
                                   int& minX, int& minY, int& maxX, int& maxY)
        {
            if (clip.minX > clip.maxX || clip.minY > clip.maxY ||
                std::isnan(minXf) || std::isnan(minYf) ||
                std::isnan(maxXf) || std::isnan(maxYf) ||
                static_cast<double>(minXf) >= static_cast<double>(clip.maxX) + 1.0 ||
                static_cast<double>(minYf) >= static_cast<double>(clip.maxY) + 1.0 ||
                static_cast<double>(maxXf) <= static_cast<double>(clip.minX) - 1.0 ||
                static_cast<double>(maxYf) <= static_cast<double>(clip.minY) - 1.0)
            {
                return false;
            }

            minX = minXf <= static_cast<float>(clip.minX)
                ? clip.minX : static_cast<int>(std::floor(minXf));
            minY = minYf <= static_cast<float>(clip.minY)
                ? clip.minY : static_cast<int>(std::floor(minYf));
            maxX = maxXf >= static_cast<float>(clip.maxX)
                ? clip.maxX : static_cast<int>(std::ceil(maxXf));
            maxY = maxYf >= static_cast<float>(clip.maxY)
                ? clip.maxY : static_cast<int>(std::ceil(maxYf));
            return minX <= maxX && minY <= maxY;
        }

        /// REMED-GFX-150: env-gated sampler trace. This is how the point path's "exactly one texel
        /// fetch per sample" claim is MEASURED rather than asserted from reading the code, and how a
        /// single destination pixel's addressing can be dumped when a result is disputed.
        ///
        ///   CNA_SOFTWARE_SAMPLE_TRACE unset or 0  off. One predictable, perfectly-predicted branch
        ///                                         per sample; no counting, no formatting, no output.
        ///   CNA_SOFTWARE_SAMPLE_TRACE=1           count samples and texel fetches; print a summary
        ///                                         line to stderr at exit.
        ///   CNA_SOFTWARE_SAMPLE_TRACE=2           additionally print one line per sample --
        ///                                         triangle, destination pixel, filter, address
        ///                                         modes, texture size, interpolated u/v, addressed
        ///                                         texel coordinates, linear neighbours and weights,
        ///                                         and the sampled RGBA -- bounded by
        ///                                         CNA_SOFTWARE_SAMPLE_TRACE_LIMIT (default 64) so a
        ///                                         full frame cannot flood the terminal.
        struct SamplerTrace
        {
            bool enabled = false;
            bool verbose = false;
            long long limit = 64;
            long long printed = 0;
            long long pointSamples = 0;
            long long linearSamples = 0;
            long long texelFetches = 0;
            long long triangles = 0;
            int fragX = -1;
            int fragY = -1;

            ~SamplerTrace()
            {
                if (!enabled) return;
                std::fprintf(stderr,
                             "[CNA_SOFTWARE_SAMPLE_TRACE] triangles=%lld pointSamples=%lld "
                             "linearSamples=%lld texelFetches=%lld fetchesPerPointSample=%.4f "
                             "fetchesPerLinearSample=%.4f\n",
                             triangles, pointSamples, linearSamples, texelFetches,
                             pointSamples ? static_cast<double>(texelFetches -
                                            4 * linearSamples) / static_cast<double>(pointSamples) : 0.0,
                             linearSamples ? 4.0 : 0.0);
            }
        };

        SamplerTrace MakeSamplerTrace()
        {
            SamplerTrace t;
            const char* mode = std::getenv("CNA_SOFTWARE_SAMPLE_TRACE");
            if (mode == nullptr || mode[0] == '0' || mode[0] == '\0') return t;
            t.enabled = true;
            t.verbose = (mode[0] == '2');
            if (const char* lim = std::getenv("CNA_SOFTWARE_SAMPLE_TRACE_LIMIT"))
            {
                const long long parsed = std::atoll(lim);
                if (parsed > 0) t.limit = parsed;
            }
            return t;
        }

        /// Namespace-scope (not function-local) so a disabled trace costs one load and one branch per
        /// sample, with no thread-safe-static guard.
        SamplerTrace g_samplerTrace = MakeSamplerTrace();

        /// REMED-GFX-182: env-gated ENVIRONMENT-MAP CUBE trace. The 2D trace above measures the
        /// sampler REMED-GFX-150 gave the ordinary texture path; this one measures what reaches the
        /// reflection cube, which is a different question and had a different answer.
        ///
        /// It prints, on one line per env-map fragment, BOTH ends of the sampler pipeline: the
        /// public `GraphicsDevice.SamplerStates[0]` and `SamplerStates[1]` descriptions captured for
        /// this draw (`slot0=` / `slot1=`) and the description the cube sample EFFECTIVELY ran under
        /// (`eff=`), together with every stage between the reflection vector and the final effect
        /// contribution. A divergence between `slot1=` and `eff=` is the whole finding: pre-fix
        /// `SampleCubeMap` took no sampler at all, so `eff=` was the same fixed
        /// point/clamp/clamp/level-0 description on every draw however `slot1=` was set.
        ///
        ///   CNA_SOFTWARE_CUBE_TRACE unset or 0  off; one predictable branch per cube sample.
        ///   CNA_SOFTWARE_CUBE_TRACE=1           count cube samples, texel fetches and the DISTINCT
        ///                                       captured/effective descriptions; print a summary at
        ///                                       exit.
        ///   CNA_SOFTWARE_CUBE_TRACE=2           additionally print one line per env-map fragment,
        ///                                       bounded by CNA_SOFTWARE_CUBE_TRACE_LIMIT
        ///                                       (default 64).
        struct CubeTrace
        {
            bool enabled = false;
            bool verbose = false;
            long long limit = 64;
            long long printed = 0;
            /// CNA_SOFTWARE_CUBE_TRACE_DRAW: print only this draw's lines (0 = every draw). A whole
            /// fixture issues tens of thousands of cube samples, so naming ONE draw is what keeps a
            /// per-fragment dump readable while still covering it completely.
            long long drawFilter = 0;

            /// Draw bookkeeping, stamped by the draw entry points (trace-only).
            long long drawId = 0;
            const char* family = "-";

            /// The two public slot descriptions captured for the triangle being rasterized.
            int slot0Filter = 0, slot0AddrU = 1, slot0AddrV = 1;
            int slot1Filter = 0, slot1AddrU = 1, slot1AddrV = 1;

            /// Cardinality.
            long long cubeSamples = 0;
            long long cubeTexelFetches = 0;
            long long cubeLevelSamples = 0;

            /// Distinct (filter, addressU, addressV) triples the PUBLIC slot 1 held at a cube sample.
            std::set<std::uint32_t> capturedKeys;
            /// Distinct (within-level filter, addressU, addressV, mip mode, level(s)) the cube sample
            /// really ran under. One element means the public state never reached the cube.
            std::set<std::uint32_t> effectiveKeys;

            /// The stage values of the cube sample in progress, filled by SampleCubeMap and printed
            /// by WriteShadedFragment once the effect contribution is known.
            float dirX = 0.0f, dirY = 0.0f, dirZ = 0.0f;
            int face = -1;
            float faceU = 0.0f, faceV = 0.0f;
            int levelCount = 1;
            float lambda = 0.0f;
            int lowLevel = 0, highLevel = 0;
            float mipWeight = 0.0f;
            bool mipPoint = true;
            bool magnify = true;
            bool withinPoint = true;
            int addressU = 1, addressV = 1;
            int texelX = 0, texelY = 0;
            int fetchCount = 0;
            float sampleR = 0.0f, sampleG = 0.0f, sampleB = 0.0f, sampleA = 0.0f;

            ~CubeTrace()
            {
                if (!enabled) return;
                std::fprintf(stderr,
                             "[CNA_SOFTWARE_CUBE_TRACE] cubeSamples=%lld texelFetches=%lld "
                             "levelSamples=%lld fetchesPerSample=%.4f distinctCaptured=%zu "
                             "distinctEffective=%zu\n",
                             cubeSamples, cubeTexelFetches, cubeLevelSamples,
                             cubeSamples ? static_cast<double>(cubeTexelFetches) /
                                           static_cast<double>(cubeSamples) : 0.0,
                             capturedKeys.size(), effectiveKeys.size());
            }
        };

        CubeTrace MakeCubeTrace()
        {
            CubeTrace t;
            const char* mode = std::getenv("CNA_SOFTWARE_CUBE_TRACE");
            if (mode == nullptr || mode[0] == '0' || mode[0] == '\0') return t;
            t.enabled = true;
            t.verbose = (mode[0] == '2');
            if (const char* lim = std::getenv("CNA_SOFTWARE_CUBE_TRACE_LIMIT"))
            {
                const long long parsed = std::atoll(lim);
                if (parsed > 0) t.limit = parsed;
            }
            if (const char* draw = std::getenv("CNA_SOFTWARE_CUBE_TRACE_DRAW"))
            {
                const long long parsed = std::atoll(draw);
                if (parsed > 0) t.drawFilter = parsed;
            }
            return t;
        }

        CubeTrace g_cubeTrace = MakeCubeTrace();

        /// REMED-GFX-182: a public (filter, addressU, addressV) triple as one comparable key.
        std::uint32_t SamplerDescKey(int filter, int addressU, int addressV)
        {
            return (static_cast<std::uint32_t>(filter & 0xFF) << 16) |
                   (static_cast<std::uint32_t>(addressU & 0xFF) << 8) |
                    static_cast<std::uint32_t>(addressV & 0xFF);
        }

        /// REMED-GFX-182: the address-mode ordinal name, for the trace only.
        const char* AddressName(int mode)
        {
            switch (mode)
            {
                case 0: return "Wrap";
                case 1: return "Clamp";
                case 2: return "Mirror";
                default: return "?";
            }
        }

        /// REMED-GFX-150: the largest magnitude a texel index may reach before the float->int
        /// conversion below stops being representable. A coordinate beyond this is saturated in
        /// FLOAT space first, so the conversion itself is always in range: an out-of-range
        /// float->int cast is undefined behaviour, which the UBSan build's -fsanitize=float-cast-
        /// overflow traps. Well inside int range, so the wrap/mirror arithmetic on the result
        /// (which needs 2*index) cannot overflow either.
        constexpr long long kTexelIndexLimit = 1 << 24;

        /// REMED-GFX-150: floor(value) as a texel index, saturating instead of invoking undefined
        /// behaviour. A NaN coordinate lands on the low limit (the `!(f > -limit)` test is false for
        /// NaN), so a non-finite texture coordinate produces a defined, addressable texel rather
        /// than an unpredictable index -- the same "deterministic rather than clever" stance the
        /// rest of this renderer's validation takes.
        long long FloorToTexelIndex(float value)
        {
            const float f = std::floor(value);
            if (!(f > -static_cast<float>(kTexelIndexLimit))) return -kTexelIndexLimit;
            if (f >= static_cast<float>(kTexelIndexLimit)) return kTexelIndexLimit;
            return static_cast<long long>(f);
        }

        /// REMED-GFX-150: XNA TextureAddressMode applied to an already-floored INTEGER texel index.
        /// The mode transforms the index, not a normalized coordinate -- Clamp reaches the first and
        /// last texel and blends with nothing, Wrap tiles at every integer junction, Mirror tiles and
        /// flips at every integer junction (period 2*size). @p mode is the raw XNA ordinal
        /// (0 = Wrap, 1 = Clamp, 2 = Mirror); anything else is treated as Clamp.
        int AddressTexel(long long i, int size, int mode)
        {
            const long long n = size;
            switch (mode)
            {
            case 0:
            {
                long long m = i % n;
                if (m < 0) m += n;
                return static_cast<int>(m);
            }
            case 2:
            {
                const long long period = 2 * n;
                long long m = i % period;
                if (m < 0) m += period;
                return static_cast<int>(m < n ? m : period - 1 - m);
            }
            default:
                return static_cast<int>(std::clamp<long long>(i, 0, n - 1));
            }
        }

        /// REMED-GFX-150: whether the MAGNIFICATION half of an XNA TextureFilter is Point.
        /// Ordinals: 0 Linear, 1 Point, 2 Anisotropic, 3 LinearMipPoint, 4 PointMipLinear,
        /// 5 MinLinearMagPointMipLinear, 6 MinLinearMagPointMipPoint,
        /// 7 MinPointMagLinearMipLinear, 8 MinPointMagLinearMipPoint.
        constexpr bool FilterMagnifiesWithPoint(int filter)
        {
            return filter == 1 || filter == 4 || filter == 5 || filter == 6;
        }

        /// REMED-GFX-150: whether the MINIFICATION half of an XNA TextureFilter is Point.
        constexpr bool FilterMinifiesWithPoint(int filter)
        {
            return filter == 1 || filter == 4 || filter == 7 || filter == 8;
        }

        /// SOFTWARE-117: a triangle's texture-space pixel footprint. The principal rates are
        /// singular values of the screen-to-texel Jacobian, while `isotropicRate` retains the
        /// established maximum-column LOD rule. `majorU/V` is the full major-axis footprint in
        /// normalized texture coordinates, independent of a mip level's dimensions.
        struct TextureFootprint
        {
            float isotropicRate = 1.0f;
            float majorRate = 1.0f;
            float minorRate = 1.0f;
            float majorU = 0.0f;
            float majorV = 0.0f;
        };

        /// REMED-GFX-150: the one authoritative texture sample for this renderer. Separates, in
        /// order: filter selection (magnification vs minification half of the XNA filter), texel
        /// addressing, address-mode transformation, and format decode. Reads exactly one texel for
        /// a point sample and exactly four for a linear one -- no allocation, no scratch buffer, no
        /// repacking of the source.
        ///
        /// Point selects `AddressMode(floor(u * width))`. NOT `u * width - 0.5`: that half-texel
        /// shift converts a position into the LINEAR path's lower neighbour index, and applying it
        /// to a point sample is exactly the GFX-150 defect (it makes the selected texel change
        /// half a texel early, and the pre-fix code then blended across the boundary as well).
        /// A coordinate exactly on a texel boundary selects the higher texel, matching the
        /// half-open [i, i+1) coverage the floor implies.
        ///
        /// Linear keeps the established construction: the neighbour pair is derived from the RAW
        /// (pre-address) indices so the two endpoints cannot collapse onto the same texel through a
        /// premature clamp -- clamping x0 first and computing x1 from the already-clamped value
        /// shifts x1 to the wrong texel at the texture edge (a real bug Software_Effects' own
        /// corner-sampling check caught). Under Clamp this is byte-identical to the pre-GFX-150
        /// behaviour; under Wrap/Mirror the neighbour now comes from the tiled/reflected side
        /// instead of being clamped, which is what makes LinearWrap differ from LinearClamp at all.
        ///
        /// REMED-GFX-124: takes the SoftwareColorSurface capability rather than the concrete
        /// SoftwareTextureRenderer, so a finished RenderTarget2D samples through this exact path --
        /// same filtering, same addressing, same sampler semantics as any other Software texture.
        ///
        /// REMED-GFX-175: samples WITHIN one mip level. @p level names the level; @p magnify still
        /// chooses only between the minification and magnification halves of the filter. Level
        /// SELECTION is the caller's job (SampleTexture below), which is what keeps the three
        /// components of an XNA TextureFilter -- min, mag and mip -- genuinely independent here.
        void SampleLevel(const SoftwareColorSurface& texture, int level,
                         const SoftwareSamplerState& sampler,
                         bool magnify, float u, float v,
                         float& r, float& g, float& b, float& a)
        {
            const int texW = std::max(1, texture.ColorWidth(level));
            const int texH = std::max(1, texture.ColorHeight(level));

            const bool point = magnify ? FilterMagnifiesWithPoint(sampler.filter)
                                       : FilterMinifiesWithPoint(sampler.filter);
            if (point)
            {
                const int x = AddressTexel(FloorToTexelIndex(u * static_cast<float>(texW)),
                                           texW, sampler.addressU);
                const int y = AddressTexel(FloorToTexelIndex(v * static_cast<float>(texH)),
                                           texH, sampler.addressV);
                texture.FetchColorTexel(level, x, y, r, g, b, a);
                // ONE texel, all four channels from it. REMED-GFX-182: counted whenever EITHER trace
                // is on, so the cube trace's fetch cardinality is measured here rather than inferred.
                if (g_samplerTrace.enabled || g_cubeTrace.enabled) g_samplerTrace.texelFetches += 1;
                if (g_samplerTrace.enabled)
                {
                    ++g_samplerTrace.pointSamples;
                    if (g_samplerTrace.verbose && g_samplerTrace.printed < g_samplerTrace.limit)
                    {
                        ++g_samplerTrace.printed;
                        std::fprintf(stderr,
                                     "[sample] tri=%lld dest=(%d,%d) filter=%d addr=(%d,%d) tex=%dx%d "
                                     "uv=(%.6f,%.6f) s=(%.6f,%.6f) POINT texel=(%d,%d) "
                                     "rgba=(%d,%d,%d,%d)\n",
                                     g_samplerTrace.triangles, g_samplerTrace.fragX, g_samplerTrace.fragY,
                                     sampler.filter, sampler.addressU, sampler.addressV, texW, texH,
                                     static_cast<double>(u), static_cast<double>(v),
                                     static_cast<double>(u) * texW, static_cast<double>(v) * texH,
                                     x, y,
                                     static_cast<int>(r * 255.0f + 0.5f), static_cast<int>(g * 255.0f + 0.5f),
                                     static_cast<int>(b * 255.0f + 0.5f), static_cast<int>(a * 255.0f + 0.5f));
                    }
                }
                return;
            }

            const float tx = u * static_cast<float>(texW) - 0.5f;
            const float ty = v * static_cast<float>(texH) - 0.5f;
            const long long x0raw = FloorToTexelIndex(tx);
            const long long y0raw = FloorToTexelIndex(ty);
            const int x0 = AddressTexel(x0raw, texW, sampler.addressU);
            const int y0 = AddressTexel(y0raw, texH, sampler.addressV);
            const int x1 = AddressTexel(x0raw + 1, texW, sampler.addressU);
            const int y1 = AddressTexel(y0raw + 1, texH, sampler.addressV);
            // The `!(0 <= f <= 1)` form rejects NaN too, so a non-finite coordinate collapses onto
            // the x0/y0 endpoint instead of leaving an uninitialized-looking weight.
            float fx = tx - std::floor(tx);
            float fy = ty - std::floor(ty);
            if (!(fx >= 0.0f && fx <= 1.0f)) fx = 0.0f;
            if (!(fy >= 0.0f && fy <= 1.0f)) fy = 0.0f;

            // REMED-GFX-182: written as nested LERPs (`a + (b-a)*t`) rather than as weighted sums
            // (`a*(1-t) + b*t`). The two are algebraically identical and cost the same four fetches,
            // but only this form is EXACT when the taps agree: `a + (a-a)*t` is `a` for every t,
            // where the weighted sum can land one ULP below it and the framebuffer's truncating
            // store then writes a byte one lower. Real hardware filters in fixed point and returns
            // the texel exactly for a uniform footprint; a linear filter over a uniform region must
            // return that region's value, on the 2D path as much as on the cube path this now
            // serves (a mip level of a flat cube face is exactly such a region).
            float r00, g00, b00, a00;
            float r10, g10, b10, a10;
            float r01, g01, b01, a01;
            float r11, g11, b11, a11;
            texture.FetchColorTexel(level, x0, y0, r00, g00, b00, a00);
            texture.FetchColorTexel(level, x1, y0, r10, g10, b10, a10);
            texture.FetchColorTexel(level, x0, y1, r01, g01, b01, a01);
            texture.FetchColorTexel(level, x1, y1, r11, g11, b11, a11);

            const auto bilerp = [&](float t00, float t10, float t01, float t11) -> float {
                const float top = t00 + (t10 - t00) * fx;
                const float bottom = t01 + (t11 - t01) * fx;
                return top + (bottom - top) * fy;
            };

            r = bilerp(r00, r10, r01, r11);
            g = bilerp(g00, g10, g01, g11);
            b = bilerp(b00, b10, b01, b11);
            a = bilerp(a00, a10, a01, a11);

            // Four neighbours, all four channels from each. Counted for either trace, see above.
            if (g_samplerTrace.enabled || g_cubeTrace.enabled) g_samplerTrace.texelFetches += 4;
            if (g_samplerTrace.enabled)
            {
                ++g_samplerTrace.linearSamples;
                if (g_samplerTrace.verbose && g_samplerTrace.printed < g_samplerTrace.limit)
                {
                    ++g_samplerTrace.printed;
                    std::fprintf(stderr,
                                 "[sample] tri=%lld dest=(%d,%d) filter=%d addr=(%d,%d) tex=%dx%d "
                                 "uv=(%.6f,%.6f) s=(%.6f,%.6f) LINEAR neighbours=(%d,%d)-(%d,%d) "
                                 "w=(%.6f,%.6f) rgba=(%d,%d,%d,%d)\n",
                                 g_samplerTrace.triangles, g_samplerTrace.fragX, g_samplerTrace.fragY,
                                 sampler.filter, sampler.addressU, sampler.addressV, texW, texH,
                                 static_cast<double>(u), static_cast<double>(v),
                                 static_cast<double>(u) * texW, static_cast<double>(v) * texH,
                                 x0, y0, x1, y1,
                                 static_cast<double>(fx), static_cast<double>(fy),
                                 static_cast<int>(r * 255.0f + 0.5f), static_cast<int>(g * 255.0f + 0.5f),
                                 static_cast<int>(b * 255.0f + 0.5f), static_cast<int>(a * 255.0f + 0.5f));
                }
            }
        }

        /// REMED-GFX-175: whether the MIPMAP half of an XNA TextureFilter is Point -- the third
        /// component of an ordinal, independent of the min and mag halves above.
        ///
        /// The authoritative decomposition is FNA's own XNAMip table: Linear(0) mip LINEAR,
        /// Point(1) mip POINT, Anisotropic(2) mip LINEAR, LinearMipPoint(3) POINT,
        /// PointMipLinear(4) LINEAR, MinLinearMagPointMipLinear(5) LINEAR,
        /// MinLinearMagPointMipPoint(6) POINT, MinPointMagLinearMipLinear(7) LINEAR,
        /// MinPointMagLinearMipPoint(8) POINT. Ordinals 0 and 1 are FULL filters that name a mip
        /// component like every other; reading them as "the ordinals without mipmapping" is the
        /// REMED-GFX-175 defect.
        constexpr bool FilterSelectsMipWithPoint(int filter)
        {
            return filter == 1 || filter == 3 || filter == 6 || filter == 8;
        }

        /// REMED-GFX-175: the one authoritative texture sample for this renderer, now with mip level
        /// selection in front of the within-level filtering that REMED-GFX-150 established.
        ///
        /// The stages are kept strictly separate and in this order:
        ///   1. the footprint arrives as @p lambda = log2(texels per destination pixel), computed
        ///      ONCE per triangle by TriangleTextureFootprint -- no per-fragment derivative work, no
        ///      allocation, no scratch buffer;
        ///   2. lambda is clamped to the levels this resource really HOLDS (ColorLevelCount), so a
        ///      declared-but-unwritten chain can never expose a level nobody filled;
        ///   3. the mip component of the filter selects the level: POINT picks exactly one, LINEAR
        ///      brackets two and weights them;
        ///   4. the min/mag component filters within each participating level, unchanged;
        ///   5. the address mode transforms texel indices, unchanged.
        ///
        /// A resource with one stored level takes the first branch, which is the pre-GFX-175 code
        /// path exactly: one call, same level, same filter, same fetch count. Point costs ONE
        /// within-level sample and Linear at most two, so nothing here changes the cost of an
        /// ordinary draw over a texture that has no chain.
        void SampleTextureIsotropic(const SoftwareColorSurface& texture,
                                    const SoftwareSamplerState& sampler,
                                    bool magnify, float lambda, float u, float v,
                                    float& r, float& g, float& b, float& a)
        {
            const int levels = std::max(1, texture.ColorLevelCount());
            if (levels <= 1)
            {
                SampleLevel(texture, 0, sampler, magnify, u, v, r, g, b, a);
                return;
            }

            const float maxLevel = static_cast<float>(levels - 1);
            // FNA3D maps MaxMipLevel to the sampler's minimum LOD and adds the explicit bias before
            // that clamp. Despite its historical XNA name, MaxMipLevel therefore means "most
            // detailed permitted level", not the largest numeric level the sampler may reach.
            // Microsoft writes the signed property through D3D9's DWORD state channel, so perform
            // that UInt32 conversion here at the common CPU sampling boundary; this covers both
            // GraphicsDevice sampler slots and SpriteBatch's renderer-private sampler snapshot.
            const float biased = lambda + sampler.lodBias;
            const float minLevel = static_cast<float>(std::min(
                static_cast<std::uint32_t>(sampler.maxMipLevel),
                static_cast<std::uint32_t>(levels - 1)));
            const float clamped = std::clamp(std::max(biased, minLevel), 0.0f, maxLevel);
            const bool effectiveMagnify = clamped <= 0.0f && (magnify || biased < 0.0f);
            if (!(clamped > 0.0f))
            {
                SampleLevel(texture, 0, sampler, effectiveMagnify, u, v, r, g, b, a);
                return;
            }

            if (FilterSelectsMipWithPoint(sampler.filter))
            {
                // GL's own rule for a *_MIPMAP_NEAREST filter: d = ceil(lambda + 0.5) - 1, i.e. the
                // nearest level with a tie resolved DOWNWARD. At an exact integer lambda that is
                // that level itself, which is what makes the contract fixture's Point checks exact.
                int level = static_cast<int>(std::ceil(clamped + 0.5f)) - 1;
                level = std::clamp(level, 0, levels - 1);
                SampleLevel(texture, level, sampler, false, u, v, r, g, b, a);
                return;
            }

            // Mip LINEAR: the two bracketing levels, weighted by the fractional part. At an exact
            // integer lambda the upper weight is 0, so this degenerates to exactly one level and
            // costs one within-level sample rather than two.
            const int lo = std::clamp(static_cast<int>(std::floor(clamped)), 0, levels - 1);
            const float weight = clamped - static_cast<float>(lo);
            if (!(weight > 0.0f) || lo >= levels - 1)
            {
                SampleLevel(texture, lo, sampler, false, u, v, r, g, b, a);
                return;
            }

            float r1 = 0.0f, g1 = 0.0f, b1 = 0.0f, a1 = 0.0f;
            SampleLevel(texture, lo, sampler, false, u, v, r, g, b, a);
            SampleLevel(texture, lo + 1, sampler, false, u, v, r1, g1, b1, a1);
            r += (r1 - r) * weight;
            g += (g1 - g) * weight;
            b += (b1 - b) * weight;
            a += (a1 - a) * weight;
        }

        /// SOFTWARE-117: deterministic CPU anisotropic filtering. The ordinary isotropic path
        /// selects LOD from the longest footprint axis, which blurs an oblique surface in both
        /// directions. Anisotropic filtering instead samples along that major axis while selecting
        /// LOD from the minor axis. When the requested ratio exceeds the renderer's 16x work cap,
        /// the minor rate is raised just enough to keep the remaining ratio representable; this is
        /// the same quality/work trade-off exposed by a finite GPU anisotropy limit.
        void SampleTexture(const SoftwareColorSurface& texture, const SoftwareSamplerState& sampler,
                           bool magnify, float lambda, float u, float v,
                           float& r, float& g, float& b, float& a,
                           const TextureFootprint* footprint = nullptr)
        {
            constexpr int kMaxCpuAnisotropy = 16;
            // Microsoft XNA converts MaxAnisotropy to UInt32 before applying the device cap.
            // Consequently a negative public Int32 value wraps high and selects the cap, while
            // zero remains the effectively-isotropic zero accepted by the D3D state path.
            const auto requestedAnisotropy =
                static_cast<std::uint32_t>(sampler.maxAnisotropy);
            const int maxAnisotropy = static_cast<int>(std::min(
                requestedAnisotropy, static_cast<std::uint32_t>(kMaxCpuAnisotropy)));
            if (sampler.filter != 2 || maxAnisotropy <= 1 || footprint == nullptr ||
                !(footprint->majorRate > 1.0f))
            {
                SampleTextureIsotropic(texture, sampler, magnify, lambda, u, v, r, g, b, a);
                return;
            }

            const float majorRate = std::max(1.0f, footprint->majorRate);
            const float minorRate = std::max(1.0f, footprint->minorRate);
            const float filteredMinorRate =
                std::max(minorRate, majorRate / static_cast<float>(maxAnisotropy));
            const int tapCount = std::clamp(
                static_cast<int>(std::ceil(majorRate / filteredMinorRate)), 1, maxAnisotropy);
            if (tapCount <= 1)
            {
                SampleTextureIsotropic(texture, sampler, false,
                                       std::log2(filteredMinorRate), u, v, r, g, b, a);
                return;
            }

            const float tapLambda = std::log2(filteredMinorRate);
            float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f, sumA = 0.0f;
            for (int tap = 0; tap < tapCount; ++tap)
            {
                const float position =
                    (static_cast<float>(tap) + 0.5f) / static_cast<float>(tapCount) - 0.5f;
                float tapR = 0.0f, tapG = 0.0f, tapB = 0.0f, tapA = 0.0f;
                SampleTextureIsotropic(texture, sampler, false, tapLambda,
                                       u + footprint->majorU * position,
                                       v + footprint->majorV * position,
                                       tapR, tapG, tapB, tapA);
                sumR += tapR;
                sumG += tapG;
                sumB += tapB;
                sumA += tapA;
            }
            const float inverseTapCount = 1.0f / static_cast<float>(tapCount);
            r = sumR * inverseTapCount;
            g = sumG * inverseTapCount;
            b = sumB * inverseTapCount;
            a = sumA * inverseTapCount;
        }

        /// REMED-GFX-150/SOFTWARE-117: the texture-space footprint of one destination pixel.
        ///
        /// XNA's TextureFilter names a SEPARATE minification and magnification filter for ordinals
        /// 5..8 (MinLinearMagPoint*, MinPointMagLinear*); the other five use one filter for both, so
        /// this classification is ignored for them and cannot perturb Point or Linear.
        ///
        /// The two rates are the singular values of the standard screen-to-texel Jacobian,
        /// evaluated once per triangle: exact for affine SpriteBatch quads and a stable
        /// per-triangle estimate for perspective geometry. A separate maximum-column rate preserves
        /// the historical isotropic LOD/magnification decision; the principal rates and major-axis
        /// direction are read only by TextureFilter::Anisotropic.
        /// REMED-GFX-182: the screen-space part of the rate, shared by the 2D and cube paths so
        /// there is exactly one footprint formula on this renderer. @p s0..@p t2 are the source
        /// coordinates in TEXELS at the triangle's three vertices; how they were obtained -- a UV
        /// attribute for an ordinary texture, a reflection vector projected onto a cube face for the
        /// environment map -- is the caller's business and is the only thing that differs.
        TextureFootprint ScreenSpaceTextureFootprint(
            const RasterVertex& v0, const RasterVertex& v1, const RasterVertex& v2,
            float s0, float s1, float s2, float t0, float t1, float t2,
            int texW, int texH)
        {
            const float area2 = (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
            if (!(std::abs(area2) > 1e-12f)) return TextureFootprint{};

            const float dsdx = ((s1 - s0) * (v2.y - v0.y) - (s2 - s0) * (v1.y - v0.y)) / area2;
            const float dsdy = ((s2 - s0) * (v1.x - v0.x) - (s1 - s0) * (v2.x - v0.x)) / area2;
            const float dtdx = ((t1 - t0) * (v2.y - v0.y) - (t2 - t0) * (v1.y - v0.y)) / area2;
            const float dtdy = ((t2 - t0) * (v1.x - v0.x) - (t1 - t0) * (v2.x - v0.x)) / area2;
            const float isotropicRate = std::max(
                std::sqrt(dsdx * dsdx + dtdx * dtdx),
                std::sqrt(dsdy * dsdy + dtdy * dtdy));

            // J*transpose(J) is a symmetric 2x2 matrix in texture space. Its eigenvalues are the
            // squared principal-axis rates; its major eigenvector gives the tap direction.
            const float aa = dsdx * dsdx + dsdy * dsdy;
            const float bb = dsdx * dtdx + dsdy * dtdy;
            const float cc = dtdx * dtdx + dtdy * dtdy;
            const float trace = aa + cc;
            const float discriminant = std::sqrt(std::max(0.0f,
                (aa - cc) * (aa - cc) + 4.0f * bb * bb));
            const float majorSquared = 0.5f * (trace + discriminant);
            const float minorSquared = std::max(0.0f, 0.5f * (trace - discriminant));
            const float majorRate = std::sqrt(std::max(0.0f, majorSquared));
            const float minorRate = std::sqrt(minorSquared);
            if (!std::isfinite(majorRate) || !std::isfinite(minorRate))
                return TextureFootprint{};

            float axisS = 1.0f;
            float axisT = 0.0f;
            if (std::abs(bb) > 1e-12f)
            {
                axisS = bb;
                axisT = majorSquared - aa;
                const float axisLength = std::sqrt(axisS * axisS + axisT * axisT);
                if (axisLength > 0.0f)
                {
                    axisS /= axisLength;
                    axisT /= axisLength;
                }
            }
            else if (cc > aa)
            {
                axisS = 0.0f;
                axisT = 1.0f;
            }

            TextureFootprint result;
            result.isotropicRate = isotropicRate > 1.0f ? isotropicRate : 1.0f;
            result.majorRate = majorRate > 1.0f ? majorRate : 1.0f;
            result.minorRate = minorRate;
            result.majorU = axisS * majorRate / static_cast<float>(std::max(1, texW));
            result.majorV = axisT * majorRate / static_cast<float>(std::max(1, texH));
            return result;
        }

        TextureFootprint TriangleTextureFootprint(
            const RasterVertex& v0, const RasterVertex& v1, const RasterVertex& v2,
            int texW, int texH, bool secondCoordinate = false)
        {
            const float w0 = (v0.invW != 0.0f) ? v0.invW : 1.0f;
            const float w1 = (v1.invW != 0.0f) ? v1.invW : 1.0f;
            const float w2 = (v2.invW != 0.0f) ? v2.invW : 1.0f;
            const float u0 = secondCoordinate ? v0.u1 : v0.u;
            const float u1 = secondCoordinate ? v1.u1 : v1.u;
            const float u2 = secondCoordinate ? v2.u1 : v2.u;
            const float t0 = secondCoordinate ? v0.v1 : v0.v;
            const float t1 = secondCoordinate ? v1.v1 : v1.v;
            const float t2 = secondCoordinate ? v2.v1 : v2.v;
            return ScreenSpaceTextureFootprint(
                v0, v1, v2,
                u0 / w0 * static_cast<float>(texW),
                u1 / w1 * static_cast<float>(texW),
                u2 / w2 * static_cast<float>(texW),
                t0 / w0 * static_cast<float>(texH),
                t1 / w1 * static_cast<float>(texH),
                t2 / w2 * static_cast<float>(texH), texW, texH);
        }

        /// REMED-GFX-175: the level-of-detail a texel rate implies. rho <= 1 is magnification, which
        /// this reports as 0 -- there is no level above level 0 to select towards.
        float LodFromTexelRate(float rho)
        {
            if (!(rho > 1.0f)) return 0.0f;
            const float lambda = std::log2(rho);
            return (lambda > 0.0f) ? lambda : 0.0f;
        }

#ifndef CNA_SOFTWARE_2D_ONLY
        /// SOFTWARE-82: applies a column-major 4x4 matrix (GpuDrawParams::worldColMajor's own
        /// layout, and SkinnedEffect's boneTransforms per-bone entries) to a vector using the
        /// standard column-vector convention `v' = M*v` -- deliberately NOT going through CNA's
        /// own Matrix type (which is row-major/row-vector), to avoid a transpose round-trip for
        /// data that already arrives in exactly this flat, column-major layout. `w=1` applies
        /// translation (for a position); `w=0` ignores it (for a direction/normal).
        Vector3 ApplyAffineColumnMajor(const float* m, const Vector3& v, float w)
        {
            return Vector3(
                m[0] * v.X + m[4] * v.Y + m[8]  * v.Z + m[12] * w,
                m[1] * v.X + m[5] * v.Y + m[9]  * v.Z + m[13] * w,
                m[2] * v.X + m[6] * v.Y + m[10] * v.Z + m[14] * w);
        }

        [[nodiscard]] Vector3 NormalizeOrZero(const Vector3& value)
        {
            const float lengthSquared = value.X * value.X + value.Y * value.Y +
                                        value.Z * value.Z;
            if (!(lengthSquared > 0.0f))
                return Vector3::Zero;
            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            return Vector3(value.X * inverseLength, value.Y * inverseLength,
                           value.Z * inverseLength);
        }

        /// SOFTWARE-113: transforms a normal with transpose(inverse(World3x3)). The cofactor
        /// layout is the same column-major representation EasyGL uploads to its stock shaders.
        [[nodiscard]] Vector3 TransformWorldNormal(const float* world, const Vector3& normal)
        {
            const float a = world[0], d = world[1], g = world[2];
            const float b = world[4], e = world[5], h = world[6];
            const float c = world[8], f = world[9], i = world[10];
            const float determinant =
                a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
            const float inverseDeterminant = determinant != 0.0f ? 1.0f / determinant : 0.0f;
            const float normalMatrix[9] = {
                (e * i - f * h) * inverseDeterminant,
                -(b * i - c * h) * inverseDeterminant,
                (b * f - c * e) * inverseDeterminant,
                -(d * i - f * g) * inverseDeterminant,
                (a * i - c * g) * inverseDeterminant,
                -(a * f - c * d) * inverseDeterminant,
                (d * h - e * g) * inverseDeterminant,
                -(a * h - b * g) * inverseDeterminant,
                (a * e - b * d) * inverseDeterminant,
            };
            return NormalizeOrZero(Vector3(
                normalMatrix[0] * normal.X + normalMatrix[3] * normal.Y +
                    normalMatrix[6] * normal.Z,
                normalMatrix[1] * normal.X + normalMatrix[4] * normal.Y +
                    normalMatrix[7] * normal.Z,
                normalMatrix[2] * normal.X + normalMatrix[5] * normal.Y +
                    normalMatrix[8] * normal.Z));
        }

        [[nodiscard]] bool UsesClassicEffectLighting(const GpuDrawParams& params)
        {
            return params.lightingEnabled && !params.envMapping && !params.pbr;
        }

        /// SOFTWARE-153: BasicEffect, AlphaTestEffect and DualTextureEffect all route their
        /// material colour through Common.fxh's COLOR0 vertex output. D3D9 saturates that semantic
        /// before interpolation. Compiled/custom effects own their own output rules; the lit,
        /// environment, skinned and PBR families have separate preparation paths below.
        [[nodiscard]] bool UsesUnlitCommonDiffuseOutput(const GpuDrawParams& params)
        {
            return !params.lightingEnabled && !params.envMapping && !params.skinned && !params.pbr &&
                   !params.customEffectRequested && params.customEffectRenderer == nullptr &&
                   params.compiledEffectRuntime == nullptr;
        }

        void PrepareUnlitCommonDiffuseVertex(ClipVertex& vertex, const GpuDrawParams& params)
        {
            if (!UsesUnlitCommonDiffuseOutput(params))
                return;
            vertex.r = std::clamp(vertex.r * params.diffuseColor[0], 0.0f, 1.0f);
            vertex.g = std::clamp(vertex.g * params.diffuseColor[1], 0.0f, 1.0f);
            vertex.b = std::clamp(vertex.b * params.diffuseColor[2], 0.0f, 1.0f);
            vertex.a = std::clamp(vertex.a * params.diffuseColor[3], 0.0f, 1.0f);
        }

        struct ClassicLightingResult
        {
            float diffuse[3] = {0.0f, 0.0f, 0.0f};
            float specular[3] = {0.0f, 0.0f, 0.0f};
        };

        /// SOFTWARE-113/114/115: FNA StockEffects/HLSL/Lighting.fxh ComputeLights. Effect-specific
        /// FillGpuDrawParams prepares ambient/emissive consistently before this shared equation.
        [[nodiscard]] ClassicLightingResult ComputeClassicLighting(
            const Vector3& worldPosition, const Vector3& worldNormal,
            const GpuDrawParams& params)
        {
            const Vector3 eye = NormalizeOrZero(Vector3(
                params.eyePositionWorld[0] - worldPosition.X,
                params.eyePositionWorld[1] - worldPosition.Y,
                params.eyePositionWorld[2] - worldPosition.Z));
            const float* directions[3] = {
                params.light0Dir, params.light1Dir, params.light2Dir};
            const float* diffuseColors[3] = {
                params.light0Diffuse, params.light1Diffuse, params.light2Diffuse};
            const float* specularColors[3] = {
                params.light0Specular, params.light1Specular, params.light2Specular};

            float diffuseSum[3] = {params.ambientColor[0], params.ambientColor[1],
                                   params.ambientColor[2]};
            float specularSum[3] = {0.0f, 0.0f, 0.0f};
            for (int light = 0; light < 3; ++light)
            {
                const float dotLight = -(directions[light][0] * worldNormal.X +
                                         directions[light][1] * worldNormal.Y +
                                         directions[light][2] * worldNormal.Z);
                const float diffuseWeight = std::max(dotLight, 0.0f);
                const Vector3 halfVector = NormalizeOrZero(Vector3(
                    eye.X - directions[light][0], eye.Y - directions[light][1],
                    eye.Z - directions[light][2]));
                const float dotHalf = std::max(
                    halfVector.X * worldNormal.X + halfVector.Y * worldNormal.Y +
                        halfVector.Z * worldNormal.Z,
                    0.0f);
                const float specularWeight = dotLight >= 0.0f
                    ? std::pow(dotHalf, params.specularPower)
                    : 0.0f;
                for (int channel = 0; channel < 3; ++channel)
                {
                    diffuseSum[channel] +=
                        diffuseWeight * diffuseColors[light][channel];
                    specularSum[channel] +=
                        specularWeight * specularColors[light][channel];
                }
            }

            ClassicLightingResult result;
            for (int channel = 0; channel < 3; ++channel)
            {
                result.diffuse[channel] =
                    diffuseSum[channel] * params.diffuseColor[channel] +
                    params.emissiveColor[channel];
                result.specular[channel] =
                    specularSum[channel] * params.specularColor[channel];
            }
            return result;
        }

        void PrepareClassicLightingVertex(ClipVertex& vertex, const Vector3& position,
                                          const Vector3& normal, bool haveNormal,
                                          const GpuDrawParams& params)
        {
            if (!UsesClassicEffectLighting(params))
                return;

            // SkinnedEffect's caller has already applied its weighted bone matrix to both inputs,
            // matching FNA Skin() before this shared World-space lighting step.
            const Vector3 worldPosition =
                ApplyAffineColumnMajor(params.worldColMajor, position, 1.0f);
            const Vector3 worldNormal = haveNormal
                ? TransformWorldNormal(params.worldColMajor, normal)
                : Vector3::Zero;
            vertex.wpx = worldPosition.X;
            vertex.wpy = worldPosition.Y;
            vertex.wpz = worldPosition.Z;
            vertex.nx = worldNormal.X;
            vertex.ny = worldNormal.Y;
            vertex.nz = worldNormal.Z;

            if (params.preferPerPixelLighting)
                return;

            const ClassicLightingResult lighting =
                ComputeClassicLighting(worldPosition, worldNormal, params);
            // D3D9/XNA saturates COLOR0/COLOR1 vertex outputs before interpolation.
            vertex.r = std::clamp(vertex.r * lighting.diffuse[0], 0.0f, 1.0f);
            vertex.g = std::clamp(vertex.g * lighting.diffuse[1], 0.0f, 1.0f);
            vertex.b = std::clamp(vertex.b * lighting.diffuse[2], 0.0f, 1.0f);
            vertex.a = std::clamp(vertex.a * params.diffuseColor[3], 0.0f, 1.0f);
            vertex.sr = std::clamp(lighting.specular[0], 0.0f, 1.0f);
            vertex.sg = std::clamp(lighting.specular[1], 0.0f, 1.0f);
            vertex.sb = std::clamp(lighting.specular[2], 0.0f, 1.0f);
        }

        /// SOFTWARE-114: EnvironmentMapEffect performs all lighting, reflection and Fresnel work
        /// in its vertex shader. The pixel shader only samples with the interpolated reflection
        /// direction and blends by the interpolated scalar, so these values must be prepared
        /// before clipping rather than reconstructed per fragment.
        void PrepareEnvironmentMapVertex(ClipVertex& vertex, const Vector3& position,
                                         const Vector3& normal, bool haveNormal,
                                         const GpuDrawParams& params)
        {
            if (!params.envMapping)
                return;

            const Vector3 worldPosition =
                ApplyAffineColumnMajor(params.worldColMajor, position, 1.0f);
            const Vector3 worldNormal = haveNormal
                ? TransformWorldNormal(params.worldColMajor, normal)
                : Vector3::Zero;
            const Vector3 eye = NormalizeOrZero(Vector3(
                params.eyePositionWorld[0] - worldPosition.X,
                params.eyePositionWorld[1] - worldPosition.Y,
                params.eyePositionWorld[2] - worldPosition.Z));
            const float normalDotEye = worldNormal.X * eye.X + worldNormal.Y * eye.Y +
                                       worldNormal.Z * eye.Z;
            vertex.envx = 2.0f * normalDotEye * worldNormal.X - eye.X;
            vertex.envy = 2.0f * normalDotEye * worldNormal.Y - eye.Y;
            vertex.envz = 2.0f * normalDotEye * worldNormal.Z - eye.Z;
            vertex.envBlend = std::clamp(params.fresnelEnabled
                ? std::pow(std::max(1.0f - std::abs(normalDotEye), 0.0f),
                           params.fresnelFactor) * params.envMapAmount
                : params.envMapAmount, 0.0f, 1.0f);

            const ClassicLightingResult lighting =
                ComputeClassicLighting(worldPosition, worldNormal, params);
            vertex.r = std::clamp(lighting.diffuse[0], 0.0f, 1.0f);
            vertex.g = std::clamp(lighting.diffuse[1], 0.0f, 1.0f);
            vertex.b = std::clamp(lighting.diffuse[2], 0.0f, 1.0f);
            vertex.a = std::clamp(params.diffuseColor[3], 0.0f, 1.0f);
        }

        /// SOFTWARE-112: the five classic XNA stock effects compute this vertex output from the
        /// object-space position after skinning. CNAEXT PBR remains outside this campaign and
        /// deliberately retains Software's previous no-fog behavior.
        float ComputeClassicFogKeep(const Vector3& position, const GpuDrawParams& params)
        {
            if (params.pbr)
                return 1.0f;
            return 1.0f - std::clamp(
                position.X * params.fogVector[0] + position.Y * params.fogVector[1] +
                position.Z * params.fogVector[2] + params.fogVector[3], 0.0f, 1.0f);
        }

        /// SOFTWARE-82: this renderer's cube-map addressing convention, in ONE place.
        ///
        /// Projects @p dir onto the named face using the classic OpenGL/D3D per-face UV formula and
        /// reports the face-local coordinate in [0,1]. Returns false when @p dir is not in that
        /// face's hemisphere (the projection would divide by a non-positive major axis), so a caller
        /// that names the face itself -- REMED-GFX-182's per-triangle LOD estimate -- can reject a
        /// vertex instead of producing a nonsense coordinate.
        ///
        /// REMED-GFX-182 factored this out of SampleCubeMap unchanged: face 0/1 are +X/-X with
        /// u = -+Z and v = -Y, face 2/3 are +Y/-Y with u = X and v = +-Z, face 4/5 are +Z/-Z with
        /// u = +-X and v = -Y. The convention itself is settled elsewhere (REMED-GFX-134) and is not
        /// touched here.
        bool CubeFaceLocal(const Vector3& dir, int face, float& s, float& t)
        {
            float u, v, ma;
            switch (face)
            {
                case 0:  ma =  dir.X; u = -dir.Z; v = -dir.Y; break;   // PositiveX
                case 1:  ma = -dir.X; u =  dir.Z; v = -dir.Y; break;   // NegativeX
                case 2:  ma =  dir.Y; u =  dir.X; v =  dir.Z; break;   // PositiveY
                case 3:  ma = -dir.Y; u =  dir.X; v = -dir.Z; break;   // NegativeY
                case 4:  ma =  dir.Z; u =  dir.X; v = -dir.Y; break;   // PositiveZ
                default: ma = -dir.Z; u = -dir.X; v = -dir.Y; break;   // NegativeZ
            }
            if (!(ma > 0.0f)) return false;
            s = std::clamp((u / ma + 1.0f) * 0.5f, 0.0f, 1.0f);
            t = std::clamp((v / ma + 1.0f) * 0.5f, 0.0f, 1.0f);
            return true;
        }

        /// SOFTWARE-82: standard cube-map face selection -- the largest-magnitude axis picks the
        /// face and its sign picks Positive/Negative -- followed by CubeFaceLocal's per-face
        /// projection. A degenerate (all-zero) direction has no major axis to select and no
        /// projection to make, so it resolves to the face centre: deterministic, and inside the
        /// face, which is the same stance the rest of this renderer's validation takes.
        void SelectCubeFace(const Vector3& dir, int& face, float& s, float& t)
        {
            const float ax = std::abs(dir.X), ay = std::abs(dir.Y), az = std::abs(dir.Z);
            if (ax >= ay && ax >= az)      face = dir.X > 0.0f ? 0 : 1;
            else if (ay >= ax && ay >= az) face = dir.Y > 0.0f ? 2 : 3;
            else                           face = dir.Z > 0.0f ? 4 : 5;
            if (!CubeFaceLocal(dir, face, s, t)) { s = 0.5f; t = 0.5f; }
        }

        /// REMED-GFX-182: one face of a cube, presented as the colour surface REMED-GFX-124
        /// introduced, so the authoritative REMED-GFX-150/REMED-GFX-175 sampler filters it.
        ///
        /// This is the whole architecture of the fix: the cube path gets no filter code, no ordinal
        /// table and no address logic of its own -- it selects a face, converts the direction to a
        /// face-local coordinate, and then hands BOTH to `SampleTexture`, the same function every
        /// ordinary texture goes through. Min/mag selection, mip level selection, level combination,
        /// texel addressing and decode are therefore identical for a cube and a Texture2D by
        /// construction, and cannot drift apart later.
        ///
        /// Stack-constructed per cube sample: it owns nothing, copies nothing and allocates nothing;
        /// the level storage stays the cube's.
        class CubeFaceSurface final : public SoftwareColorSurface
        {
        public:
            CubeFaceSurface(const SoftwareCubeSurface& cube, int face)
                : cube_(cube), face_(face) {}

            [[nodiscard]] int ColorWidth() const override { return std::max(1, cube_.CubeSize()); }
            [[nodiscard]] int ColorHeight() const override { return std::max(1, cube_.CubeSize()); }
            [[nodiscard]] const std::vector<std::uint8_t>& ColorPixels() const override
            { return cube_.CubeFacePixels(face_, 0); }

            [[nodiscard]] int ColorLevelCount() const override
            { return cube_.CubeFaceLevelCount(face_); }
            [[nodiscard]] int ColorWidth(int level) const override
            { return cube_.CubeFaceDimension(level); }
            [[nodiscard]] int ColorHeight(int level) const override
            { return cube_.CubeFaceDimension(level); }
            [[nodiscard]] const std::vector<std::uint8_t>& ColorPixels(int level) const override
            { return cube_.CubeFacePixels(face_, level); }
            void FetchColorTexel(int level, int x, int y,
                                 float& r, float& g, float& b, float& a) const override
            { cube_.FetchCubeColorTexel(face_, level, x, y, r, g, b, a); }

        private:
            const SoftwareCubeSurface& cube_;
            int face_;
        };

        /// REMED-GFX-182: the ADDRESS half of a cube sampler.
        ///
        /// A cube map is addressed by a DIRECTION: face selection consumes the direction and the
        /// face-local coordinate that comes out is already in [0,1], so U/V/W address modes are
        /// unreachable except exactly at a face edge. Every GPU renderer in this project measures
        /// Clamp, Wrap and Mirror as rendering a cube IDENTICALLY (REMED-GFX-173 and REMED-GFX-181
        /// both report 0 differing pixels for each pair), because cube filtering is seamless there
        /// and the modes never apply. This renderer does not implement cross-face filtering -- an
        /// explicit, long-standing simplification -- so the honest match to that measured behaviour
        /// is to address face-locally with CLAMP, which keeps an edge tap on the face's own last
        /// texel instead of teleporting it to the opposite edge under Wrap. No cube address-mode
        /// convention is invented here and none is asserted; the mode the game set is carried in the
        /// trace so the boundary stays visible.
        SoftwareSamplerState CubeSamplerFor(const SoftwareSamplerState& slot)
        {
            SoftwareSamplerState s = slot;   // the FILTER, all three components, exactly as set
            s.addressU = 1;                  // Clamp
            s.addressV = 1;                  // Clamp
            return s;
        }

        /// SOFTWARE-82 / REMED-GFX-182: one environment-map cube sample under the public
        /// `GraphicsDevice.SamplerStates[1]` this draw captured.
        ///
        /// The stages are kept strictly separate and in this order, and only stages 3-6 are new:
        ///   1. select the face from the direction    (SelectCubeFace, unchanged convention)
        ///   2. convert the direction to face-local u/v (CubeFaceLocal, unchanged formula)
        ///   3. select the mip level(s) from the MIPMAP component of the filter and @p lambda
        ///   4. filter within each participating level by the MIN/MAG component
        ///   5. combine levels where mip-linear applies
        ///   6. decode
        /// Stages 3-6 are not implemented here at all: they are `SampleTexture`, reached through a
        /// CubeFaceSurface view of the selected face.
        ///
        /// The pre-fix signature took no sampler and ended in a single clamped
        /// `static_cast<int>(s * size)` fetch at level 0, so the cube was point-sampled at level 0
        /// however `SamplerStates[1]` was set. The parameter list now mirrors `SampleTexture`'s
        /// exactly -- sampler, magnification classification, lambda -- because it does the same job.
        void SampleCubeMap(const SoftwareCubeSurface& cube, const SoftwareSamplerState& sampler,
                          bool magnify, float lambda, const Vector3& dir,
                          float& r, float& g, float& b, float& a,
                          const TextureFootprint* footprint = nullptr)
        {
            int face = 0;
            float s = 0.5f, t = 0.5f;
            SelectCubeFace(dir, face, s, t);

            const CubeFaceSurface surface(cube, face);
            const SoftwareSamplerState cubeSampler = CubeSamplerFor(sampler);

            const long long fetchesBefore = g_samplerTrace.texelFetches;
            SampleTexture(surface, cubeSampler, magnify, lambda, s, t, r, g, b, a, footprint);

            if (g_cubeTrace.enabled)
            {
                // Re-derive the level selection for the trace from the SAME helpers the sample just
                // used, so the reported stages cannot describe a different decision than the one
                // that produced the pixel.
                const int levels = std::max(1, surface.ColorLevelCount());
                const bool mipPoint = FilterSelectsMipWithPoint(cubeSampler.filter);
                const bool withinPoint = magnify ? FilterMagnifiesWithPoint(cubeSampler.filter)
                                                 : FilterMinifiesWithPoint(cubeSampler.filter);
                int lo = 0, hi = 0;
                float weight = 0.0f;
                if (levels > 1 && !magnify && lambda > 0.0f)
                {
                    const float clamped = std::min(lambda, static_cast<float>(levels - 1));
                    if (mipPoint)
                    {
                        lo = std::clamp(static_cast<int>(std::ceil(clamped + 0.5f)) - 1, 0, levels - 1);
                        hi = lo;
                    }
                    else
                    {
                        lo = std::clamp(static_cast<int>(std::floor(clamped)), 0, levels - 1);
                        weight = clamped - static_cast<float>(lo);
                        hi = (!(weight > 0.0f) || lo >= levels - 1) ? lo : lo + 1;
                        if (hi == lo) weight = 0.0f;
                    }
                }
                const int dim = std::max(1, surface.ColorWidth(lo));

                ++g_cubeTrace.cubeSamples;
                g_cubeTrace.cubeLevelSamples += (hi == lo) ? 1 : 2;
                // MEASURED at the fetch site, not derived from the level decision above.
                g_cubeTrace.cubeTexelFetches += g_samplerTrace.texelFetches - fetchesBefore;
                g_cubeTrace.capturedKeys.insert(SamplerDescKey(g_cubeTrace.slot1Filter,
                                                               g_cubeTrace.slot1AddrU,
                                                               g_cubeTrace.slot1AddrV));
                g_cubeTrace.effectiveKeys.insert(
                    SamplerDescKey(withinPoint ? 1 : 0, cubeSampler.addressU, cubeSampler.addressV) |
                    (static_cast<std::uint32_t>(mipPoint ? 1 : 0) << 24) |
                    (static_cast<std::uint32_t>(lo & 0x7) << 28));
                g_cubeTrace.dirX = dir.X; g_cubeTrace.dirY = dir.Y; g_cubeTrace.dirZ = dir.Z;
                g_cubeTrace.face = face;
                g_cubeTrace.faceU = s; g_cubeTrace.faceV = t;
                g_cubeTrace.levelCount = levels;
                g_cubeTrace.lambda = lambda;
                g_cubeTrace.lowLevel = lo; g_cubeTrace.highLevel = hi; g_cubeTrace.mipWeight = weight;
                g_cubeTrace.mipPoint = mipPoint;
                g_cubeTrace.magnify = magnify;
                g_cubeTrace.withinPoint = withinPoint;
                g_cubeTrace.texelX = std::clamp(static_cast<int>(s * static_cast<float>(dim)), 0, dim - 1);
                g_cubeTrace.texelY = std::clamp(static_cast<int>(t * static_cast<float>(dim)), 0, dim - 1);
                g_cubeTrace.fetchCount =
                    static_cast<int>(g_samplerTrace.texelFetches - fetchesBefore);
                g_cubeTrace.addressU = cubeSampler.addressU;
                g_cubeTrace.addressV = cubeSampler.addressV;
                g_cubeTrace.sampleR = r; g_cubeTrace.sampleG = g;
                g_cubeTrace.sampleB = b; g_cubeTrace.sampleA = a;
            }
        }

        /// REMED-GFX-182: this triangle's cube texel rate -- the footprint the MIPMAP component of
        /// `SamplerStates[1]` turns into a level, resolved ONCE per triangle exactly like the 2D
        /// rate REMED-GFX-175 established, with no per-fragment derivative work.
        ///
        /// The already-prepared EnvironmentMapEffect reflection direction is read at the three
        /// vertices, the face is chosen from their SUM (the triangle's dominant direction,
        /// so all three project onto one face), and the three face-local coordinates in texels then
        /// go through the shared ScreenSpaceTexelRate. A vertex that does not lie in the chosen
        /// face's hemisphere, or a triangle whose normals or eye vector degenerate, reports a rate
        /// of 1 -- magnification, level 0 -- which is the same "deterministic rather than clever"
        /// fallback a degenerate 2D triangle already gets.
        TextureFootprint TriangleCubeTextureFootprint(
            const RasterVertex& v0, const RasterVertex& v1,
            const RasterVertex& v2, int faceDim)
        {
            const RasterVertex* verts[3] = {&v0, &v1, &v2};
            Vector3 dirs[3];
            Vector3 sum(0.0f, 0.0f, 0.0f);
            for (int k = 0; k < 3; ++k)
            {
                const RasterVertex& rv = *verts[k];
                const float invW = (rv.invW != 0.0f) ? rv.invW : 1.0f;
                dirs[k] = Vector3(rv.envx / invW, rv.envy / invW, rv.envz / invW);
                const float directionLengthSquared =
                    dirs[k].X * dirs[k].X + dirs[k].Y * dirs[k].Y +
                    dirs[k].Z * dirs[k].Z;
                if (!(directionLengthSquared > 1e-12f)) return TextureFootprint{};
                sum = Vector3(sum.X + dirs[k].X, sum.Y + dirs[k].Y, sum.Z + dirs[k].Z);
            }

            int face = 0;
            float cs = 0.5f, ct = 0.5f;
            SelectCubeFace(sum, face, cs, ct);

            float s[3], t[3];
            for (int k = 0; k < 3; ++k)
                if (!CubeFaceLocal(dirs[k], face, s[k], t[k])) return TextureFootprint{};

            const float dim = static_cast<float>(std::max(1, faceDim));
            return ScreenSpaceTextureFootprint(
                v0, v1, v2,
                s[0] * dim, s[1] * dim, s[2] * dim,
                t[0] * dim, t[1] * dim, t[2] * dim, faceDim, faceDim);
        }

        /// REMED-GFX-182: one complete cube-sample line, emitted once the effect contribution is
        /// known. Every stage of the operation is on it, so a disputed pixel can be classified
        /// without a debugger: which slot state was captured, which face and face-local coordinate
        /// the reflection selected, which level(s) the MIPMAP component chose, which filter ran
        /// inside them, how many texels were fetched, and what the effect finally wrote.
        void PrintCubeTraceLine(float outR, float outG, float outB)
        {
            if (!g_cubeTrace.verbose || g_cubeTrace.printed >= g_cubeTrace.limit) return;
            if (g_cubeTrace.drawFilter != 0 && g_cubeTrace.drawId != g_cubeTrace.drawFilter) return;
            ++g_cubeTrace.printed;
            std::fprintf(stderr,
                         "[cube] draw=%lld family=%s dest=(%d,%d) "
                         "slot0=(filter=%d,%s,%s) slot1=(filter=%d,%s,%s) "
                         "eff=(within=%s,addr=%s/%s,mip=%s) "
                         "dir=(%.6f,%.6f,%.6f) face=%d st=(%.6f,%.6f) levels=%d lambda=%.6f "
                         "mip=[%d,%d] w=%.6f magnify=%d texel=(%d,%d) fetches=%d "
                         "sample=(%d,%d,%d,%d) out=(%d,%d,%d)\n",
                         g_cubeTrace.drawId, g_cubeTrace.family,
                         g_samplerTrace.fragX, g_samplerTrace.fragY,
                         g_cubeTrace.slot0Filter, AddressName(g_cubeTrace.slot0AddrU),
                         AddressName(g_cubeTrace.slot0AddrV),
                         g_cubeTrace.slot1Filter, AddressName(g_cubeTrace.slot1AddrU),
                         AddressName(g_cubeTrace.slot1AddrV),
                         g_cubeTrace.withinPoint ? "Point" : "Linear",
                         AddressName(g_cubeTrace.addressU), AddressName(g_cubeTrace.addressV),
                         g_cubeTrace.mipPoint ? "Point" : "Linear",
                         static_cast<double>(g_cubeTrace.dirX), static_cast<double>(g_cubeTrace.dirY),
                         static_cast<double>(g_cubeTrace.dirZ),
                         g_cubeTrace.face,
                         static_cast<double>(g_cubeTrace.faceU), static_cast<double>(g_cubeTrace.faceV),
                         g_cubeTrace.levelCount, static_cast<double>(g_cubeTrace.lambda),
                         g_cubeTrace.lowLevel, g_cubeTrace.highLevel,
                         static_cast<double>(g_cubeTrace.mipWeight),
                         g_cubeTrace.magnify ? 1 : 0,
                         g_cubeTrace.texelX, g_cubeTrace.texelY, g_cubeTrace.fetchCount,
                         static_cast<int>(g_cubeTrace.sampleR * 255.0f + 0.5f),
                         static_cast<int>(g_cubeTrace.sampleG * 255.0f + 0.5f),
                         static_cast<int>(g_cubeTrace.sampleB * 255.0f + 0.5f),
                         static_cast<int>(g_cubeTrace.sampleA * 255.0f + 0.5f),
                         // Truncated exactly as the framebuffer store below truncates, so the trace
                         // reports the byte that is really written and not a rounded approximation.
                         static_cast<int>(std::clamp(outR, 0.0f, 1.0f) * 255.0f),
                         static_cast<int>(std::clamp(outG, 0.0f, 1.0f) * 255.0f),
                         static_cast<int>(std::clamp(outB, 0.0f, 1.0f) * 255.0f));
        }
#endif

        /// SOFTWARE-81: whether a triangle with the given signed screen-space `area` should be
        /// culled under the given raw CullMode ordinal (0=None, 1=CullClockwiseFace,
        /// 2=CullCounterClockwiseFace). In this renderer's screen-space convention (Y grows
        /// downward, matching the framebuffer's own top-left-origin layout), a NEGATIVE signed
        /// area corresponds to clockwise winding as displayed and a POSITIVE area to
        /// counter-clockwise -- verified empirically via `Software_Culling` against real XNA/FNA's
        /// documented default (`RasterizerState.CullCounterClockwise`, which must keep the
        /// conventionally-front-facing, clockwise-as-displayed winding order visible).
        bool ShouldCullTriangle(float area, int cullMode)
        {
            if (cullMode == 0) return false;                    // None
            if (cullMode == 1) return area < 0.0f;               // CullClockwiseFace
            return area > 0.0f;                                  // CullCounterClockwiseFace (default)
        }

        /// REMED-GFX-082: bit flags selecting which of a triangle's three edges a WireFrame pass
        /// rasterizes. Edge bit 0 = v0->v1, bit 1 = v1->v2, bit 2 = v2->v0. A whole (unclipped)
        /// triangle draws all three; the 3D near-plane clipper masks the artificial fan diagonal of a
        /// clipped quad so only the real polygon boundary is outlined (see the draw entry points).
        /// SpriteBatch always draws all three edges of each of its two quad triangles, so a wireframe
        /// sprite shows its split diagonal -- real submitted geometry, matching D3D11/FNA.
        enum : unsigned { kEdgeV0V1 = 1u, kEdgeV1V2 = 2u, kEdgeV2V0 = 4u, kEdgeAll = 7u };

        /// SOFTWARE-107: D3D's top-left rule includes a sample on an exact boundary only when the
        /// boundary is the triangle's top or left edge. Normalize every triangle to clockwise
        /// screen-space edge direction first, so reversing the submitted winding changes culling
        /// but not coverage when CullMode::None keeps both orientations.
        [[nodiscard]] bool IsTopLeftEdge(float ax, float ay, float bx, float by, float area)
        {
            float dx = bx - ax;
            float dy = by - ay;
            if (area > 0.0f)
            {
                dx = -dx;
                dy = -dy;
            }
            return dy < 0.0f || (dy == 0.0f && dx > 0.0f);
        }

        [[nodiscard]] bool EdgeContainsSample(float value, float area,
                                              float ax, float ay, float bx, float by)
        {
            const float normalized = area > 0.0f ? value : -value;
            if (normalized > 0.0f)
                return true;
            if (normalized < 0.0f)
                return false;
            return IsTopLeftEdge(ax, ay, bx, by, area);
        }

        [[nodiscard]] bool TriangleContainsSample(
            const RasterVertex& v0, const RasterVertex& v1, const RasterVertex& v2,
            float area, float edgeV1V2, float edgeV2V0, float edgeV0V1)
        {
            return EdgeContainsSample(edgeV1V2, area, v1.x, v1.y, v2.x, v2.y) &&
                   EdgeContainsSample(edgeV2V0, area, v2.x, v2.y, v0.x, v0.y) &&
                   EdgeContainsSample(edgeV0V1, area, v0.x, v0.y, v1.x, v1.y);
        }

        /// Interpolates a triangle varying from two independent barycentric coordinates.  The
        /// affine-difference form deliberately preserves a constant input exactly: GPU
        /// interpolation cannot turn a flat-coloured triangle into adjacent 8-bit values merely
        /// because three separately divided weights sum to 0.99999994 on the CPU.
        [[nodiscard]] float BarycentricInterpolate(float a, float b, float c,
                                                   float lambda0, float lambda1)
        {
            return c + lambda0 * (a - c) + lambda1 * (b - c);
        }

        /// REMED-GFX-082: Liang-Barsky clip of the parametric segment P(t) = a + t*(b - a), t in
        /// [0,1], to the inclusive pixel rectangle [clip.minX,maxX] x [clip.minY,maxY]. Returns the
        /// surviving parameter range [t0,t1] (t0 <= t1) or false if the segment is entirely outside.
        /// Clipping the segment up front keeps a wire edge whose projected endpoints are far off-screen
        /// from spinning the DDA over a huge invisible span (Phase 26/27) and bounds the step count.
        bool ClipSegmentToRect(float ax, float ay, float bx, float by, const RasterClipRect& clip,
                               float& t0, float& t1)
        {
            t0 = 0.0f;
            t1 = 1.0f;
            const float dx = bx - ax, dy = by - ay;
            const float p[4] = {-dx, dx, -dy, dy};
            const float q[4] = {ax - static_cast<float>(clip.minX),
                                static_cast<float>(clip.maxX) - ax,
                                ay - static_cast<float>(clip.minY),
                                static_cast<float>(clip.maxY) - ay};
            for (int i = 0; i < 4; ++i)
            {
                if (p[i] == 0.0f)
                {
                    if (q[i] < 0.0f)
                        return false;  // parallel to this clip edge and entirely outside it
                }
                else
                {
                    const float t = q[i] / p[i];
                    if (p[i] < 0.0f) { if (t > t1) return false; if (t > t0) t0 = t; }
                    else             { if (t < t0) return false; if (t < t1) t1 = t; }
                }
            }
            return t0 <= t1;
        }

        /// SOFTWARE-344: applies the D3D/GDI aliased-line diamond-exit coverage test to one pixel.
        /// The CPU framebuffer uses corner-origin coordinates, so its pixel diamond is centred at
        /// `(x + 0.5, y + 0.5)` with four half-pixel diagonal planes. A directed line covers the
        /// pixel only when it exits that diamond before reaching its ending vertex; an endpoint
        /// which remains inside the diamond is the inclusive/exclusive line rule, not a fragment.
        /// `interpolationT` is evaluated at the pixel centre and remains relative to the original
        /// unclipped segment.
        bool SegmentExitsPixelDiamond(const RasterVertex& a, const RasterVertex& b,
                                      int x, int y, float& interpolationT)
        {
            if (!std::isfinite(a.x) || !std::isfinite(a.y) ||
                !std::isfinite(b.x) || !std::isfinite(b.y))
                return false;

            const double ax = static_cast<double>(a.x);
            const double ay = static_cast<double>(a.y);
            const double dx = static_cast<double>(b.x) - ax;
            const double dy = static_cast<double>(b.y) - ay;
            const double lengthSquared = dx * dx + dy * dy;
            if (!(lengthSquared > 0.0) || !std::isfinite(lengthSquared))
                return false;

            const double centerX = static_cast<double>(x) + 0.5;
            const double centerY = static_cast<double>(y) + 0.5;
            double enterT = 0.0;
            double exitT = 1.0;
            constexpr double normals[4][2] = {
                { 1.0,  1.0}, { 1.0, -1.0},
                {-1.0,  1.0}, {-1.0, -1.0},
            };
            for (const auto& normal : normals)
            {
                const double velocity = normal[0] * dx + normal[1] * dy;
                const double allowance = 0.5 -
                    (normal[0] * (ax - centerX) + normal[1] * (ay - centerY));
                if (velocity == 0.0)
                {
                    if (allowance < 0.0)
                        return false;
                    continue;
                }
                const double boundaryT = allowance / velocity;
                if (velocity > 0.0)
                    exitT = std::min(exitT, boundaryT);
                else
                    enterT = std::max(enterT, boundaryT);
                if (enterT > exitT)
                    return false;
            }

            // Reaching a diamond only at/after the ending vertex is not an exit by this line.
            if (exitT < 0.0 || enterT > 1.0 || !(exitT < 1.0))
                return false;

            interpolationT = static_cast<float>(std::clamp(
                ((centerX - ax) * dx + (centerY - ay) * dy) / lengthSquared,
                0.0, 1.0));
            return true;
        }

        /// REMED-GFX-082 / SOFTWARE-344: walks the aliased pixels of a wire edge between two
        /// screen-space RasterVertices, invoking `emit(x, y, t)` with the ORIGINAL segment
        /// parameter. D3D9 defines non-antialiased lines by the GDI rule; sampling a ceil/floor DDA
        /// picked adjacent pixels on fractional diagonals and included endpoints which never left
        /// their final diamond. Iterate along the dominant axis, test the bounded neighbouring
        /// diamonds exactly, and therefore remain O(line length) rather than scanning its box.
        template <typename EmitFn>
        void WalkWireEdge(const RasterClipRect& clip, const RasterVertex& a, const RasterVertex& b,
                          EmitFn&& emit)
        {
            if (!std::isfinite(a.x) || !std::isfinite(a.y) ||
                !std::isfinite(b.x) || !std::isfinite(b.y))
                return;
            const float dx = b.x - a.x, dy = b.y - a.y;
            if (!(dx != 0.0f || dy != 0.0f))
                return;

            const auto testAndEmit = [&](int x, int y)
            {
                if (x < clip.minX || x > clip.maxX || y < clip.minY || y > clip.maxY)
                    return;
                float t = 0.0f;
                if (SegmentExitsPixelDiamond(a, b, x, y, t))
                    emit(x, y, t);
            };

            if (std::fabs(dx) >= std::fabs(dy))
            {
                const int firstX = std::max(
                    clip.minX, static_cast<int>(std::floor(std::min(a.x, b.x) - 1.0f)));
                const int lastX = std::min(
                    clip.maxX, static_cast<int>(std::ceil(std::max(a.x, b.x) + 1.0f)));
                for (int x = firstX; x <= lastX; ++x)
                {
                    const float projectedT = std::clamp(
                        ((static_cast<float>(x) + 0.5f) - a.x) / dx, 0.0f, 1.0f);
                    const int centerY = static_cast<int>(std::floor(a.y + projectedT * dy));
                    for (int y = centerY - 2; y <= centerY + 2; ++y)
                        testAndEmit(x, y);
                }
            }
            else
            {
                const int firstY = std::max(
                    clip.minY, static_cast<int>(std::floor(std::min(a.y, b.y) - 1.0f)));
                const int lastY = std::min(
                    clip.maxY, static_cast<int>(std::ceil(std::max(a.y, b.y) + 1.0f)));
                for (int y = firstY; y <= lastY; ++y)
                {
                    const float projectedT = std::clamp(
                        ((static_cast<float>(y) + 0.5f) - a.y) / dy, 0.0f, 1.0f);
                    const int centerX = static_cast<int>(std::floor(a.x + projectedT * dx));
                    for (int x = centerX - 2; x <= centerX + 2; ++x)
                        testAndEmit(x, y);
                }
            }
        }

        /// Rasterizes a one-pixel line under the active multisample mode.  A disabled
        /// RasterizerState.MultiSampleAntiAlias deliberately retains the historical pixel-center
        /// DDA and replicates each covered pixel to every sample.  When enabled, a one-pixel-wide
        /// rectangle around the segment is evaluated at the same four standard locations as
        /// triangle coverage.  The exact mask and the original-segment parameter at each sample
        /// are forwarded so depth can be evaluated independently by the fragment pipeline.
        template <typename EmitFn>
        void WalkRasterLine(const SoftwareFramebuffer& fb, bool multiSampleAntiAlias,
                            const RasterClipRect& clip, const RasterVertex& a,
                            const RasterVertex& b, EmitFn&& emit)
        {
            if (!fb.HasMultiSampleColor() || !multiSampleAntiAlias)
            {
                WalkWireEdge(clip, a, b, [&](int x, int y, float t) {
                    emit(x, y, t, 0xFFFFFFFFu, nullptr);
                });
                return;
            }

            float t0 = 0.0f;
            float t1 = 1.0f;
            if (!ClipSegmentToRect(a.x, a.y, b.x, b.y, clip, t0, t1))
                return;

            const float originalDx = b.x - a.x;
            const float originalDy = b.y - a.y;
            const float ax = a.x + t0 * originalDx;
            const float ay = a.y + t0 * originalDy;
            const float bx = a.x + t1 * originalDx;
            const float by = a.y + t1 * originalDy;
            const float dx = bx - ax;
            const float dy = by - ay;
            const float lengthSquared = dx * dx + dy * dy;
            if (!(lengthSquared > 0.0f) || !std::isfinite(lengthSquared))
            {
                WalkWireEdge(clip, a, b, [&](int x, int y, float t) {
                    emit(x, y, t, 0xFu, nullptr);
                });
                return;
            }

            const int minX = std::max(
                clip.minX, static_cast<int>(std::floor(std::min(ax, bx) - 0.5f)));
            const int maxX = std::min(
                clip.maxX, static_cast<int>(std::floor(std::max(ax, bx) + 0.5f)));
            const int minY = std::max(
                clip.minY, static_cast<int>(std::floor(std::min(ay, by) - 0.5f)));
            const int maxY = std::min(
                clip.maxY, static_cast<int>(std::floor(std::max(ay, by) + 0.5f)));

            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    unsigned int coverageMask = 0u;
                    std::array<float, 4> sampleTs{};
                    for (int sample = 0; sample < 4; ++sample)
                    {
                        const auto& samplePosition =
                            kStandardFourSamplePositions[static_cast<std::size_t>(sample)];
                        const float sampleX = static_cast<float>(x) +
                            samplePosition.x;
                        const float sampleY = static_cast<float>(y) + samplePosition.y;
                        const float localT = ((sampleX - ax) * dx +
                                              (sampleY - ay) * dy) / lengthSquared;
                        if (localT < 0.0f || localT > 1.0f)
                            continue;
                        const float nearestX = ax + localT * dx;
                        const float nearestY = ay + localT * dy;
                        const float distanceX = sampleX - nearestX;
                        const float distanceY = sampleY - nearestY;
                        if (distanceX * distanceX + distanceY * distanceY <= 0.25f)
                        {
                            coverageMask |= 1u << sample;
                            sampleTs[static_cast<std::size_t>(sample)] =
                                t0 + localT * (t1 - t0);
                        }
                    }
                    if (coverageMask == 0u)
                        continue;

                    const float centerX = static_cast<float>(x) + 0.5f;
                    const float centerY = static_cast<float>(y) + 0.5f;
                    const float localT = std::clamp(
                        ((centerX - ax) * dx + (centerY - ay) * dy) / lengthSquared,
                        0.0f, 1.0f);
                    emit(x, y, t0 + localT * (t1 - t0), coverageMask, &sampleTs);
                }
            }
        }

        /// REMED-GFX-082: writes one already-interpolated colored fragment (the DrawColoredPrimitives
        /// path -- opaque, no texture/blend). `pr..pa` are the perspective-premultiplied color sums
        /// (color * invW), divided by invW here exactly as the fill loop did, so the shared helper is
        /// byte-identical for the Solid fill and reused verbatim by the WireFrame line walk. The clip
        /// guard is a no-op for the fill loop (its bounding box is already clamped to `clip`) and the
        /// safety net for the line walk.
#ifndef CNA_SOFTWARE_2D_ONLY
        inline void WriteColoredFragment(SoftwareFramebuffer& fb, const RasterDepthState& depthState,
                                         const RasterStencilState& stencilState,
                                         const RasterClipRect& clip, int x, int y,
                                         float depth, float invW, float pr, float pg, float pb, float pa,
                                         int colorWriteMask, unsigned int multiSampleMask,
                                         SoftwareOcclusionQueryRenderer* occlusionQuery,
                                         unsigned int coverageMask = 0xFFFFFFFFu,
                                         const std::array<float, 4>* sampleDepths = nullptr)
        {
            if (x < clip.minX || x > clip.maxX || y < clip.minY || y > clip.maxY)
                return;
            const unsigned int availableSamples = fb.HasMultiSampleColor() ? 0xFu : 0x1u;
            const unsigned int activeSamples =
                multiSampleMask & coverageMask & availableSamples;
            if (activeSamples == 0u)
                return;
            if (g_samplerTrace.enabled) { g_samplerTrace.fragX = x; g_samplerTrace.fragY = y; }
            const std::size_t pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.width) +
                                           static_cast<std::size_t>(x);
            const unsigned int passingSamples = ApplyFragmentTests(
                fb, depthState, stencilState, pixelIndex, activeSamples, depth, sampleDepths,
                occlusionQuery);
            if (passingSamples == 0u)
                return;
            const float r = pr / invW, g = pg / invW, b = pb / invW, a = pa / invW;
            const std::array<float, 4> output{r, g, b, a};
            if (!fb.HasMultiSampleColor())
            {
                fb.WriteColor(pixelIndex, -1, output, colorWriteMask);
                return;
            }
            for (int sample = 0; sample < 4; ++sample)
            {
                if ((passingSamples & (1u << sample)) == 0u)
                    continue;
                fb.WriteColor(pixelIndex, sample, output, colorWriteMask);
            }
        }

        /// Fills one triangle into `fb` using a standard edge-function/barycentric rasterizer,
        /// with a per-sample depth test/write (one sample per pixel without MSAA) and
        /// backface culling per `cullMode` (SOFTWARE-81; raw ordinal, see ShouldCullTriangle()).
        /// REMED-GFX-082: when `wireframe`, only the edges selected by `edgeMask` are rasterized
        /// (line walk) instead of the interior fill -- culling and the zero-area reject are shared, so
        /// a culled/degenerate triangle emits no wire either.
        void RasterizeTriangle(SoftwareFramebuffer& fb, const RasterDepthState& depthState,
                               const RasterStencilState& stencilState, int cullMode,
                               float depthBias, float slopeScaleDepthBias,
                               const RasterClipRect& clip,
                               const RasterVertex& v0, const RasterVertex& v1, const RasterVertex& v2,
                               int colorWriteMask, unsigned int multiSampleMask,
                               SoftwareOcclusionQueryRenderer* occlusionQuery,
                               bool multiSampleAntiAlias,
                               bool wireframe = false, unsigned edgeMask = kEdgeAll)
        {
            const float area = EdgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
            if (area == 0.0f)
                return;  // degenerate (zero-area) triangle
            if (ShouldCullTriangle(area, cullMode))
                return;
            const RasterStencilState faceStencil = SelectStencilFace(
                stencilState, area < 0.0f);

            // REMED-GFX-083: one polygon-offset value for the whole triangle (after culling; a culled
            // triangle emits no fragments, biased or not). hasBias is 0 for the common zero-bias case, so
            // the depth expressions below are byte-identical to pre-GFX-083 then.
            const float biasOffset = ComputeDepthBiasOffset(v0, v1, v2, depthBias, slopeScaleDepthBias);
            const bool hasBias = (biasOffset != 0.0f);

            if (wireframe)
            {
                // REMED-GFX-082: rasterize the selected edges as perspective-correct lines, reusing
                // WriteColoredFragment (the same depth-test/write + clip path as the fill below).
                const auto drawEdge = [&](const RasterVertex& A, const RasterVertex& B) {
                    WalkRasterLine(fb, multiSampleAntiAlias, clip, A, B,
                                   [&](int x, int y, float t, unsigned int coverageMask,
                                       const std::array<float, 4>* sampleTs) {
                        const float invW  = A.invW  + t * (B.invW  - A.invW);
                        float depth = A.depth + t * (B.depth - A.depth);
                        if (hasBias) depth = std::clamp(depth + biasOffset, 0.0f, 1.0f);  // REMED-GFX-083
                        std::array<float, 4> sampleDepths{};
                        if (sampleTs != nullptr)
                        {
                            for (int sample = 0; sample < 4; ++sample)
                            {
                                float sampleDepth = A.depth +
                                    (*sampleTs)[static_cast<std::size_t>(sample)] *
                                    (B.depth - A.depth);
                                if (hasBias)
                                    sampleDepth = std::clamp(sampleDepth + biasOffset, 0.0f, 1.0f);
                                sampleDepths[static_cast<std::size_t>(sample)] = sampleDepth;
                            }
                        }
                        WriteColoredFragment(fb, depthState, faceStencil, clip, x, y, depth, invW,
                                             A.r + t * (B.r - A.r), A.g + t * (B.g - A.g),
                                             A.b + t * (B.b - A.b), A.a + t * (B.a - A.a),
                                             colorWriteMask, multiSampleMask, occlusionQuery,
                                             coverageMask,
                                             sampleTs != nullptr ? &sampleDepths : nullptr);
                    });
                };
                if (edgeMask & kEdgeV0V1) drawEdge(v0, v1);
                if (edgeMask & kEdgeV1V2) drawEdge(v1, v2);
                if (edgeMask & kEdgeV2V0) drawEdge(v2, v0);
                return;
            }

            const float minXf = std::min({v0.x, v1.x, v2.x});
            const float maxXf = std::max({v0.x, v1.x, v2.x});
            const float minYf = std::min({v0.y, v1.y, v2.y});
            const float maxYf = std::max({v0.y, v1.y, v2.y});

            // REMED-GFX-079: clamp the raster bounding box to the clip rectangle (framebuffer ∩
            // active Viewport) instead of the raw framebuffer -- pixels outside the Viewport are
            // never touched. A default full-target viewport yields the pre-GFX-079
            // [0,width-1] x [0,height-1] clamp byte-for-byte.
            int minX = 0, minY = 0, maxX = -1, maxY = -1;
            if (!CalculateRasterBounds(minXf, minYf, maxXf, maxYf, clip,
                                       minX, minY, maxX, maxY))
                return;

            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;

                    const float w0 = EdgeFunction(v1.x, v1.y, v2.x, v2.y, px, py);
                    const float w1 = EdgeFunction(v2.x, v2.y, v0.x, v0.y, px, py);
                    const float w2 = EdgeFunction(v0.x, v0.y, v1.x, v1.y, px, py);

                    unsigned int coverageMask = 1u;
                    std::array<float, 4> sampleDepths{};
                    if (!fb.HasMultiSampleColor())
                    {
                        if (!TriangleContainsSample(v0, v1, v2, area, w0, w1, w2))
                            continue;
                    }
                    else if (!multiSampleAntiAlias)
                    {
                        if (!TriangleContainsSample(v0, v1, v2, area, w0, w1, w2))
                            continue;
                        coverageMask = 0xFu;
                    }
                    else
                    {
                        coverageMask = 0u;
                        for (int sample = 0; sample < 4; ++sample)
                        {
                            const auto& samplePosition =
                                kStandardFourSamplePositions[static_cast<std::size_t>(sample)];
                            const float sampleX = static_cast<float>(x) +
                                samplePosition.x;
                            const float sampleY = static_cast<float>(y) + samplePosition.y;
                            const float sampleW0 = EdgeFunction(
                                v1.x, v1.y, v2.x, v2.y, sampleX, sampleY);
                            const float sampleW1 = EdgeFunction(
                                v2.x, v2.y, v0.x, v0.y, sampleX, sampleY);
                            const float sampleW2 = EdgeFunction(
                                v0.x, v0.y, v1.x, v1.y, sampleX, sampleY);
                            if (!TriangleContainsSample(v0, v1, v2, area,
                                                        sampleW0, sampleW1, sampleW2))
                                continue;
                            coverageMask |= 1u << sample;
                            float sampleDepth = (sampleW0 * v0.depth +
                                                 sampleW1 * v1.depth +
                                                 sampleW2 * v2.depth) / area;
                            if (hasBias)
                                sampleDepth = std::clamp(sampleDepth + biasOffset,
                                                         0.0f, 1.0f);
                            sampleDepths[static_cast<std::size_t>(sample)] = sampleDepth;
                        }
                        if (coverageMask == 0u)
                            continue;
                    }

                    const float lambda0 = w0 / area;
                    const float lambda1 = w1 / area;

                    // Post-divide depth interpolates linearly in screen space -- no perspective
                    // correction needed for this one attribute (a well-known rasterization
                    // property), unlike color/UV below.
                    float depth = BarycentricInterpolate(
                        v0.depth, v1.depth, v2.depth, lambda0, lambda1);
                    if (hasBias) depth = std::clamp(depth + biasOffset, 0.0f, 1.0f);  // REMED-GFX-083
                    if (fb.HasMultiSampleColor() && !multiSampleAntiAlias)
                        sampleDepths.fill(depth);
                    const float invW = BarycentricInterpolate(
                        v0.invW, v1.invW, v2.invW, lambda0, lambda1);
                    WriteColoredFragment(fb, depthState, faceStencil, clip, x, y, depth, invW,
                                         BarycentricInterpolate(v0.r, v1.r, v2.r, lambda0, lambda1),
                                         BarycentricInterpolate(v0.g, v1.g, v2.g, lambda0, lambda1),
                                         BarycentricInterpolate(v0.b, v1.b, v2.b, lambda0, lambda1),
                                         BarycentricInterpolate(v0.a, v1.a, v2.a, lambda0, lambda1),
                                         colorWriteMask, multiSampleMask, occlusionQuery, coverageMask,
                                         fb.HasMultiSampleColor() ? &sampleDepths : nullptr);
                }
            }
        }
#endif

        // ---- Phase S5/S6: generalized (textured/blended/effect-driven) rasterization ----

#ifndef CNA_SOFTWARE_2D_ONLY
        /// Transforms a vertex whose byte layout is inferred from `stride` (plans/plan_software.md
        /// design decision 2: 16=VertexPositionColor, 20=VertexPositionTexture,
        /// 24=VertexPositionColorTexture, 32=VertexPositionNormalTexture (SOFTWARE-82,
        /// EnvironmentMapEffect), 48/60=VertexPositionNormalTangentTexture with one/two UV sets,
        /// 52=VertexPositionNormalTextureSkinned (SOFTWARE-82, SkinnedEffect),
        /// 56=the same skinned layout plus Color, and
        /// 68/76=VertexPositionNormalTangentTextureSkinned with one/two UV sets) into clip space,
        /// for the
        /// DrawPrimitivesEx/DrawIndexedPrimitivesEx path.
        /// `params.vertexColorEnabled` mirrors GpuDrawParams' own flag -- when false (or for a
        /// stride with no Color field at all), vertex color is treated as opaque white so it
        /// doesn't affect the eventual texture/diffuse modulation, matching a real Effect's own
        /// VertexColorEnabled=false behavior. Attributes are left un-premultiplied; near-plane
        /// clipping (SOFTWARE-106) happens on ClipVertex, before the perspective divide.
        /// REMED-GFX-201: reads one combined vertex whose bytes may live in several bound streams.
        ///
        /// The layout above is expressed in COMBINED byte offsets, and a multi-stream draw stores
        /// those same bytes in separate buffers with separate strides. This resolves each offset to
        /// the stream that owns it instead of copying the streams together: there is no
        /// interleaved temporary, no allocation, and for a single-stream draw `At(n)` is exactly
        /// the `raw + n` this path used before -- `recordBase[0]` is that same pointer and
        /// MapCombinedOffsetToStream degenerates to the identity.
        struct CombinedVertexReader
        {
            const GpuDrawParams* params = nullptr;
            const SoftwareVertexBufferRenderer* fallbackBuffer = nullptr;
            std::array<const std::uint8_t*, kMaxVertexStreams> recordBase{};
            bool useInstanceStreams = false;

            [[nodiscard]] const std::uint8_t* At(int combinedByteOffset) const
            {
                const GpuVertexStreamSlot slot =
                    MapCombinedOffsetToStream(*params, combinedByteOffset);
                return recordBase[static_cast<std::size_t>(slot.streamIndex)] +
                       slot.byteOffsetInStream;
            }

            struct Attribute
            {
                bool found = false;
                std::array<float, 4> value{0.0f, 0.0f, 0.0f, 1.0f};
            };

            [[nodiscard]] static Attribute Decode(
                const std::uint8_t* bytes,
                Microsoft::Xna::Framework::Graphics::VertexElementFormat format)
            {
                using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
                using Microsoft::Xna::Framework::Graphics::PackedVector::HalfTypeHelper;

                Attribute result;
                result.found = true;
                switch (format)
                {
                    case VertexElementFormat::Single:
                        std::memcpy(&result.value[0], bytes, sizeof(float));
                        break;
                    case VertexElementFormat::Vector2:
                        std::memcpy(result.value.data(), bytes, sizeof(float) * 2u);
                        break;
                    case VertexElementFormat::Vector3:
                        std::memcpy(result.value.data(), bytes, sizeof(float) * 3u);
                        break;
                    case VertexElementFormat::Vector4:
                        std::memcpy(result.value.data(), bytes, sizeof(float) * 4u);
                        break;
                    case VertexElementFormat::Color:
                        for (int i = 0; i < 4; ++i)
                            result.value[static_cast<std::size_t>(i)] = bytes[i] / 255.0f;
                        break;
                    case VertexElementFormat::Byte4:
                        for (int i = 0; i < 4; ++i)
                            result.value[static_cast<std::size_t>(i)] = bytes[i];
                        break;
                    case VertexElementFormat::Short2:
                    case VertexElementFormat::Short4:
                    case VertexElementFormat::NormalizedShort2:
                    case VertexElementFormat::NormalizedShort4:
                    {
                        const int componentCount =
                            (format == VertexElementFormat::Short2 ||
                             format == VertexElementFormat::NormalizedShort2) ? 2 : 4;
                        const bool normalized =
                            format == VertexElementFormat::NormalizedShort2 ||
                            format == VertexElementFormat::NormalizedShort4;
                        for (int i = 0; i < componentCount; ++i)
                        {
                            std::int16_t component = 0;
                            std::memcpy(&component, bytes + static_cast<std::size_t>(i) * 2u,
                                        sizeof(component));
                            result.value[static_cast<std::size_t>(i)] = normalized
                                ? std::max(-1.0f, component / 32767.0f)
                                : static_cast<float>(component);
                        }
                        break;
                    }
                    case VertexElementFormat::HalfVector2:
                    case VertexElementFormat::HalfVector4:
                    {
                        const int componentCount =
                            format == VertexElementFormat::HalfVector2 ? 2 : 4;
                        for (int i = 0; i < componentCount; ++i)
                        {
                            std::uint16_t component = 0;
                            std::memcpy(&component, bytes + static_cast<std::size_t>(i) * 2u,
                                        sizeof(component));
                            result.value[static_cast<std::size_t>(i)] =
                                HalfTypeHelper::Convert(component);
                        }
                        break;
                    }
                }
                return result;
            }

            [[nodiscard]] Attribute Read(
                Microsoft::Xna::Framework::Graphics::VertexElementUsage usage,
                int usageIndex) const
            {
                const auto readFrom = [&](const SoftwareVertexBufferRenderer& buffer,
                                          const std::uint8_t* base,
                                          const GpuVertexStreamBinding* stream) -> Attribute {
                    const auto& elements = buffer.Declaration().GetElements();
                    for (std::size_t elementIndex = 0;
                         elementIndex < elements.size(); ++elementIndex)
                    {
                        const auto& element = elements[elementIndex];
                        const int effectiveUsageIndex = stream != nullptr
                            ? stream->EffectiveUsageIndex(
                                elementIndex, element.getUsageIndexProperty())
                            : element.getUsageIndexProperty();
                        if (element.getVertexElementUsageProperty() == usage &&
                            effectiveUsageIndex == usageIndex)
                        {
                            return Decode(base + element.getOffsetProperty(),
                                          element.getVertexElementFormatProperty());
                        }
                    }
                    return {};
                };

                if (params->vertexStreamCount == 0)
                {
                    if (fallbackBuffer == nullptr || fallbackBuffer->Declaration().IsEmpty())
                        return {};
                    return readFrom(*fallbackBuffer, recordBase[0], nullptr);
                }

                for (int i = 0; i < params->vertexStreamCount; ++i)
                {
                    const auto& stream = params->vertexStreams[static_cast<std::size_t>(i)];
                    if (stream.instanceFrequency != 0 || !stream.vertexShaderInputUsed)
                        continue;
                    const std::uint8_t* base = recordBase[static_cast<std::size_t>(i)];
                    if (base == nullptr)
                        continue;   // SOFTWARE-322: native out-of-range fetch -> default attribute
                    const auto* buffer =
                        static_cast<const SoftwareVertexBufferRenderer*>(stream.buffer);
                    const Attribute attribute = readFrom(*buffer, base, &stream);
                    if (attribute.found)
                        return attribute;
                }
                return {};
            }

            [[nodiscard]] bool ReadInstanceMatrix(float matrix[16]) const
            {
                if (!useInstanceStreams)
                    return false;
                // OpenGL's disabled/default generic vertex attribute is (0,0,0,1), which is also
                // what EasyGL's stock instancing locations retain when a declaration supplies
                // fewer than four columns. The public parity fixtures use a complete matrix, but
                // preserving the native default makes malformed/short declarations deterministic.
                std::fill(matrix, matrix + 16, 0.0f);
                matrix[3] = matrix[7] = matrix[11] = matrix[15] = 1.0f;

                int column = 0;
                bool found = false;
                for (int i = 0; i < params->vertexStreamCount && column < 4; ++i)
                {
                    const auto& stream = params->vertexStreams[static_cast<std::size_t>(i)];
                    if (stream.instanceFrequency <= 0)
                        continue;
                    const auto* buffer =
                        static_cast<const SoftwareVertexBufferRenderer*>(stream.buffer);
                    for (const auto& element : buffer->Declaration().GetElements())
                    {
                        if (column >= 4)
                            break;
                        const std::uint8_t* base = recordBase[static_cast<std::size_t>(i)];
                        if (base == nullptr)
                        {
                            ++column;
                            continue;   // retain GL's disabled-attribute default for this column
                        }
                        const Attribute attribute = Decode(
                            base + element.getOffsetProperty(),
                            element.getVertexElementFormatProperty());
                        for (int component = 0; component < 4; ++component)
                        {
                            matrix[static_cast<std::size_t>(column * 4 + component)] =
                                attribute.value[static_cast<std::size_t>(component)];
                        }
                        ++column;
                        found = true;
                    }
                }
                return found;
            }

            [[nodiscard]] bool HasDeclaration() const
            {
                if (params->vertexStreamCount == 0)
                    return fallbackBuffer != nullptr && !fallbackBuffer->Declaration().IsEmpty();
                for (int i = 0; i < params->vertexStreamCount; ++i)
                {
                    const auto& stream = params->vertexStreams[static_cast<std::size_t>(i)];
                    if (stream.instanceFrequency == 0 &&
                        !static_cast<const SoftwareVertexBufferRenderer*>(stream.buffer)
                             ->Declaration().IsEmpty())
                        return true;
                }
                return false;
            }
        };

        ClipVertex BuildLegacyGenericClipVertex(const CombinedVertexReader& raw, std::size_t stride,
                                                const Matrix& combined,
                                                const GpuDrawParams& params)
        {
            Vector3 position;
            std::memcpy(&position, raw.At(0), sizeof(Vector3));
            Vector3 normal(0.0f, 0.0f, 1.0f);
            bool haveNormal = false;

            const bool skinnedLayout =
                stride == 52 || stride == 56 || stride == 68 || stride == 76 || stride == 80;
            if (skinnedLayout && params.skinned)
            {
                // The stride-52/56 layouts carry BlendWeight@32 and BlendIndices@48; the
                // tangent-bearing stride-68/76 layouts carry them at 48/64. Blend up to
                // weightsPerVertex bone matrices (column-major, GpuDrawParams::boneTransforms'
                // own layout -- Task 895's "only sum the first N pairs" behavior) and apply the
                // blended matrix to Position/Normal BEFORE the standard World*View*Projection
                // transform below, mirroring FNA's own Skin(vin, boneCount) step.
                const bool tangentSkinned = stride == 68 || stride == 76 || stride == 80;
                const int blendWeightOffset = tangentSkinned ? 48 : 32;
                const int blendIndicesOffset = tangentSkinned ? 64 : 48;
                Vector4 blendWeight;
                std::memcpy(&blendWeight, raw.At(blendWeightOffset), sizeof(Vector4));
                std::uint8_t blendIndices[4];
                std::memcpy(blendIndices, raw.At(blendIndicesOffset), 4);
                const float weights[4] = {blendWeight.X, blendWeight.Y, blendWeight.Z, blendWeight.W};

                float blended[16] = {};
                const int n = std::clamp(params.weightsPerVertex, 1, 4);
                for (int k = 0; k < n; ++k)
                {
                    const int boneIndex = std::clamp(static_cast<int>(blendIndices[k]), 0, 71);
                    const float* bone = &params.boneTransforms[static_cast<std::size_t>(boneIndex) * 16u];
                    for (int e = 0; e < 16; ++e)
                        blended[e] += bone[e] * weights[k];
                }

                position = ApplyAffineColumnMajor(blended, position, 1.0f);
                std::memcpy(&normal, raw.At(12), sizeof(Vector3));
                normal = ApplyAffineColumnMajor(blended, normal, 0.0f);
                haveNormal = true;
            }

            float instanceMatrix[16];
            if (raw.ReadInstanceMatrix(instanceMatrix))
            {
                position = ApplyAffineColumnMajor(instanceMatrix, position, 1.0f);
                if (haveNormal)
                    normal = ApplyAffineColumnMajor(instanceMatrix, normal, 0.0f);
            }

            const Vector4 clip = Vector4::Transform(position, combined);

            ClipVertex out;
            out.x = clip.X; out.y = clip.Y; out.z = clip.Z; out.w = clip.W;
            out.fogKeep = ComputeClassicFogKeep(position, params);

            if (stride == 16)
            {
                UnpackColorBytes(raw.At(12), out.r, out.g, out.b, out.a);
            }
            else if (stride == 20)
            {
                std::memcpy(&out.u, raw.At(12), sizeof(float));
                std::memcpy(&out.v, raw.At(16), sizeof(float));
            }
            else if (stride == 24)
            {
                UnpackColorBytes(raw.At(12), out.r, out.g, out.b, out.a);
                std::memcpy(&out.u, raw.At(16), sizeof(float));
                std::memcpy(&out.v, raw.At(20), sizeof(float));
            }
            else if (stride == 32)
            {
                // VertexPositionNormalTexture: Position@0, Normal@12, TextureCoordinate@24.
                std::memcpy(&normal, raw.At(12), sizeof(Vector3));
                haveNormal = true;
                std::memcpy(&out.u, raw.At(24), sizeof(float));
                std::memcpy(&out.v, raw.At(28), sizeof(float));
            }
            else if (stride == 48 || stride == 60)
            {
                // VertexPositionNormalTangentTexture, optionally followed by TextureCoordinate1.
                // The Software PBR fallback samples the base-colour texture, so consume that map's
                // selector (mask bit 0) without pretending to evaluate the other PBR maps here.
                std::memcpy(&normal, raw.At(12), sizeof(Vector3));
                haveNormal = true;
                std::memcpy(&out.u, raw.At(40), sizeof(float));
                std::memcpy(&out.v, raw.At(44), sizeof(float));
                if (stride == 60 && (params.pbrTextureCoordinateSetMask & 1u) != 0u)
                {
                    std::memcpy(&out.u1, raw.At(48), sizeof(float));
                    std::memcpy(&out.v1, raw.At(52), sizeof(float));
                    out.u = out.u1;
                    out.v = out.v1;
                }
                // plans/plan_gltf.md GLTF-462: stride 60's last four bytes were reserved padding and are
                // the packed COLOR_0 now. §3.7.2.1 makes it "an additional linear multiplier to base
                // color", and this raster path already multiplies out.r/g/b/a into the sampled base
                // colour -- so reading it here is the whole of vertex-coloured PBR for this
                // renderer. The `!params.vertexColorEnabled` guard further down replaces it with
                // white for an uncoloured primitive, exactly as it does for stride 56.
                if (stride == 60)
                    UnpackColorBytes(raw.At(56), out.r, out.g, out.b, out.a);
            }
            else if (stride == 52 || stride == 56)
            {
                std::memcpy(&out.u, raw.At(24), sizeof(float));
                std::memcpy(&out.v, raw.At(28), sizeof(float));
                if (stride == 56)
                    UnpackColorBytes(raw.At(52), out.r, out.g, out.b, out.a);
            }
            else if (stride == 68 || stride == 76 || stride == 80)
            {
                std::memcpy(&out.u, raw.At(40), sizeof(float));
                std::memcpy(&out.v, raw.At(44), sizeof(float));
                if ((stride == 76 || stride == 80) &&
                    (params.pbrTextureCoordinateSetMask & 1u) != 0u)
                {
                    std::memcpy(&out.u1, raw.At(68), sizeof(float));
                    std::memcpy(&out.v1, raw.At(72), sizeof(float));
                    out.u = out.u1;
                    out.v = out.v1;
                }
                // plans/plan_gltf.md GLTF-463: stride 80 is the stride-76 skinned PBR record with a packed
                // COLOR_0 appended. This raster path already multiplies out.r/g/b/a into the sampled
                // base colour, so reading it here is the whole of skinned vertex-coloured PBR for
                // this renderer -- and `!params.vertexColorEnabled` further down replaces it with
                // white for an uncoloured primitive, exactly as it does for strides 56 and 60.
                if (stride == 80)
                    UnpackColorBytes(raw.At(76), out.r, out.g, out.b, out.a);
            }

            if (stride == 48 || stride == 60 || stride == 68 || stride == 76 || stride == 80)
            {
                // PbrEffect's base-colour transform is slot zero. Identity rows make this an exact
                // no-op for old callers and materials without KHR_texture_transform.
                const float u = out.u;
                const float v = out.v;
                out.u = u * params.pbrTextureTransformRows[0][0] +
                        v * params.pbrTextureTransformRows[0][1] +
                        params.pbrTextureTransformRows[0][2];
                out.v = u * params.pbrTextureTransformRows[1][0] +
                        v * params.pbrTextureTransformRows[1][1] +
                        params.pbrTextureTransformRows[1][2];
            }

            if (!params.vertexColorEnabled)
            {
                out.r = out.g = out.b = out.a = 1.0f;
            }
            PrepareUnlitCommonDiffuseVertex(out, params);
            PrepareEnvironmentMapVertex(out, position, normal, haveNormal, params);
            PrepareClassicLightingVertex(out, position, normal, haveNormal, params);
            return out;
        }

        ClipVertex BuildGenericClipVertex(const CombinedVertexReader& raw, std::size_t stride,
                                          const Matrix& combined,
                                          const GpuDrawParams& params)
        {
            using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

            if (!raw.HasDeclaration())
                return BuildLegacyGenericClipVertex(raw, stride, combined, params);

            const auto positionAttribute = raw.Read(VertexElementUsage::Position, 0);
            Vector3 position(positionAttribute.value[0], positionAttribute.value[1],
                             positionAttribute.value[2]);

            const auto normalAttribute = raw.Read(VertexElementUsage::Normal, 0);
            Vector3 normal(normalAttribute.value[0], normalAttribute.value[1],
                           normalAttribute.value[2]);
            bool haveNormal = normalAttribute.found;

            if (params.skinned)
            {
                const auto weightsAttribute = raw.Read(VertexElementUsage::BlendWeight, 0);
                const auto indicesAttribute = raw.Read(VertexElementUsage::BlendIndices, 0);
                float blended[16] = {};
                const int n = std::clamp(params.weightsPerVertex, 1, 4);
                for (int k = 0; k < n; ++k)
                {
                    const int boneIndex = std::clamp(
                        static_cast<int>(indicesAttribute.value[static_cast<std::size_t>(k)]),
                        0, 71);
                    const float* bone =
                        &params.boneTransforms[static_cast<std::size_t>(boneIndex) * 16u];
                    const float weight = weightsAttribute.value[static_cast<std::size_t>(k)];
                    for (int e = 0; e < 16; ++e)
                        blended[e] += bone[e] * weight;
                }
                position = ApplyAffineColumnMajor(blended, position, 1.0f);
                normal = ApplyAffineColumnMajor(blended, normal, 0.0f);
            }

            float instanceMatrix[16];
            if (raw.ReadInstanceMatrix(instanceMatrix))
            {
                position = ApplyAffineColumnMajor(instanceMatrix, position, 1.0f);
                if (haveNormal)
                    normal = ApplyAffineColumnMajor(instanceMatrix, normal, 0.0f);
            }

            const Vector4 clip = Vector4::Transform(position, combined);
            ClipVertex out;
            out.x = clip.X; out.y = clip.Y; out.z = clip.Z; out.w = clip.W;
            out.fogKeep = ComputeClassicFogKeep(position, params);

            const auto colorAttribute = raw.Read(VertexElementUsage::Color, 0);
            if (colorAttribute.found)
            {
                out.r = colorAttribute.value[0];
                out.g = colorAttribute.value[1];
                out.b = colorAttribute.value[2];
                out.a = colorAttribute.value[3];
            }

            auto uvAttribute = raw.Read(VertexElementUsage::TextureCoordinate, 0);
            const auto uv1Attribute = raw.Read(VertexElementUsage::TextureCoordinate, 1);
            if (uv1Attribute.found)
            {
                out.u1 = uv1Attribute.value[0];
                out.v1 = uv1Attribute.value[1];
            }
            if (params.pbr && (params.pbrTextureCoordinateSetMask & 1u) != 0u)
            {
                if (uv1Attribute.found)
                    uvAttribute = uv1Attribute;
            }
            if (uvAttribute.found)
            {
                out.u = uvAttribute.value[0];
                out.v = uvAttribute.value[1];
            }

            if (params.pbr)
            {
                const float u = out.u;
                const float v = out.v;
                out.u = u * params.pbrTextureTransformRows[0][0] +
                        v * params.pbrTextureTransformRows[0][1] +
                        params.pbrTextureTransformRows[0][2];
                out.v = u * params.pbrTextureTransformRows[1][0] +
                        v * params.pbrTextureTransformRows[1][1] +
                        params.pbrTextureTransformRows[1][2];
            }

            if (!params.vertexColorEnabled)
                out.r = out.g = out.b = out.a = 1.0f;
            PrepareUnlitCommonDiffuseVertex(out, params);
            PrepareEnvironmentMapVertex(out, position, normal, haveNormal, params);
            PrepareClassicLightingVertex(out, position, normal, haveNormal, params);
            return out;
        }
#endif

        /// REMED-GFX-073/079: the raster clip rectangle = the GraphicsDevice.Viewport rectangle
        /// intersected with the framebuffer. Uses a wider intermediate for the right/bottom edge so
        /// a large Viewport.X+Width / Viewport.Y+Height cannot overflow int; a zero/negative-size
        /// viewport collapses to an empty rectangle (nothing drawn).
        RasterClipRect ViewportClip(const SoftwareFramebuffer& fb, int vpX, int vpY, int vpW, int vpH)
        {
            const long long rightExclusive = static_cast<long long>(vpX) + static_cast<long long>(std::max(0, vpW));
            const long long bottomExclusive = static_cast<long long>(vpY) + static_cast<long long>(std::max(0, vpH));
            RasterClipRect c;
            c.minX = std::max(0, vpX);
            c.minY = std::max(0, vpY);
            c.maxX = static_cast<int>(std::min<long long>(static_cast<long long>(fb.width), rightExclusive)) - 1;
            c.maxY = static_cast<int>(std::min<long long>(static_cast<long long>(fb.height), bottomExclusive)) - 1;
            return c;
        }

        /// REMED-GFX-080: intersects a viewport-derived raster clip (framebuffer ∩ Viewport, from
        /// ViewportClip) with the GraphicsDevice.ScissorRectangle when scissor testing is enabled.
        /// The ScissorRectangle is in framebuffer/target space (NOT viewport-local), so it is
        /// intersected DIRECTLY against the base clip -- no Viewport.X/Y offset is added, matching
        /// XNA/FNA/D3D11 where the scissor is independent of the viewport origin. A disabled scissor
        /// (`scissorEnabled == false`) returns the base clip unchanged, so a small ScissorRectangle
        /// left over from a previous RasterizerState has no effect. A zero/negative-size or
        /// non-overlapping scissor collapses the clip to empty (minX>maxX / minY>maxY -> the raster
        /// loops draw nothing). Uses a wider intermediate for the right/bottom edge so a large
        /// Scissor.X+Width / Scissor.Y+Height cannot overflow int (same overflow-safe pattern as
        /// ViewportClip); base.minX/minY are already >= 0, so a negative scissor origin is clamped
        /// away by the std::max without indexing outside the framebuffer.
        RasterClipRect ScissorClip(const RasterClipRect& base, bool scissorEnabled,
                                   int scX, int scY, int scW, int scH)
        {
            if (!scissorEnabled)
                return base;
            const long long scRightExclusive =
                static_cast<long long>(scX) + static_cast<long long>(std::max(0, scW));
            const long long scBottomExclusive =
                static_cast<long long>(scY) + static_cast<long long>(std::max(0, scH));
            RasterClipRect c = base;
            c.minX = std::max(base.minX, scX);
            c.minY = std::max(base.minY, scY);
            c.maxX = static_cast<int>(std::min<long long>(static_cast<long long>(base.maxX), scRightExclusive - 1));
            c.maxY = static_cast<int>(std::min<long long>(static_cast<long long>(base.maxY), scBottomExclusive - 1));
            return c;
        }

        /// REMED-GFX-082: the per-triangle shading state resolved once by RasterizeTriangleShaded and
        /// shared by every fragment (fill pixel or wire-edge pixel) through WriteShadedFragment.
        struct ShadedContext
        {
            const GpuDrawParams& params;
            // REMED-GFX-124: resolved through the SoftwareColorSurface capability, so an ordinary
            // texture and a finished render target are indistinguishable to every consumer below.
            const SoftwareColorSurface* texture0;
            const SoftwareColorSurface* texture1;
            const SoftwareCubeSurface* envMap;
            bool useDualTexture;
            bool useEnvMap;
            bool needUV;
            SoftwareBlendState blendState;
            std::array<float, 4> blendFactor;
            RasterDepthState depthState; // REMED-GFX-030: per-draw test/write/function snapshot
            RasterStencilState stencilState; // GDI-026: per-draw 8-bit stencil snapshot
            int colorWriteMask;           // REMED-GFX-077: raw XNA ColorWriteChannels (bit0=R..bit3=A)
            unsigned int multiSampleMask; // REMED-GFX-077: single-sample ⇒ only bit 0 is meaningful
            /// SOFTWARE-122: query open when this draw was submitted, or null outside Begin/End.
            SoftwareOcclusionQueryRenderer* occlusionQuery;
            // REMED-GFX-150: the SamplerState of each bound texture slot, resolved once per draw so
            // no fragment can consult a later live state, plus this triangle's magnification
            // classification for each (only XNA filters 5..8 read it).
            SoftwareSamplerState sampler0;
            SoftwareSamplerState sampler1;
            bool magnify0;
            bool magnify1;
            // REMED-GFX-175: this triangle's level-of-detail for each bound texture, log2 of the
            // texel rate, resolved once per triangle alongside the magnification classification it
            // is derived from. The mip component of the filter turns it into a level; ordinals whose
            // resource has a single stored level never consult it.
            float lambda0;
            float lambda1;
            /// SOFTWARE-117: directional footprint used only by TextureFilter::Anisotropic.
            TextureFootprint footprint0;
            TextureFootprint footprint1;
            // REMED-GFX-182: the reflection cube's own magnification classification and
            // level-of-detail. The SAMPLER is `sampler1` -- the cube and DualTextureEffect's second
            // texture share slot 1, exactly as they share binding 1 on every GPU renderer -- but the
            // FOOTPRINT cannot be shared: `lambda1` is derived from `texture1`, which an
            // EnvironmentMapEffect draw does not bind at all, and a cube's footprint comes from the
            // reflection vector rather than from the UV attribute.
            bool magnifyCube;
            float lambdaCube;
            TextureFootprint footprintCube;
        };

        /// SOFTWARE-111: evaluates the exact stock-effect alpha-test expression shared by FNA's
        /// AlphaTestEffect shaders and CNA's GPU renderers. The four values already contain the
        /// half-byte threshold and pass/fail clip weights selected for the public CompareFunction.
        [[nodiscard]] bool AlphaTestPasses(const GpuDrawParams& params, float alpha)
        {
            const bool comparison = params.alphaTest[1] > 0.0f
                ? std::fabs(alpha - params.alphaTest[0]) < params.alphaTest[1]
                : alpha < params.alphaTest[0];
            const float clipWeight = comparison ? params.alphaTest[2] : params.alphaTest[3];
            return !(clipWeight < 0.0f);
        }

        /// REMED-GFX-082: writes one already-interpolated shaded fragment -- the whole texture/diffuse/
        /// dual-texture/env-map/blend pipeline that used to live inline in RasterizeTriangleShaded's
        /// fill loop. `p*` are the perspective-premultiplied attribute sums (attr * invW), divided by
        /// invW here exactly as before, so the Solid fill stays byte-identical and the WireFrame line
        /// walk gets the identical shading along its edges.
        inline void WriteShadedFragment(SoftwareFramebuffer& fb, const ShadedContext& ctx,
                                        const RasterClipRect& clip, int x, int y, float depth, float invW,
                                        float pr, float pg, float pb, float pa,
                                        float pu, float pv, float pu1, float pv1,
                                        float pfogKeep,
                                        float psr, float psg, float psb,
                                        float penvx, float penvy, float penvz, float penvBlend,
                                        float pwpx, float pwpy, float pwpz, float pnx, float pny, float pnz,
                                        unsigned int coverageMask = 0xFFFFFFFFu,
                                        const std::array<float, 4>* sampleDepths = nullptr)
        {
            if (x < clip.minX || x > clip.maxX || y < clip.minY || y > clip.maxY)
                return;
            // GDI-025: 4x CPU MSAA evaluates a 2x2 sub-pixel coverage pattern in the triangle
            // walker below. MultiSampleMask gates those actual samples; single-sample renderers
            // retain the established bit-0 behavior exactly.
            const unsigned int availableSamples = fb.HasMultiSampleColor() ? 0xFu : 0x1u;
            const unsigned int activeSamples =
                ctx.multiSampleMask & coverageMask & availableSamples;
            if (activeSamples == 0u)
                return;
            // REMED-GFX-182: the cube trace reports the same destination pixel, so this stamp is
            // shared by both traces rather than tied to the 2D one.
            if (g_samplerTrace.enabled || g_cubeTrace.enabled)
            { g_samplerTrace.fragX = x; g_samplerTrace.fragY = y; }
            const std::size_t pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.width) +
                                           static_cast<std::size_t>(x);

            float r = pr / invW, g = pg / invW, b = pb / invW, a = pa / invW;
            const float fogKeep = pfogKeep / invW;

            float u = 0.0f, v = 0.0f;
            float u1 = 0.0f, v1 = 0.0f;
            if (ctx.needUV)
            {
                u = pu / invW;
                v = pv / invW;
                u1 = pu1 / invW;
                v1 = pv1 / invW;
            }

            if (ctx.useDualTexture)
            {
                // DualTextureEffect (SOFTWARE-82/116/302): color.rgb*=2;
                // color *= overlay*diffuse
                // (FNA's PSDualTexture). Texture and Texture2 consume TEXCOORD0 and TEXCOORD1
                // independently, with their corresponding sampler slots and footprints. A null
                // sampler returns opaque black on Microsoft XNA 4.0, matching the other classic
                // stock effects' unbound-sampler rule (SOFTWARE-303).
                float t0r = 0.0f, t0g = 0.0f, t0b = 0.0f, t0a = 1.0f;
                if (ctx.texture0 != nullptr)
                    SampleTexture(*ctx.texture0, ctx.sampler0, ctx.magnify0, ctx.lambda0, u, v,
                                  t0r, t0g, t0b, t0a, &ctx.footprint0);
                float t1r = 0.0f, t1g = 0.0f, t1b = 0.0f, t1a = 1.0f;
                if (ctx.texture1 != nullptr)
                    SampleTexture(*ctx.texture1, ctx.sampler1, ctx.magnify1, ctx.lambda1, u1, v1,
                                  t1r, t1g, t1b, t1a, &ctx.footprint1);
                r *= (t0r * 2.0f) * t1r;
                g *= (t0g * 2.0f) * t1g;
                b *= (t0b * 2.0f) * t1b;
                a *= t0a * t1a;
            }
            else if (ctx.params.textureEnabled)
            {
                // D3D9 supplies opaque black for an unbound classic stock-effect sampler.
                // CNAEXT PBR deliberately uses opaque white as its absent-base-map identity.
                float texR = ctx.params.pbr ? 1.0f : 0.0f;
                float texG = ctx.params.pbr ? 1.0f : 0.0f;
                float texB = ctx.params.pbr ? 1.0f : 0.0f;
                float texA = 1.0f;
                if (ctx.texture0 != nullptr)
                    SampleTexture(*ctx.texture0, ctx.sampler0, ctx.magnify0, ctx.lambda0, u, v,
                                  texR, texG, texB, texA, &ctx.footprint0);
                r *= texR;
                g *= texG;
                b *= texB;
                a *= texA;
            }

            if (UsesClassicEffectLighting(ctx.params))
            {
                float specularR;
                float specularG;
                float specularB;
                if (ctx.params.preferPerPixelLighting)
                {
                    const Vector3 worldPosition(
                        pwpx / invW, pwpy / invW, pwpz / invW);
                    const Vector3 worldNormal = NormalizeOrZero(Vector3(
                        pnx / invW, pny / invW, pnz / invW));
                    const ClassicLightingResult lighting =
                        ComputeClassicLighting(worldPosition, worldNormal, ctx.params);
                    r *= lighting.diffuse[0];
                    g *= lighting.diffuse[1];
                    b *= lighting.diffuse[2];
                    a *= ctx.params.diffuseColor[3];
                    specularR = lighting.specular[0];
                    specularG = lighting.specular[1];
                    specularB = lighting.specular[2];
                }
                else
                {
                    // The diffuse/alpha result was evaluated, saturated and multiplied by
                    // vertex colour at each vertex, exactly like VSBasicVertexLighting*.
                    specularR = psr / invW;
                    specularG = psg / invW;
                    specularB = psb / invW;
                }
                // FNA Common.fxh AddSpecular: the light/material result is scaled by the
                // completed texture/effect/vertex alpha, but not by texture or vertex RGB.
                r += specularR * a;
                g += specularG * a;
                b += specularB * a;
            }
            else if (!ctx.params.envMapping && !UsesUnlitCommonDiffuseOutput(ctx.params))
            {
                r *= ctx.params.diffuseColor[0];
                g *= ctx.params.diffuseColor[1];
                b *= ctx.params.diffuseColor[2];
                a *= ctx.params.diffuseColor[3];
            }

            // FNA's AlphaTestEffect pixel shader evaluates texture * vertex colour * diffuse/alpha,
            // then clip(), and only afterward applies fog. A discarded fragment must not update
            // colour, depth, or any stencil operation. The default vector passes, so evaluating it
            // unconditionally also keeps non-alpha-tested stock effects on one exact path.
            if (!AlphaTestPasses(ctx.params, a))
                return;

            // SOFTWARE-111/110: alpha-test discard precedes every observable per-sample
            // depth/stencil operation. Only samples surviving those operations reach colour.
            const unsigned int passingSamples = ApplyFragmentTests(
                fb, ctx.depthState, ctx.stencilState, pixelIndex, activeSamples, depth,
                sampleDepths, ctx.occlusionQuery);
            if (passingSamples == 0u)
                return;

            // GDI-022: ColorMatrixEffect is intentionally a small fixed CPU SpriteBatch effect,
            // not a shader language. It acts after the ordinary texture/tint calculation and
            // before stock-effect additions and BlendState, so its output is the source colour
            // consumed by the normal XNA blend implementation.
            if (ctx.params.cpu2DColorMatrixEnabled)
            {
                const float source[4] = { r, g, b, a };
                const auto transform = [&](int row) {
                    const float* m = ctx.params.cpu2DColorMatrix + row * 4;
                    return std::clamp(m[0] * source[0] + m[1] * source[1] +
                                      m[2] * source[2] + m[3] * source[3] +
                                      ctx.params.cpu2DColorOffset[row], 0.0f, 1.0f);
                };
                r = transform(0);
                g = transform(1);
                b = transform(2);
                a = transform(3);
            }

#ifndef CNA_SOFTWARE_2D_ONLY
            if (ctx.useEnvMap)
            {
                // SOFTWARE-114: FNA computes reflection and Fresnel at each vertex. These are
                // their clipped, perspective-interpolated values; do not re-normalize or
                // reconstruct them here, because the stock pixel shader does neither.
                const Vector3 reflDir(penvx / invW, penvy / invW, penvz / invW);
                const float blendFactor = penvBlend / invW;
                float envR = 0.0f, envG = 0.0f, envB = 0.0f, envA = 1.0f;
                // REMED-GFX-182: the cube is filtered by the PUBLIC SamplerStates[1] this draw
                // captured, through the same sampler every ordinary texture goes through.
                // SOFTWARE-303: a null XNA cube sampler contributes opaque black, just like the
                // 2D stock-effect samplers; no invalid resource dereference is required.
                if (ctx.envMap != nullptr)
                    SampleCubeMap(*ctx.envMap, ctx.sampler1, ctx.magnifyCube, ctx.lambdaCube,
                                  reflDir, envR, envG, envB, envA, &ctx.footprintCube);

                r = r * (1.0f - blendFactor) + (envR * a) * blendFactor + ctx.params.envMapSpecular[0] * envA * a;
                g = g * (1.0f - blendFactor) + (envG * a) * blendFactor + ctx.params.envMapSpecular[1] * envA * a;
                b = b * (1.0f - blendFactor) + (envB * a) * blendFactor + ctx.params.envMapSpecular[2] * envA * a;

                if (g_cubeTrace.enabled) PrintCubeTraceLine(r, g, b);   // REMED-GFX-182
            }
#endif

            // SOFTWARE-112: FNA computes this factor per vertex from the post-skin object
            // position, then the rasterizer perspective-interpolates it. Fog affects RGB only and
            // follows texture/material/env-map and alpha-test processing, immediately before the
            // ordinary BlendState equation. FNA premultiplies FogColor by the completed output
            // alpha before this mix, matching all five classic stock-effect shaders.
            r = ctx.params.fogColor[0] * a * (1.0f - fogKeep) + r * fogKeep;
            g = ctx.params.fogColor[1] * a * (1.0f - fogKeep) + g * fogKeep;
            b = ctx.params.fogColor[2] * a * (1.0f - fogKeep) + b * fogKeep;

            // REMED-GFX-077: final colour channels (opaque store or exact XNA blend result). Each channel is
            // gated by BlendState.ColorWriteChannels — a masked-off channel keeps its existing
            // destination byte (identity), applied AFTER blending (Phase 10). The common All(15)
            // path writes every channel exactly as before.
            const std::array<float, 4> source{r, g, b, a};
            const auto writeBlendedColor = [&](int sample) {
                std::array<float, 4> output = source;
                if (!ctx.blendState.IsOpaqueIdentity())
                {
                    const std::array<float, 4> destination =
                        fb.ReadColor(pixelIndex, sample);
                    output[0] = BlendComponent(0, ctx.blendState.colorSource,
                                               ctx.blendState.colorDestination,
                                               ctx.blendState.colorFunction,
                                               source, destination, ctx.blendFactor);
                    output[1] = BlendComponent(1, ctx.blendState.colorSource,
                                               ctx.blendState.colorDestination,
                                               ctx.blendState.colorFunction,
                                               source, destination, ctx.blendFactor);
                    output[2] = BlendComponent(2, ctx.blendState.colorSource,
                                               ctx.blendState.colorDestination,
                                               ctx.blendState.colorFunction,
                                               source, destination, ctx.blendFactor);
                    output[3] = BlendComponent(3, ctx.blendState.alphaSource,
                                               ctx.blendState.alphaDestination,
                                               ctx.blendState.alphaFunction,
                                               source, destination, ctx.blendFactor);
                }
                fb.WriteColor(pixelIndex, sample, output, ctx.colorWriteMask);
            };
            if (!fb.HasMultiSampleColor())
            {
                writeBlendedColor(-1);
                return;
            }
            for (int sample = 0; sample < 4; ++sample)
            {
                if ((passingSamples & (1u << sample)) == 0u)
                    continue;
                writeBlendedColor(sample);
            }
        }

        /// Builds the fragment state for line and point primitives.  Their one-dimensional or
        /// zero-dimensional footprint has no triangle derivatives, so texture/cube LOD resolves
        /// deterministically to level zero (the same fallback used by a degenerate triangle).
        ShadedContext MakeLinearShadedContext(
            const GpuDrawParams& params, const RasterDepthState& depthState,
            const RasterStencilState& stencilState, const SoftwareBlendState& blendState,
            const std::array<float, 4>& blendFactor, int colorWriteMask,
            unsigned int multiSampleMask, const SoftwareSamplerState& sampler0,
            const SoftwareSamplerState& sampler1,
            SoftwareOcclusionQueryRenderer* occlusionQuery)
        {
            const auto* texture0 = dynamic_cast<const SoftwareColorSurface*>(params.texture0);
            const auto* texture1 = dynamic_cast<const SoftwareColorSurface*>(params.texture1);
#ifndef CNA_SOFTWARE_2D_ONLY
            const auto* envMap = dynamic_cast<const SoftwareCubeSurface*>(params.envMap);
#else
            const SoftwareCubeSurface* envMap = nullptr;
#endif
            const bool useDualTexture = params.dualTexture;
#ifndef CNA_SOFTWARE_2D_ONLY
            const bool useEnvMap = params.envMapping;
#else
            constexpr bool useEnvMap = false;
#endif
            const bool needUV = useDualTexture || useEnvMap || params.textureEnabled;
            return ShadedContext{params, texture0, texture1, envMap, useDualTexture, useEnvMap,
                                 needUV, blendState, blendFactor, depthState, stencilState,
                                 colorWriteMask, multiSampleMask, occlusionQuery, sampler0, sampler1,
                                 true, true, 0.0f, 0.0f, true, 0.0f};
        }

        /// Rasterizes one one-pixel-wide line through the same depth, texture, material and blend
        /// fragment path as triangles.  CullMode and FillMode do not apply to line primitives.
        void RasterizeLineShaded(
            SoftwareFramebuffer& fb, const RasterDepthState& depthState,
            const RasterStencilState& stencilState, const SoftwareBlendState& blendState,
            const std::array<float, 4>& blendFactor, const GpuDrawParams& params,
            const RasterClipRect& clip, const RasterVertex& a, const RasterVertex& b,
            int colorWriteMask, unsigned int multiSampleMask,
            const SoftwareSamplerState& sampler0, const SoftwareSamplerState& sampler1,
            SoftwareOcclusionQueryRenderer* occlusionQuery, bool multiSampleAntiAlias)
        {
            const ShadedContext ctx = MakeLinearShadedContext(
                params, depthState, stencilState, blendState, blendFactor, colorWriteMask,
                multiSampleMask, sampler0, sampler1, occlusionQuery);
            WalkRasterLine(fb, multiSampleAntiAlias, clip, a, b,
                           [&](int x, int y, float t, unsigned int coverageMask,
                               const std::array<float, 4>* sampleTs) {
                const float invW = a.invW + t * (b.invW - a.invW);
                std::array<float, 4> sampleDepths{};
                if (sampleTs != nullptr)
                {
                    for (int sample = 0; sample < 4; ++sample)
                    {
                        const float sampleT = (*sampleTs)[static_cast<std::size_t>(sample)];
                        sampleDepths[static_cast<std::size_t>(sample)] =
                            a.depth + sampleT * (b.depth - a.depth);
                    }
                }
                WriteShadedFragment(
                    fb, ctx, clip, x, y, a.depth + t * (b.depth - a.depth), invW,
                    a.r + t * (b.r - a.r), a.g + t * (b.g - a.g),
                    a.b + t * (b.b - a.b), a.a + t * (b.a - a.a),
                    a.u + t * (b.u - a.u), a.v + t * (b.v - a.v),
                    a.u1 + t * (b.u1 - a.u1), a.v1 + t * (b.v1 - a.v1),
                    a.fogKeep + t * (b.fogKeep - a.fogKeep),
                    a.sr + t * (b.sr - a.sr), a.sg + t * (b.sg - a.sg),
                    a.sb + t * (b.sb - a.sb),
                    a.envx + t * (b.envx - a.envx), a.envy + t * (b.envy - a.envy),
                    a.envz + t * (b.envz - a.envz),
                    a.envBlend + t * (b.envBlend - a.envBlend),
                    a.wpx + t * (b.wpx - a.wpx), a.wpy + t * (b.wpy - a.wpy),
                    a.wpz + t * (b.wpz - a.wpz), a.nx + t * (b.nx - a.nx),
                    a.ny + t * (b.ny - a.ny), a.nz + t * (b.nz - a.nz),
                    coverageMask, sampleTs != nullptr ? &sampleDepths : nullptr);
            });
        }

        /// Rasterizes an XNA/CNA point as one framebuffer pixel centered on the transformed
        /// coordinate.  Like GPU point-list paths, it is unaffected by culling and FillMode.
        void RasterizePointShaded(
            SoftwareFramebuffer& fb, const RasterDepthState& depthState,
            const RasterStencilState& stencilState, const SoftwareBlendState& blendState,
            const std::array<float, 4>& blendFactor, const GpuDrawParams& params,
            const RasterClipRect& clip, const RasterVertex& point, int colorWriteMask,
            unsigned int multiSampleMask, const SoftwareSamplerState& sampler0,
            const SoftwareSamplerState& sampler1,
            SoftwareOcclusionQueryRenderer* occlusionQuery)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
                return;
            const ShadedContext ctx = MakeLinearShadedContext(
                params, depthState, stencilState, blendState, blendFactor, colorWriteMask,
                multiSampleMask, sampler0, sampler1, occlusionQuery);
            WriteShadedFragment(fb, ctx, clip,
                                static_cast<int>(std::floor(point.x)),
                                static_cast<int>(std::floor(point.y)),
                                point.depth, point.invW,
                                point.r, point.g, point.b, point.a,
                                point.u, point.v, point.u1, point.v1,
                                point.fogKeep,
                                point.sr, point.sg, point.sb,
                                point.envx, point.envy, point.envz, point.envBlend,
                                point.wpx, point.wpy, point.wpz,
                                point.nx, point.ny, point.nz);
        }

        /// General-purpose triangle fill for the DrawPrimitivesEx/DrawIndexedPrimitivesEx and
        /// SpriteBatch paths: adds nearest-neighbor texture sampling, diffuseColor modulation, and
        /// the complete XNA BlendState equation on top of RasterizeTriangle's
        /// depth-tested, perspective-correct color interpolation. Backface culling per `cullMode`
        /// (SOFTWARE-81; raw ordinal, see ShouldCullTriangle()). `params.dualTexture`/`envMapping`
        /// (SOFTWARE-82) select DualTextureEffect's second-texture blend or EnvironmentMapEffect's
        /// cube-map reflection on top of the same base texture/diffuse/vertex-color path.
        /// SOFTWARE-113..115 add FNA-accurate BasicEffect, EnvironmentMapEffect and
        /// SkinnedEffect lighting; SOFTWARE-116 adds the independent second texture coordinate.
        void RasterizeTriangleShaded(SoftwareFramebuffer& fb, const RasterDepthState& depthState,
                                     const RasterStencilState& stencilState,
                                     const SoftwareBlendState& blendState,
                                     const std::array<float, 4>& blendFactor,
                                     int cullMode, float depthBias, float slopeScaleDepthBias,
                                     const GpuDrawParams& params, const RasterClipRect& clip,
                                     const RasterVertex& v0, const RasterVertex& v1, const RasterVertex& v2,
                                     int colorWriteMask, unsigned int multiSampleMask,
                                     const SoftwareSamplerState& sampler0,
                                     const SoftwareSamplerState& sampler1,
                                     SoftwareOcclusionQueryRenderer* occlusionQuery,
                                     bool multiSampleAntiAlias,
                                     bool wireframe = false, unsigned edgeMask = kEdgeAll)
        {
            const float area = EdgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
            if (area == 0.0f)
                return;
            if (ShouldCullTriangle(area, cullMode))
                return;
            const RasterStencilState faceStencil = SelectStencilFace(
                stencilState, area < 0.0f);

            // REMED-GFX-124: the cast target is the colour-storage capability, not a concrete
            // renderer class, so both a SoftwareTextureRenderer and a SoftwareRenderTargetRenderer
            // resolve here. A foreign renderer still resolves to nullptr exactly as before.
            const auto* texture0 = dynamic_cast<const SoftwareColorSurface*>(params.texture0);
            const auto* texture1 = dynamic_cast<const SoftwareColorSurface*>(params.texture1);
#ifndef CNA_SOFTWARE_2D_ONLY
            const auto* envMap = dynamic_cast<const SoftwareCubeSurface*>(params.envMap);
#else
            const SoftwareCubeSurface* envMap = nullptr;
#endif
            const bool useDualTexture = params.dualTexture;
#ifndef CNA_SOFTWARE_2D_ONLY
            const bool useEnvMap = params.envMapping;
#else
            constexpr bool useEnvMap = false;
#endif
            const bool needUV = useDualTexture || useEnvMap || params.textureEnabled;
            // REMED-GFX-150: classify magnification once per triangle per bound texture. Only XNA
            // filters 5..8 distinguish the two halves, so this is inert for Point, Linear and
            // Anisotropic; it is computed only when a texture is actually sampled.
            // REMED-GFX-175: one texel rate per bound texture, resolved once per triangle, feeding
            // BOTH the magnification classification REMED-GFX-150 established and the level-of-detail
            // the mip component needs. The two can never disagree because they come from one number.
            const TextureFootprint footprint0 = (texture0 != nullptr)
                ? TriangleTextureFootprint(v0, v1, v2,
                                           std::max(1, texture0->ColorWidth()),
                                           std::max(1, texture0->ColorHeight()))
                : TextureFootprint{};
            const TextureFootprint footprint1 = (texture1 != nullptr)
                ? TriangleTextureFootprint(v0, v1, v2,
                                           std::max(1, texture1->ColorWidth()),
                                           std::max(1, texture1->ColorHeight()), true)
                : TextureFootprint{};
            const float rho0 = footprint0.isotropicRate;
            const float rho1 = footprint1.isotropicRate;
            const bool magnify0 = !(rho0 > 1.0f);
            const bool magnify1 = !(rho1 > 1.0f);
            // REMED-GFX-182: the cube's own footprint, resolved once per triangle from the SAME
            // reflection expression the fragment path uses and only when a cube is actually bound.
#ifndef CNA_SOFTWARE_2D_ONLY
            const TextureFootprint footprintCube = useEnvMap && envMap != nullptr
                ? TriangleCubeTextureFootprint(v0, v1, v2,
                                               std::max(1, envMap->CubeSize()))
                : TextureFootprint{};
            const float rhoCube = footprintCube.isotropicRate;
#else
            constexpr float rhoCube = 1.0f;
            const TextureFootprint footprintCube{};
#endif
            const ShadedContext ctx{params, texture0, texture1, envMap, useDualTexture, useEnvMap,
                                    needUV, blendState, blendFactor,
                                    depthState, faceStencil, colorWriteMask, multiSampleMask,
                                    occlusionQuery, sampler0, sampler1, magnify0, magnify1,
                                    LodFromTexelRate(rho0), LodFromTexelRate(rho1),
                                    footprint0, footprint1,
                                    !(rhoCube > 1.0f), LodFromTexelRate(rhoCube), footprintCube};

            if (g_samplerTrace.enabled) ++g_samplerTrace.triangles;
            // REMED-GFX-182: stamp BOTH captured slot descriptions, so the cube trace can print the
            // public state this draw carried beside the description its cube sample really ran under.
#ifndef CNA_SOFTWARE_2D_ONLY
            if (g_cubeTrace.enabled)
            {
                g_cubeTrace.slot0Filter = sampler0.filter;
                g_cubeTrace.slot0AddrU = sampler0.addressU;
                g_cubeTrace.slot0AddrV = sampler0.addressV;
                g_cubeTrace.slot1Filter = sampler1.filter;
                g_cubeTrace.slot1AddrU = sampler1.addressU;
                g_cubeTrace.slot1AddrV = sampler1.addressV;
            }
#endif

            // REMED-GFX-083: one polygon-offset value for the whole triangle (after culling). hasBias is
            // 0 for the common zero-bias case, so the depth expressions below stay byte-identical then.
            const float biasOffset = ComputeDepthBiasOffset(v0, v1, v2, depthBias, slopeScaleDepthBias);
            const bool hasBias = (biasOffset != 0.0f);

            if (wireframe)
            {
                // REMED-GFX-082: rasterize the selected edges as perspective-correct shaded lines,
                // reusing WriteShadedFragment (identical texture/diffuse/env-map/blend + depth path).
                const auto drawEdge = [&](const RasterVertex& A, const RasterVertex& B) {
                    WalkRasterLine(fb, multiSampleAntiAlias, clip, A, B,
                                   [&](int x, int y, float t, unsigned int coverageMask,
                                       const std::array<float, 4>* sampleTs) {
                        const float invW  = A.invW  + t * (B.invW  - A.invW);
                        float depth = A.depth + t * (B.depth - A.depth);
                        if (hasBias) depth = std::clamp(depth + biasOffset, 0.0f, 1.0f);  // REMED-GFX-083
                        std::array<float, 4> sampleDepths{};
                        if (sampleTs != nullptr)
                        {
                            for (int sample = 0; sample < 4; ++sample)
                            {
                                float sampleDepth = A.depth +
                                    (*sampleTs)[static_cast<std::size_t>(sample)] *
                                    (B.depth - A.depth);
                                if (hasBias)
                                    sampleDepth = std::clamp(sampleDepth + biasOffset, 0.0f, 1.0f);
                                sampleDepths[static_cast<std::size_t>(sample)] = sampleDepth;
                            }
                        }
                        WriteShadedFragment(fb, ctx, clip, x, y, depth, invW,
                                            A.r + t * (B.r - A.r), A.g + t * (B.g - A.g),
                                            A.b + t * (B.b - A.b), A.a + t * (B.a - A.a),
                                            A.u + t * (B.u - A.u), A.v + t * (B.v - A.v),
                                            A.u1 + t * (B.u1 - A.u1),
                                            A.v1 + t * (B.v1 - A.v1),
                                            A.fogKeep + t * (B.fogKeep - A.fogKeep),
                                            A.sr + t * (B.sr - A.sr),
                                            A.sg + t * (B.sg - A.sg),
                                            A.sb + t * (B.sb - A.sb),
                                            A.envx + t * (B.envx - A.envx),
                                            A.envy + t * (B.envy - A.envy),
                                            A.envz + t * (B.envz - A.envz),
                                            A.envBlend + t * (B.envBlend - A.envBlend),
                                            A.wpx + t * (B.wpx - A.wpx), A.wpy + t * (B.wpy - A.wpy),
                                            A.wpz + t * (B.wpz - A.wpz), A.nx + t * (B.nx - A.nx),
                                            A.ny + t * (B.ny - A.ny), A.nz + t * (B.nz - A.nz),
                                            coverageMask,
                                            sampleTs != nullptr ? &sampleDepths : nullptr);
                    });
                };
                if (edgeMask & kEdgeV0V1) drawEdge(v0, v1);
                if (edgeMask & kEdgeV1V2) drawEdge(v1, v2);
                if (edgeMask & kEdgeV2V0) drawEdge(v2, v0);
                return;
            }

            const float minXf = std::min({v0.x, v1.x, v2.x});
            const float maxXf = std::max({v0.x, v1.x, v2.x});
            const float minYf = std::min({v0.y, v1.y, v2.y});
            const float maxYf = std::max({v0.y, v1.y, v2.y});

            // REMED-GFX-073: clamp the raster bounding box to the clip rectangle (framebuffer for
            // the 3D path, framebuffer-intersected Viewport for SpriteBatch) instead of the raw
            // framebuffer -- pixels outside the Viewport are never touched.
            int minX = 0, minY = 0, maxX = -1, maxY = -1;
            if (!CalculateRasterBounds(minXf, minYf, maxXf, maxYf, clip,
                                       minX, minY, maxX, maxY))
                return;

            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;

                    const float w0 = EdgeFunction(v1.x, v1.y, v2.x, v2.y, px, py);
                    const float w1 = EdgeFunction(v2.x, v2.y, v0.x, v0.y, px, py);
                    const float w2 = EdgeFunction(v0.x, v0.y, v1.x, v1.y, px, py);

                    unsigned int coverageMask = 1u;
                    std::array<float, 4> sampleDepths{};
                    if (!fb.HasMultiSampleColor())
                    {
                        if (!TriangleContainsSample(v0, v1, v2, area, w0, w1, w2))
                            continue;
                    }
                    else if (!multiSampleAntiAlias)
                    {
                        if (!TriangleContainsSample(v0, v1, v2, area, w0, w1, w2))
                            continue;
                        coverageMask = 0xFu;
                    }
                    else
                    {
                        // SOFTWARE-319: evaluate the canonical standard 4x positions. A partially
                        // covered edge blends only its covered samples and ResolveColor() averages
                        // them before GDI blits.
                        coverageMask = 0u;
                        for (int sample = 0; sample < 4; ++sample)
                        {
                            const auto& samplePosition =
                                kStandardFourSamplePositions[static_cast<std::size_t>(sample)];
                            const float sampleX = static_cast<float>(x) +
                                samplePosition.x;
                            const float sampleY = static_cast<float>(y) + samplePosition.y;
                            const float sampleW0 = EdgeFunction(v1.x, v1.y, v2.x, v2.y,
                                                                sampleX, sampleY);
                            const float sampleW1 = EdgeFunction(v2.x, v2.y, v0.x, v0.y,
                                                                sampleX, sampleY);
                            const float sampleW2 = EdgeFunction(v0.x, v0.y, v1.x, v1.y,
                                                                sampleX, sampleY);
                            if (TriangleContainsSample(v0, v1, v2, area,
                                                       sampleW0, sampleW1, sampleW2))
                            {
                                coverageMask |= 1u << sample;
                                float sampleDepth = (sampleW0 * v0.depth +
                                                     sampleW1 * v1.depth +
                                                     sampleW2 * v2.depth) / area;
                                if (hasBias)
                                    sampleDepth = std::clamp(sampleDepth + biasOffset,
                                                             0.0f, 1.0f);
                                sampleDepths[static_cast<std::size_t>(sample)] = sampleDepth;
                            }
                        }
                        if (coverageMask == 0u)
                            continue;
                    }

                    const float lambda0 = w0 / area;
                    const float lambda1 = w1 / area;

                    float depth = BarycentricInterpolate(
                        v0.depth, v1.depth, v2.depth, lambda0, lambda1);
                    if (hasBias) depth = std::clamp(depth + biasOffset, 0.0f, 1.0f);  // REMED-GFX-083
                    if (fb.HasMultiSampleColor() && !multiSampleAntiAlias)
                        sampleDepths.fill(depth);
                    const float invW = BarycentricInterpolate(
                        v0.invW, v1.invW, v2.invW, lambda0, lambda1);
                    WriteShadedFragment(fb, ctx, clip, x, y, depth, invW,
                                        BarycentricInterpolate(v0.r, v1.r, v2.r, lambda0, lambda1),
                                        BarycentricInterpolate(v0.g, v1.g, v2.g, lambda0, lambda1),
                                        BarycentricInterpolate(v0.b, v1.b, v2.b, lambda0, lambda1),
                                        BarycentricInterpolate(v0.a, v1.a, v2.a, lambda0, lambda1),
                                        BarycentricInterpolate(v0.u, v1.u, v2.u, lambda0, lambda1),
                                        BarycentricInterpolate(v0.v, v1.v, v2.v, lambda0, lambda1),
                                        BarycentricInterpolate(v0.u1, v1.u1, v2.u1, lambda0, lambda1),
                                        BarycentricInterpolate(v0.v1, v1.v1, v2.v1, lambda0, lambda1),
                                        BarycentricInterpolate(
                                            v0.fogKeep, v1.fogKeep, v2.fogKeep, lambda0, lambda1),
                                        BarycentricInterpolate(v0.sr, v1.sr, v2.sr, lambda0, lambda1),
                                        BarycentricInterpolate(v0.sg, v1.sg, v2.sg, lambda0, lambda1),
                                        BarycentricInterpolate(v0.sb, v1.sb, v2.sb, lambda0, lambda1),
                                        BarycentricInterpolate(v0.envx, v1.envx, v2.envx, lambda0, lambda1),
                                        BarycentricInterpolate(v0.envy, v1.envy, v2.envy, lambda0, lambda1),
                                        BarycentricInterpolate(v0.envz, v1.envz, v2.envz, lambda0, lambda1),
                                        BarycentricInterpolate(
                                            v0.envBlend, v1.envBlend, v2.envBlend, lambda0, lambda1),
                                        BarycentricInterpolate(v0.wpx, v1.wpx, v2.wpx, lambda0, lambda1),
                                        BarycentricInterpolate(v0.wpy, v1.wpy, v2.wpy, lambda0, lambda1),
                                        BarycentricInterpolate(v0.wpz, v1.wpz, v2.wpz, lambda0, lambda1),
                                        BarycentricInterpolate(v0.nx, v1.nx, v2.nx, lambda0, lambda1),
                                        BarycentricInterpolate(v0.ny, v1.ny, v2.ny, lambda0, lambda1),
                                        BarycentricInterpolate(v0.nz, v1.nz, v2.nz, lambda0, lambda1),
                                        coverageMask,
                                        fb.HasMultiSampleColor() ? &sampleDepths : nullptr);
                }
            }
        }

#ifndef CNA_SOFTWARE_2D_ONLY
        // ---- REMED-GFX-110 / SOFTWARE-322: indexed addressing and safe fallback bounds ----
        //
        // Shared by the strict renderer-contract/legacy fallback paths so they cannot drift apart.
        // The address equation reconciled by REMED-GFX-106 is:
        //
        //   consumed element  = startIndex + localIndex          (an ELEMENT offset, never bytes)
        //   decoded index     = 16- or 32-bit value at that element, per the buffer's own width
        //   fetched vertex    = decoded index + baseVertex       (added exactly once)
        //
        // minVertexIndex/numVertices are native range hints: they never add to a decoded index,
        // never replace startIndex, and never narrow the vertices an index legitimately reaches.

        /// Exact topology-derived consumed element count, computed in 64-bit so an extreme
        /// primitiveCount cannot wrap. Returns -1 for an unrecognized topology. XNA uses the same
        /// formula for both kinds of draw, so this counts index elements for the indexed paths and
        /// vertex elements for the non-indexed ones.
        std::int64_t PrimitiveElementCount(PrimitiveType primitive, int primitiveCount)
        {
            const std::int64_t count = primitiveCount;
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return count * 3;
                case PrimitiveType::TriangleStrip: return count + 2;
                case PrimitiveType::LineList:      return count * 2;
                case PrimitiveType::LineStrip:     return count + 1;
                case PrimitiveType::PointListEXT:  return count;
                default:                           return -1;
            }
        }

        /// Returns the local vertex/index element for one corner of one triangle. XNA triangle
        /// strips reverse their first two vertices on every odd primitive so every assembled
        /// triangle keeps the strip's declared front-face winding.
        std::int64_t TriangleElementOffset(PrimitiveType primitive, int triangle, int corner)
        {
            if (primitive == PrimitiveType::TriangleStrip)
            {
                if ((triangle & 1) != 0 && corner < 2)
                    return static_cast<std::int64_t>(triangle) + (1 - corner);
                return static_cast<std::int64_t>(triangle) + corner;
            }
            return static_cast<std::int64_t>(triangle) * 3 + corner;
        }

        /// Reads one index element at its own declared width. `element` is an element ordinal,
        /// never a byte offset -- the byte position is derived from the width here and nowhere else.
        std::uint32_t DecodeIndexElement(const std::uint8_t* indexBase, bool thirtyTwoBit,
                                         std::int64_t element)
        {
            if (thirtyTwoBit)
            {
                std::uint32_t value;
                std::memcpy(&value,
                            indexBase + static_cast<std::size_t>(element) * sizeof(std::uint32_t),
                            sizeof(std::uint32_t));
                return value;
            }
            std::uint16_t value;
            std::memcpy(&value,
                        indexBase + static_cast<std::size_t>(element) * sizeof(std::uint16_t),
                        sizeof(std::uint16_t));
            return value;
        }

        void ValidateIndexedAddressing(const std::uint8_t* indexBase, bool thirtyTwoBit,
                                       int availableIndexCount, int availableVertexCount,
                                       std::int64_t consumedIndexCount, int startIndex,
                                       int baseVertex)
        {
            if (startIndex < 0)
            {
                throw System::ArgumentOutOfRangeException(
                    "startIndex", std::to_string(startIndex),
                    "startIndex must not be negative.");
            }
            if (startIndex > availableIndexCount ||
                consumedIndexCount > static_cast<std::int64_t>(availableIndexCount) - startIndex)
            {
                throw System::ArgumentOutOfRangeException(
                    "startIndex", std::to_string(startIndex),
                    "The requested primitive range exceeds the bound index buffer.");
            }
            for (std::int64_t local = 0; local < consumedIndexCount; ++local)
            {
                const std::int64_t vertexIndex =
                    static_cast<std::int64_t>(
                        DecodeIndexElement(indexBase, thirtyTwoBit, startIndex + local)) +
                    baseVertex;
                if (vertexIndex < 0 || vertexIndex >= availableVertexCount)
                {
                    throw System::ArgumentOutOfRangeException(
                        "baseVertex", std::to_string(baseVertex),
                        "Index element " + std::to_string(startIndex + local) +
                            " plus baseVertex addresses vertex " + std::to_string(vertexIndex) +
                            ", outside the bound vertex buffer of " +
                            std::to_string(availableVertexCount) + " vertices.");
                }
            }
        }

        // ---- REMED-GFX-119: non-indexed addressing and bounds ----
        //
        // The non-indexed counterpart of the block above, shared by both CPU non-indexed raster
        // paths so they cannot drift apart either. The public contract is:
        //
        //   fetched vertex = vertexStart + localVertex           (an ELEMENT offset, never bytes)
        //   localVertex    in [0, PrimitiveElementCount(primitive, primitiveCount))
        //
        // DrawColoredPrimitives carries no GpuDrawParams and therefore no vertexStart: its own
        // contract is a complete-buffer draw from element zero, because its only caller has already
        // copied exactly the requested source range into the temporary buffer it binds. Applying an
        // offset there as well would consume that range twice.

        /// Protects the renderer-contract/empty-declaration compatibility path before it reads raw
        /// host storage. Classic declared public draws instead follow XNA native forwarding and
        /// make each individual fetch safe in their declaration-driven reader.
        void ValidateNonIndexedAddressing(int availableVertexCount,
                                          std::int64_t consumedVertexCount, int vertexStart)
        {
            // This legacy fallback has no per-stream bounds metadata, so it remains strict.
            if (vertexStart < 0)
            {
                throw System::ArgumentOutOfRangeException(
                    "vertexStart", std::to_string(vertexStart),
                    "vertexStart must not be negative.");
            }
            if (consumedVertexCount < 0)
            {
                throw System::ArgumentOutOfRangeException(
                    "primitiveCount", std::to_string(consumedVertexCount),
                    "The requested primitive range is too large.");
            }
            if (vertexStart > availableVertexCount ||
                consumedVertexCount >
                    static_cast<std::int64_t>(availableVertexCount) - vertexStart)
            {
                throw System::ArgumentOutOfRangeException(
                    "vertexStart", std::to_string(vertexStart),
                    "The requested primitive range consumes " +
                        std::to_string(consumedVertexCount) + " vertices from element " +
                        std::to_string(vertexStart) +
                        ", outside the bound vertex buffer of " +
                        std::to_string(availableVertexCount) + " vertices.");
            }
        }
#endif
    }

    // ---- SoftwareVertexBufferRenderer ----

#ifndef CNA_SOFTWARE_2D_ONLY

    SoftwareVertexBufferRenderer::SoftwareVertexBufferRenderer(int vertexCapacity)
        : capacity_(vertexCapacity)
    {
    }

    void SoftwareVertexBufferRenderer::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        if (vertex_count < 0 || vertex_count > capacity_)
            throw std::runtime_error("SoftwareVertexBufferRenderer::SetData: vertex_count exceeds capacity");
        if (stride_in_bytes == 0)
            throw std::runtime_error("SoftwareVertexBufferRenderer::SetData: stride_in_bytes must be > 0");

        vertexCount_ = vertex_count;
        stride_ = stride_in_bytes;
        const std::size_t byteCount = static_cast<std::size_t>(vertex_count) * stride_in_bytes;
        data_.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + byteCount);
    }

    void SoftwareVertexBufferRenderer::SetDataWithOptions(const void* data, int vertex_count,
                                                         std::size_t stride_in_bytes, SetDataOptions)
    {
        SetData(data, vertex_count, stride_in_bytes);
    }

    // ---- SoftwareIndexBufferRenderer ----

    SoftwareIndexBufferRenderer::SoftwareIndexBufferRenderer(int indexCapacity, bool thirtyTwoBit)
        : capacity_(indexCapacity), thirtyTwoBit_(thirtyTwoBit)
    {
    }

    void SoftwareIndexBufferRenderer::Upload(const void* data, int index_count, bool dataIsThirtyTwoBit)
    {
        if (index_count < 0 || index_count > capacity_)
            throw std::runtime_error("SoftwareIndexBufferRenderer: index_count exceeds capacity");
        if (dataIsThirtyTwoBit != thirtyTwoBit_)
            throw std::runtime_error("SoftwareIndexBufferRenderer: SetData bit-width does not match the buffer's declared width");

        indexCount_ = index_count;
        const std::size_t elementSize = dataIsThirtyTwoBit ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const std::size_t byteCount = static_cast<std::size_t>(index_count) * elementSize;
        data_.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + byteCount);
    }

    void SoftwareIndexBufferRenderer::SetData16(const void* data, int index_count) { Upload(data, index_count, false); }
    void SoftwareIndexBufferRenderer::SetData32(const void* data, int index_count) { Upload(data, index_count, true); }
    void SoftwareIndexBufferRenderer::SetData16WithOptions(const void* data, int index_count, SetDataOptions)
    { Upload(data, index_count, false); }
    void SoftwareIndexBufferRenderer::SetData32WithOptions(const void* data, int index_count, SetDataOptions)
    { Upload(data, index_count, true); }

#endif

    // ---- SoftwareTextureCubeRenderer (SOFTWARE-82) ----

#ifndef CNA_SOFTWARE_2D_ONLY

    // REMED-GFX-135: mirrors TextureCube.cpp's CalculateMipLevels(size,size) -- cube faces are
    // square, so the chain length is driven by a single edge.
    static int CalculateCubeMipLevels(int size)
    {
        int levels = 1;
        int s = size;
        while (s > 1) { s = std::max(1, s / 2); ++levels; }
        return levels;
    }

    int SoftwareTextureCubeRenderer::LevelDim(int level) const
    {
        return std::max(1, size_ >> level);
    }

    SoftwareTextureCubeRenderer::SoftwareTextureCubeRenderer(
        int size, bool mipMap, int surfaceFormat)
        : size_(size)
        , surfaceFormat_(surfaceFormat)
        , levelCount_(mipMap ? CalculateCubeMipLevels(size) : 1)
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const auto format = static_cast<SurfaceFormat>(surfaceFormat_);
        const bool compressed = format == SurfaceFormat::Dxt1 ||
                                format == SurfaceFormat::Dxt3 ||
                                format == SurfaceFormat::Dxt5;
        levels_.resize(static_cast<std::size_t>(levelCount_));
        sampleLevels_.resize(static_cast<std::size_t>(levelCount_));
        if (compressed)
            compressedLevels_.resize(static_cast<std::size_t>(levelCount_));
        else
            rawLevels_.resize(static_cast<std::size_t>(levelCount_));
        supplied_.assign(static_cast<std::size_t>(levelCount_), std::array<bool, 6>{});
        for (int level = 0; level < levelCount_; ++level)
        {
            const int dim = LevelDim(level);
            const std::size_t faceBytes = static_cast<std::size_t>(dim) * static_cast<std::size_t>(dim) * 4u;
            for (int face = 0; face < 6; ++face)
            {
                levels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)]
                    .assign(faceBytes, 0u);
                sampleLevels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)]
                    .assign(faceBytes, 0.0f);
                if (!compressed)
                {
                    auto& raw = rawLevels_[static_cast<std::size_t>(level)]
                                         [static_cast<std::size_t>(face)];
                    raw.assign(SoftwareTextureFormat::RawByteCount(surfaceFormat_, dim, dim), 0u);
                    SoftwareTextureFormat::DecodePixels(
                        surfaceFormat_, raw.data(), raw.size(),
                        dim * SoftwareTextureFormat::BytesPerTexel(surfaceFormat_), dim, dim,
                        levels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)],
                        sampleLevels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)]);
                }
            }
            if (compressed)
            {
                const std::size_t blockBytes = format == SurfaceFormat::Dxt1 ? 8u : 16u;
                const std::size_t compressedBytes =
                    static_cast<std::size_t>((dim + 3) / 4) *
                    static_cast<std::size_t>((dim + 3) / 4) * blockBytes;
                for (auto& face : compressedLevels_[static_cast<std::size_t>(level)])
                    face.assign(compressedBytes, 0u);
            }
        }
    }

    // REMED-GFX-182: level 0 counts as supplied from construction (its storage is what every
    // pre-mip consumer has always read), so a cube nobody wrote still samples exactly as it did.
    int SoftwareTextureCubeRenderer::FaceLevelCount(int face) const
    {
        if (face < 0 || face > 5) return 1;
        return faceLevels_[static_cast<std::size_t>(face)];
    }

    const std::vector<std::uint8_t>& SoftwareTextureCubeRenderer::FacePixels(int face, int level) const
    {
        const int f = (face < 0 || face > 5) ? 0 : face;
        const int l = (level < 0 || level >= levelCount_) ? 0 : level;
        return levels_[static_cast<std::size_t>(l)][static_cast<std::size_t>(f)];
    }

    bool SoftwareTextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                             const void* data, int dataLength)
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const auto format = static_cast<SurfaceFormat>(surfaceFormat_);
        if (format != SurfaceFormat::Color)
            return false;
        return SetDataBytesEXT(face, level, x, y, w, h, data, dataLength);
    }

    bool SoftwareTextureCubeRenderer::SetDataBytesEXT(
        int face, int level, int x, int y, int w, int h,
        const void* data, int dataLength)
    {
        if (SoftwareTextureFormat::IsDxt(surfaceFormat_)) return false;
        // REMED-GFX-135: `level != 0` used to be a silent early `return` -- the shared layer had no
        // way to tell that apart from a completed upload, so a mipmapped cube accepted every level
        // and kept only level 0. Every level TextureCube declares now has real storage, and
        // anything outside the chain is refused rather than swallowed.
        if (data == nullptr || face < 0 || face > 5) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int dim = LevelDim(level);
        if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > dim || y + h > dim) return false;
        const int bytesPerTexel = SoftwareTextureFormat::BytesPerTexel(surfaceFormat_);
        if (dataLength < w * h * bytesPerTexel) return false;

        const auto* src = static_cast<const std::uint8_t*>(data);
        std::vector<std::uint8_t>& raw =
            rawLevels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)];
        const std::size_t rowBytes =
            static_cast<std::size_t>(w) * static_cast<std::size_t>(bytesPerTexel);
        for (int row = 0; row < h; ++row)
        {
            const std::size_t dstOffset = (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(dim) +
                                          static_cast<std::size_t>(x)) *
                                          static_cast<std::size_t>(bytesPerTexel);
            std::copy(src + static_cast<std::size_t>(row) * rowBytes,
                     src + static_cast<std::size_t>(row) * rowBytes + rowBytes,
                     raw.begin() + static_cast<std::ptrdiff_t>(dstOffset));
        }
        SoftwareTextureFormat::DecodePixels(
            surfaceFormat_, raw.data(), raw.size(), dim * bytesPerTexel, dim, dim,
            levels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)],
            sampleLevels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)]);

        // REMED-GFX-182: a level becomes selectable only once the FULL face rectangle at that level
        // has been written -- a partial upload leaves the rest of the level at the construction
        // zero-fill, and letting the sampler minify into that would fade a draw into transparent
        // black nobody supplied. Recounted from level 1 upward per face, exactly as
        // SoftwareTextureRenderer::UpdatePixelsLevel recounts its own chain: a level only counts once
        // every level below it is present, so a chain written out of order bounds the sampler at the
        // last contiguous level that really exists instead of exposing a gap.
        if (x == 0 && y == 0 && w == dim && h == dim)
        {
            supplied_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)] = true;
            int contiguous = 1;
            for (int l = 1; l < levelCount_; ++l)
            {
                if (!supplied_[static_cast<std::size_t>(l)][static_cast<std::size_t>(face)]) break;
                ++contiguous;
            }
            faceLevels_[static_cast<std::size_t>(face)] = contiguous;
        }
        return true;
    }

    bool SoftwareTextureCubeRenderer::SetCompressedDataEXT(
        int face, int level, int x, int y, int w, int h,
        const void* data, int dataLength)
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const auto format = static_cast<SurfaceFormat>(surfaceFormat_);
        if (format != SurfaceFormat::Dxt1 && format != SurfaceFormat::Dxt3 &&
            format != SurfaceFormat::Dxt5)
            return false;
        if (data == nullptr || face < 0 || face > 5 || level < 0 || level >= levelCount_ ||
            w <= 0 || h <= 0)
            return false;

        const int dim = LevelDim(level);
        if (x < 0 || y < 0 || w > dim || h > dim || x > dim - w || y > dim - h ||
            (x % 4) != 0 || (y % 4) != 0 ||
            ((w % 4) != 0 && x + w != dim) ||
            ((h % 4) != 0 && y + h != dim))
            return false;

        const std::size_t blockBytes = format == SurfaceFormat::Dxt1 ? 8u : 16u;
        const int levelBlockColumns = (dim + 3) / 4;
        const int regionBlockColumns = (w + 3) / 4;
        const int regionBlockRows = (h + 3) / 4;
        const std::size_t required = static_cast<std::size_t>(regionBlockColumns) *
                                     static_cast<std::size_t>(regionBlockRows) * blockBytes;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
            return false;

        std::vector<std::uint8_t>& blocks =
            compressedLevels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)];
        std::vector<std::uint8_t> replacement = blocks;
        const auto* source = static_cast<const std::uint8_t*>(data);
        for (int row = 0; row < regionBlockRows; ++row)
        {
            const std::size_t destinationOffset =
                (static_cast<std::size_t>(y / 4 + row) *
                     static_cast<std::size_t>(levelBlockColumns) +
                 static_cast<std::size_t>(x / 4)) * blockBytes;
            const std::size_t sourceOffset =
                static_cast<std::size_t>(row) *
                static_cast<std::size_t>(regionBlockColumns) * blockBytes;
            std::copy_n(source + sourceOffset,
                        static_cast<std::size_t>(regionBlockColumns) * blockBytes,
                        replacement.begin() + static_cast<std::ptrdiff_t>(destinationOffset));
        }

        blocks = std::move(replacement);
        SoftwareTextureFormat::DecodePixels(
            surfaceFormat_, blocks.data(), blocks.size(), 0, dim, dim,
            levels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)],
            sampleLevels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)]);

        if (x == 0 && y == 0 && w == dim && h == dim)
        {
            supplied_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)] = true;
            int contiguous = 1;
            for (int candidate = 1; candidate < levelCount_; ++candidate)
            {
                if (!supplied_[static_cast<std::size_t>(candidate)]
                              [static_cast<std::size_t>(face)])
                    break;
                ++contiguous;
            }
            faceLevels_[static_cast<std::size_t>(face)] = contiguous;
        }
        return true;
    }

    bool SoftwareTextureCubeRenderer::GetCompressedDataEXT(
        int face, int level, int x, int y, int w, int h,
        void* data, int dataLength) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const auto format = static_cast<SurfaceFormat>(surfaceFormat_);
        if ((format != SurfaceFormat::Dxt1 && format != SurfaceFormat::Dxt3 &&
             format != SurfaceFormat::Dxt5) ||
            data == nullptr || face < 0 || face > 5 || level < 0 || level >= levelCount_ ||
            w <= 0 || h <= 0)
            return false;

        const int dim = LevelDim(level);
        if (x < 0 || y < 0 || w > dim || h > dim || x > dim - w || y > dim - h ||
            (x % 4) != 0 || (y % 4) != 0 ||
            ((w % 4) != 0 && x + w != dim) ||
            ((h % 4) != 0 && y + h != dim))
            return false;

        const std::size_t blockBytes = format == SurfaceFormat::Dxt1 ? 8u : 16u;
        const int levelBlockColumns = (dim + 3) / 4;
        const int regionBlockColumns = (w + 3) / 4;
        const int regionBlockRows = (h + 3) / 4;
        const std::size_t required = static_cast<std::size_t>(regionBlockColumns) *
                                     static_cast<std::size_t>(regionBlockRows) * blockBytes;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
            return false;

        const auto& blocks =
            compressedLevels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)];
        auto* destination = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < regionBlockRows; ++row)
        {
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(y / 4 + row) *
                     static_cast<std::size_t>(levelBlockColumns) +
                 static_cast<std::size_t>(x / 4)) * blockBytes;
            const std::size_t destinationOffset =
                static_cast<std::size_t>(row) *
                static_cast<std::size_t>(regionBlockColumns) * blockBytes;
            std::copy_n(blocks.data() + sourceOffset,
                        static_cast<std::size_t>(regionBlockColumns) * blockBytes,
                        destination + destinationOffset);
        }
        return true;
    }

    bool SoftwareTextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                             void* data, int dataLength) const
    {
        // REMED-GFX-130: every rejection below used to be a silent `return`, which the shared layer
        // turned into a complete transparent-black face rather than a refusal.
        if (data == nullptr || face < 0 || face > 5)
            return false;
        if (level < 0 || level >= levelCount_)
            return false;
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const auto format = static_cast<SurfaceFormat>(surfaceFormat_);
        if (format != SurfaceFormat::Color &&
            format != SurfaceFormat::Dxt1 && format != SurfaceFormat::Dxt3 &&
            format != SurfaceFormat::Dxt5)
            return false;
        const int dim = LevelDim(level);
        if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > dim || y + h > dim)
            return false;
        if (dataLength < w * h * 4)
            return false;
        auto* dst = static_cast<std::uint8_t*>(data);
        const std::vector<std::uint8_t>& pixels =
            levels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)];
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        for (int row = 0; row < h; ++row)
        {
            const std::size_t srcOffset = (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(dim) +
                                          static_cast<std::size_t>(x)) * 4u;
            std::copy(pixels.begin() + static_cast<std::ptrdiff_t>(srcOffset),
                     pixels.begin() + static_cast<std::ptrdiff_t>(srcOffset) + static_cast<std::ptrdiff_t>(rowBytes),
                     dst + static_cast<std::size_t>(row) * rowBytes);
        }
        return true;
    }

    bool SoftwareTextureCubeRenderer::GetDataBytesEXT(
        int face, int level, int x, int y, int w, int h,
        void* data, int dataLength) const
    {
        if (SoftwareTextureFormat::IsDxt(surfaceFormat_) || data == nullptr ||
            face < 0 || face > 5 || level < 0 || level >= levelCount_)
            return false;
        const int dim = LevelDim(level);
        if (w <= 0 || h <= 0 || x < 0 || y < 0 || w > dim || h > dim ||
            x > dim - w || y > dim - h)
            return false;
        const int bytesPerTexel = SoftwareTextureFormat::BytesPerTexel(surfaceFormat_);
        if (dataLength < w * h * bytesPerTexel) return false;

        auto* destination = static_cast<std::uint8_t*>(data);
        const auto& raw = rawLevels_[static_cast<std::size_t>(level)]
                                   [static_cast<std::size_t>(face)];
        const std::size_t rowBytes =
            static_cast<std::size_t>(w) * static_cast<std::size_t>(bytesPerTexel);
        for (int row = 0; row < h; ++row)
        {
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(y + row) * dim + x) *
                static_cast<std::size_t>(bytesPerTexel);
            std::copy_n(raw.data() + sourceOffset, rowBytes,
                        destination + static_cast<std::size_t>(row) * rowBytes);
        }
        return true;
    }

    void SoftwareTextureCubeRenderer::FetchCubeColorTexel(
        int face, int level, int x, int y,
        float& r, float& g, float& b, float& a) const
    {
        const int resolvedFace = (face < 0 || face > 5) ? 0 : face;
        const int resolvedLevel = (level < 0 || level >= levelCount_) ? 0 : level;
        const int dim = LevelDim(resolvedLevel);
        const auto& samples = sampleLevels_[static_cast<std::size_t>(resolvedLevel)]
                                           [static_cast<std::size_t>(resolvedFace)];
        if (samples.size() < static_cast<std::size_t>(dim) * dim * 4u)
        {
            SoftwareCubeSurface::FetchCubeColorTexel(
                resolvedFace, resolvedLevel, x, y, r, g, b, a);
            return;
        }
        const std::size_t offset =
            (static_cast<std::size_t>(y) * dim + x) * 4u;
        r = samples[offset + 0];
        g = samples[offset + 1];
        b = samples[offset + 2];
        a = samples[offset + 3];
    }

#endif

    // ---- SoftwareOcclusionQueryRenderer (SOFTWARE-122) ----

    SoftwareOcclusionQueryRenderer::SoftwareOcclusionQueryRenderer(SoftwareRenderer& owner)
        : owner_(&owner)
    {
    }

    SoftwareOcclusionQueryRenderer::~SoftwareOcclusionQueryRenderer()
    {
        if (active_ && owner_ != nullptr)
            owner_->ReleaseOcclusionQuery(this);
    }

    void SoftwareOcclusionQueryRenderer::Begin()
    {
        // OcclusionQuery owns XNA's public Begin/End state machine. Keep the renderer guard as a
        // defensive native invariant for a different already-active query.
        if (active_ || owner_ == nullptr || !owner_->TryActivateOcclusionQuery(this))
            return;
        pixelCount_ = 0;
        complete_ = false;
        active_ = true;
    }

    void SoftwareOcclusionQueryRenderer::End()
    {
        if (!active_)
            return;
        active_ = false;
        complete_ = true;
        if (owner_ != nullptr)
            owner_->ReleaseOcclusionQuery(this);
    }

    bool SoftwareRenderer::TryActivateOcclusionQuery(SoftwareOcclusionQueryRenderer* query)
    {
        if (activeOcclusionQuery_ != nullptr && activeOcclusionQuery_ != query)
            return false;
        activeOcclusionQuery_ = query;
        return true;
    }

    void SoftwareRenderer::ReleaseOcclusionQuery(SoftwareOcclusionQueryRenderer* query)
    {
        if (activeOcclusionQuery_ == query)
            activeOcclusionQuery_ = nullptr;
    }

    std::unique_ptr<IOcclusionQueryRenderer> SoftwareRenderer::CreateOcclusionQuery()
    {
        return std::make_unique<SoftwareOcclusionQueryRenderer>(*this);
    }

    // ---- SoftwareEffectRenderer ----

#ifndef CNA_SOFTWARE_2D_ONLY

    bool SoftwareEffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        if (vertSrc.empty() && fragSrc.empty())
            throw std::runtime_error("SoftwareEffectRenderer::CompileProgram: both vertSrc and fragSrc are empty");
        compiled_ = true;
        return true;
    }

#endif

    void SoftwareRenderer::RasterizeSpriteQuad(
        const ITextureRenderer& texture,
        const Vector4& c0, const Vector4& c1, const Vector4& c2, const Vector4& c3,
        float r, float g, float b, float a,
        float u1, float v1, float u2, float v2,
        Effect* customEffect, const SoftwareSamplerState& spriteSampler)
    {
        SoftwareFramebuffer& fb = CurrentFramebuffer();
        int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
        float vpMinDepth = 0.0f, vpMaxDepth = 1.0f;
        GetActiveViewportRaster(vpX, vpY, vpW, vpH, vpMinDepth, vpMaxDepth);
        if (vpW <= 0 || vpH <= 0)
            return;
        const ViewportTransform vpT{static_cast<float>(vpX), static_cast<float>(vpY),
                                    static_cast<float>(vpW), static_cast<float>(vpH),
                                    vpMinDepth, vpMaxDepth, fb.multiSampleCount > 1};

        // SOFTWARE-338: apply FNA's inlined SpriteBatch orthographic projection after the caller's
        // full homogeneous transform. This keeps transformed Z/W observable and leaves viewport
        // origin outside the transform. The result uses XNA/D3D's 0 <= Z <= W clip volume.
        const float xScale = 2.0f / static_cast<float>(vpW);
        const float yScale = -2.0f / static_cast<float>(vpH);
        const auto makeClipVertex = [&](const Vector4& position, float u, float v) {
            ClipVertex out;
            out.x = xScale * position.X - position.W;
            out.y = yScale * position.Y + position.W;
            out.z = position.Z;
            out.w = position.W;
            out.r = r; out.g = g; out.b = b; out.a = a;
            out.u = u; out.v = v;
            out.u1 = u; out.v1 = v;
            out.spriteX = position.X;
            out.spriteY = position.Y;
            return out;
        };
        const ClipVertex cv0 = makeClipVertex(c0, u1, v1);
        const ClipVertex cv1 = makeClipVertex(c1, u2, v1);
        const ClipVertex cv2 = makeClipVertex(c2, u2, v2);
        const ClipVertex cv3 = makeClipVertex(c3, u1, v2);

        // REMED-GFX-030: snapshot the complete depth tuple for this submitted sprite draw.
        const RasterDepthState depthState{IsDepthTestEnabled(),
                                          IsDepthWriteEnabled(), GetDepthCompareFunction()};
        const RasterStencilState stencilState{
            IsStencilTestEnabled(), GetStencilCompareFunction(),
            GetStencilPassOperation(), GetStencilFailOperation(),
            GetStencilDepthFailOperation(),
            static_cast<std::uint8_t>(GetStencilReadMask()),
            static_cast<std::uint8_t>(GetStencilWriteMask()),
            static_cast<std::uint8_t>(GetReferenceStencil()),
            IsTwoSidedStencilEnabled(), GetCounterClockwiseStencilCompareFunction(),
            GetCounterClockwiseStencilPassOperation(),
            GetCounterClockwiseStencilFailOperation(),
            GetCounterClockwiseStencilDepthFailOperation()};
        const SoftwareBlendState blendState = GetBlendState();
        const std::array<float, 4> blendFactor = GetBlendFactor();
        const int cullMode = GetCullMode();
        // REMED-GFX-080: effective raster clip = framebuffer ∩ Viewport ∩ (ScissorRectangle when
        // RasterizerState.ScissorTestEnable). The scissor is framebuffer-space, intersected after
        // the viewport clip (not viewport-local); disabled scissor leaves the viewport clip intact.
        int scX = 0, scY = 0, scW = 0, scH = 0;
        GetActiveScissor(scX, scY, scW, scH);
        const RasterClipRect clip = ScissorClip(ViewportClip(fb, vpX, vpY, vpW, vpH),
                                                IsScissorTestEnabled(), scX, scY, scW, scH);
        GpuDrawParams spriteParams;
        // REMED-GFX-124: hand the sprite's texture on as the plain renderer handle and let
        // RasterizeTriangleShaded resolve the colour-storage capability, so this path has no second,
        // narrower notion of "a Software texture" that could reject a finished render target after
        // the shared one already accepted it.
        spriteParams.texture0 = &texture;
        spriteParams.textureEnabled = true;
        // A `ColorMatrixEffect` is the sole custom Effect GDI/SOFTWARE accepts for SpriteBatch.
        // ShaderEffect stays rejected by GDI; every unrelated Effect keeps the regular sprite
        // path rather than being misidentified as a programmable shader.
        if (const auto* colorMatrix =
                dynamic_cast<const Microsoft::Xna::Framework::Graphics::ColorMatrixEffect*>(customEffect))
            colorMatrix->FillSpriteDrawParams(spriteParams);
        // REMED-GFX-082: honor RasterizerState.FillMode for the sprite's two quad triangles too. Each
        // draws ALL THREE of its edges (kEdgeAll), so a wireframe sprite shows its quad outline plus
        // the internal triangle-split diagonal -- real submitted geometry, matching D3D11/FNA (not
        // suppressed like the 3D near-plane clip diagonal). Passed THROUGH SpriteBatch.Begin's
        // RasterizerState via GraphicsDevice (REMED-GFX-081).
        const bool wire = (GetFillMode() == 1);
        // REMED-GFX-083: SpriteBatch's two quad triangles honor RasterizerState.DepthBias /
        // SlopeScaleDepthBias too (a bias supplied through SpriteBatch.Begin's RasterizerState, GFX-081).
        // A quad is flat (constant layerDepth -> zero depth slope), so only the constant term applies.
        const float depthBias = GetDepthBias();
        const float slopeScaleDepthBias = GetSlopeScaleDepthBias();
        // REMED-GFX-150: the sprite quad samples through the SamplerState SpriteBatch.Begin
        // resolved for THIS batch, not the device's 3D slot state -- SpriteBatch has its own sampler
        // channel (SetSamplerFilter/SetSamplerAddressMode), exactly as it does on every GPU renderer.
        // Begin always re-applies it, so it cannot leak in from a previous batch, and passing it
        // here rather than through the device slots means a sprite batch cannot leak it out either.
        float visibleMinX = std::numeric_limits<float>::infinity();
        float visibleMinY = std::numeric_limits<float>::infinity();
        float visibleMaxX = -std::numeric_limits<float>::infinity();
        float visibleMaxY = -std::numeric_limits<float>::infinity();
        bool haveVisibleVertex = false;
        const auto rasterizeClippedTriangle = [&](const ClipVertex& aClip,
                                                   const ClipVertex& bClip,
                                                   const ClipVertex& cClip) {
            const ClipVertex input[3] = {aClip, bClip, cClip};
            std::array<ClipVertex, kMaxClippedTriangleVertices> clipped{};
            const int clippedCount = ClipTriangleToFrustum(input, clipped);
            if (clippedCount == 0)
                return;

            std::array<RasterVertex, kMaxClippedTriangleVertices> vertices{};
            for (int i = 0; i < clippedCount; ++i)
            {
                RasterVertex& vertex = vertices[static_cast<std::size_t>(i)];
                vertex = SpriteClipVertexToRasterVertex(
                    clipped[static_cast<std::size_t>(i)], vpT);
                visibleMinX = std::min(visibleMinX, vertex.x);
                visibleMinY = std::min(visibleMinY, vertex.y);
                visibleMaxX = std::max(visibleMaxX, vertex.x);
                visibleMaxY = std::max(visibleMaxY, vertex.y);
                haveVisibleVertex = true;
            }

            // Fan triangulation retains only the clipped polygon boundary in wireframe. For an
            // unclipped submitted triangle the mask is kEdgeAll, preserving SpriteBatch's visible
            // quad-split diagonal exactly as before this homogeneous route.
            for (int fan = 1; fan + 1 < clippedCount; ++fan)
            {
                unsigned edgeMask = kEdgeV1V2;
                if (fan == 1) edgeMask |= kEdgeV0V1;
                if (fan + 1 == clippedCount - 1) edgeMask |= kEdgeV2V0;
                RasterizeTriangleShaded(
                    fb, depthState, stencilState, blendState, blendFactor,
                    cullMode, depthBias, slopeScaleDepthBias,
                    spriteParams, clip, vertices[0],
                    vertices[static_cast<std::size_t>(fan)],
                    vertices[static_cast<std::size_t>(fan + 1)],
                    GetColorWriteMask(), GetMultiSampleMask(),
                    spriteSampler, spriteSampler, activeOcclusionQuery_,
                    IsMultiSampleAntiAliasEnabled(), wire, edgeMask);
            }
        };

        rasterizeClippedTriangle(cv0, cv1, cv2);
        rasterizeClippedTriangle(cv2, cv3, cv0);

        int damageMinX = 0, damageMinY = 0, damageMaxX = -1, damageMaxY = -1;
        if (haveVisibleVertex &&
            CalculateRasterBounds(visibleMinX, visibleMinY, visibleMaxX, visibleMaxY, clip,
                                  damageMinX, damageMinY, damageMaxX, damageMaxY))
        {
            OnSpriteRasterBounds(damageMinX, damageMinY, damageMaxX, damageMaxY);
        }
    }

    std::unique_ptr<ITexture3DRenderer> SoftwareRenderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
#ifdef CNA_SOFTWARE_2D_ONLY
        (void)w;
        (void)h;
        (void)depth;
        (void)mipMap;
        (void)surfaceFormat;
        throw System::NotSupportedException(
            "Software's GDI 2D compilation unit does not include Texture3D resources.");
#else
        return std::make_unique<SoftwareTexture3DRenderer>(
            w, h, depth, mipMap, surfaceFormat);
#endif
    }

    std::unique_ptr<ITextureCubeRenderer> SoftwareRenderer::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
#ifdef CNA_SOFTWARE_2D_ONLY
        (void)size;
        (void)mipMap;
        (void)surfaceFormat;
        throw System::NotSupportedException(
            "Software's GDI 2D compilation unit does not include TextureCube resources.");
#else
        // REMED-GFX-135: `mipMap` used to be discarded here, so a mipmapped TextureCube reported a
        // LevelCount whose storage did not exist and every mip upload was dropped in silence.
        return std::make_unique<SoftwareTextureCubeRenderer>(size, mipMap, surfaceFormat);
#endif
    }

    std::unique_ptr<IRenderTargetCubeRenderer> SoftwareRenderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
#ifdef CNA_SOFTWARE_2D_ONLY
        (void)size;
        (void)depthFormat;
        (void)preserveContents;
        (void)mipMap;
        (void)multiSampleCount;
        throw System::NotSupportedException(
            "Software's GDI 2D compilation unit does not include RenderTargetCube resources.");
#else
        return std::make_unique<SoftwareRenderTargetCubeRenderer>(
            size, depthFormat, preserveContents, mipMap, multiSampleCount);
#endif
    }

    std::unique_ptr<IRenderTargetCubeRenderer> SoftwareRenderer::CreateRenderTargetCubeEXT(
        int size, int depthFormat, bool preserveContents, bool mipMap,
        int multiSampleCount, int surfaceFormat)
    {
#ifdef CNA_SOFTWARE_2D_ONLY
        (void)size;
        (void)depthFormat;
        (void)preserveContents;
        (void)mipMap;
        (void)multiSampleCount;
        (void)surfaceFormat;
        throw System::NotSupportedException(
            "Software's GDI 2D compilation unit does not include RenderTargetCube resources.");
#else
        if (ClassifyRenderTargetFormatEXT(surfaceFormat) != RendererFormatVerdict::Supported)
            throw std::runtime_error(
                "SoftwareRenderer::CreateRenderTargetCubeEXT: unsupported SurfaceFormat ordinal " +
                std::to_string(surfaceFormat));
        return std::make_unique<SoftwareRenderTargetCubeRenderer>(
            size, depthFormat, preserveContents, mipMap, multiSampleCount, surfaceFormat);
#endif
    }

    std::unique_ptr<IEffectRenderer> SoftwareRenderer::CreateEffectRenderer(const std::string& vertSrc,
                                                                                const std::string& fragSrc)
    {
#ifdef CNA_SOFTWARE_2D_ONLY
        (void)vertSrc;
        (void)fragSrc;
        throw System::NotSupportedException(
            "Software's GDI 2D compilation unit does not include programmable effect resources.");
#else
        auto effect = std::make_unique<SoftwareEffectRenderer>();
        effect->CompileProgram(vertSrc, fragSrc);
        return effect;
#endif
    }

    std::unique_ptr<IVertexBufferRenderer> SoftwareRenderer::CreateVertexBuffer(int vertex_capacity)
    {
#ifdef CNA_SOFTWARE_2D_ONLY
        (void)vertex_capacity;
        throw System::NotSupportedException(
            "Software's GDI 2D compilation unit does not include vertex buffers.");
#else
        return std::make_unique<SoftwareVertexBufferRenderer>(vertex_capacity);
#endif
    }

    std::unique_ptr<IIndexBufferRenderer> SoftwareRenderer::CreateIndexBuffer16(int index_capacity)
    {
#ifdef CNA_SOFTWARE_2D_ONLY
        (void)index_capacity;
        throw System::NotSupportedException(
            "Software's GDI 2D compilation unit does not include index buffers.");
#else
        return std::make_unique<SoftwareIndexBufferRenderer>(index_capacity, false);
#endif
    }

    std::unique_ptr<IIndexBufferRenderer> SoftwareRenderer::CreateIndexBuffer32(int index_capacity)
    {
#ifdef CNA_SOFTWARE_2D_ONLY
        (void)index_capacity;
        throw System::NotSupportedException(
            "Software's GDI 2D compilation unit does not include index buffers.");
#else
        return std::make_unique<SoftwareIndexBufferRenderer>(index_capacity, true);
#endif
    }

    // Phase S4 (SOFTWARE-30..34) plus SOFTWARE-105: real transform/rasterize/depth-test pipeline
    // for triangle lists and strips. Effect-aware paths below additionally support line and point
    // topologies.
#ifndef CNA_SOFTWARE_2D_ONLY
    void SoftwareRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world,
                                                        const Matrix& view, const Matrix& projection,
                                                        PrimitiveType primitive, int primitiveCount)
    {
        if (primitiveCount <= 0)
            throw std::runtime_error("SoftwareRenderer::DrawColoredPrimitives: primitiveCount must be > 0");
        if (primitive != PrimitiveType::TriangleList && primitive != PrimitiveType::TriangleStrip)
            throw std::runtime_error(
                "SoftwareRenderer::DrawColoredPrimitives: unsupported primitive topology");

        // REMED-GFX-119: this entry point carries no GpuDrawParams, so its contract is a complete
        // buffer draw -- first element zero, no offset. It still validates the exact
        // topology-derived range through the shared helper, in 64-bit, so an extreme
        // primitiveCount is rejected instead of wrapping the old `primitiveCount * 3` product.
        constexpr int kVertexStart = 0;
        const std::int64_t consumedVertexCount =
            PrimitiveElementCount(primitive, primitiveCount);
        ValidateNonIndexedAddressing(vb.GetVertexCount(), consumedVertexCount, kVertexStart);

        const auto& swVb = static_cast<const SoftwareVertexBufferRenderer&>(vb);
        const std::uint8_t* base = swVb.Data().data();
        const std::size_t stride = swVb.Stride();

        const Matrix combined = world * view * projection;
        SoftwareFramebuffer& fb = CurrentFramebuffer();
        const RasterDepthState depthState{
            depthTestEnabled_, depthWriteEnabled_, depthCompareFunction_}; // REMED-GFX-030 draw snapshot
        const RasterStencilState stencilState = SnapshotStencilState(*this); // SOFTWARE-121
        // REMED-GFX-079: map NDC over the active GraphicsDevice.Viewport (X/Y offset, Width/Height
        // sub-scale, MinDepth/MaxDepth range) and clip rasterization to framebuffer ∩ Viewport --
        // a default full-target viewport reduces to the pre-GFX-079 full-framebuffer mapping.
        int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
        float vpMinDepth = 0.0f, vpMaxDepth = 1.0f;
        GetActiveViewportRaster(vpX, vpY, vpW, vpH, vpMinDepth, vpMaxDepth);
        const ViewportTransform vpT{static_cast<float>(vpX), static_cast<float>(vpY),
                                    static_cast<float>(vpW), static_cast<float>(vpH),
                                    vpMinDepth, vpMaxDepth, fb.multiSampleCount > 1};
        // REMED-GFX-080: effective raster clip = framebuffer ∩ Viewport ∩ (ScissorRectangle when
        // RasterizerState.ScissorTestEnable). The scissor is framebuffer-space, intersected after
        // the viewport clip (not viewport-local); disabled scissor leaves the viewport clip intact.
        int scX = 0, scY = 0, scW = 0, scH = 0;
        GetActiveScissor(scX, scY, scW, scH);
        const RasterClipRect clip = ScissorClip(ViewportClip(fb, vpX, vpY, vpW, vpH),
                                                scissorTestEnable_, scX, scY, scW, scH);

        // REMED-GFX-119: element = vertexStart + local (never a byte offset); the stride multiply
        // happens only after the element range above was validated.
        const auto fetchVertex = [&](std::int64_t local) -> const std::uint8_t* {
            return base + static_cast<std::size_t>(kVertexStart + local) * stride;
        };

        for (int i = 0; i < primitiveCount; ++i)
        {
            ClipVertex cv[3];
            for (int k = 0; k < 3; ++k)
            {
                const std::uint8_t* raw = fetchVertex(TriangleElementOffset(primitive, i, k));
                cv[k] = BuildPositionColorClipVertex(raw, combined);
            }

            std::array<ClipVertex, kMaxClippedTriangleVertices> clipped{};
            const int clippedCount = ClipTriangleToFrustum(cv, clipped);  // SOFTWARE-106
            if (clippedCount == 0)
                continue;

            std::array<RasterVertex, kMaxClippedTriangleVertices> rv{};
            for (int k = 0; k < clippedCount; ++k)
                rv[static_cast<std::size_t>(k)] =
                    ClipVertexToRasterVertex(clipped[static_cast<std::size_t>(k)], vpT);

            // SOFTWARE-106/107: fan-triangulate the visible polygon. Wireframe exposes only
            // polygon boundary edges; the top-left fill rule owns every internal diagonal once.
            const bool wire = (fillMode_ == 1);
            for (int fan = 1; fan + 1 < clippedCount; ++fan)
            {
                unsigned edgeMask = kEdgeV1V2;
                if (fan == 1) edgeMask |= kEdgeV0V1;
                if (fan + 1 == clippedCount - 1) edgeMask |= kEdgeV2V0;
                RasterizeTriangle(fb, depthState, stencilState, cullMode_,
                                  depthBias_, slopeScaleDepthBias_, clip,
                                  rv[0], rv[static_cast<std::size_t>(fan)],
                                  rv[static_cast<std::size_t>(fan + 1)],
                                  colorWriteMasks_[0], multiSampleMask_, activeOcclusionQuery_,
                                  multiSampleAntiAlias_, wire, edgeMask);
            }
        }
    }

    void SoftwareRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                                               const Matrix& world, const Matrix& view, const Matrix& projection,
                                                               PrimitiveType primitive, int primitiveCount)
    {
        if (primitiveCount <= 0)
            throw std::runtime_error("SoftwareRenderer::DrawIndexedColoredPrimitives: primitiveCount must be > 0");
        if (primitive != PrimitiveType::TriangleList && primitive != PrimitiveType::TriangleStrip)
            throw std::runtime_error(
                "SoftwareRenderer::DrawIndexedColoredPrimitives: unsupported primitive topology");

        const auto& swVb = static_cast<const SoftwareVertexBufferRenderer&>(vb);
        const auto& swIb = static_cast<const SoftwareIndexBufferRenderer&>(ib);
        const std::uint8_t* vbBase = swVb.Data().data();
        const std::size_t stride = swVb.Stride();
        const std::uint8_t* ibBase = swIb.Data().data();
        const bool thirtyTwoBit = swIb.IsThirtyTwoBit();

        // REMED-GFX-110: this entry point carries no GpuDrawParams, so its contract is a complete
        // buffer draw -- first element zero, no base addend. It still validates the exact
        // topology-derived range and every decoded vertex address through the shared helper.
        constexpr int kStartIndex = 0;
        constexpr int kBaseVertex = 0;
        const std::int64_t consumedIndexCount = PrimitiveElementCount(primitive, primitiveCount);
        ValidateIndexedAddressing(ibBase, thirtyTwoBit, ib.GetIndexCount(), vb.GetVertexCount(),
                                  consumedIndexCount, kStartIndex, kBaseVertex);

        const Matrix combined = world * view * projection;
        SoftwareFramebuffer& fb = CurrentFramebuffer();
        const RasterDepthState depthState{
            depthTestEnabled_, depthWriteEnabled_, depthCompareFunction_}; // REMED-GFX-030 draw snapshot
        const RasterStencilState stencilState = SnapshotStencilState(*this); // SOFTWARE-121
        // REMED-GFX-079: see DrawColoredPrimitives -- the active viewport transform + clip.
        int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
        float vpMinDepth = 0.0f, vpMaxDepth = 1.0f;
        GetActiveViewportRaster(vpX, vpY, vpW, vpH, vpMinDepth, vpMaxDepth);
        const ViewportTransform vpT{static_cast<float>(vpX), static_cast<float>(vpY),
                                    static_cast<float>(vpW), static_cast<float>(vpH),
                                    vpMinDepth, vpMaxDepth, fb.multiSampleCount > 1};
        // REMED-GFX-080: effective raster clip = framebuffer ∩ Viewport ∩ (ScissorRectangle when
        // RasterizerState.ScissorTestEnable). The scissor is framebuffer-space, intersected after
        // the viewport clip (not viewport-local); disabled scissor leaves the viewport clip intact.
        int scX = 0, scY = 0, scW = 0, scH = 0;
        GetActiveScissor(scX, scY, scW, scH);
        const RasterClipRect clip = ScissorClip(ViewportClip(fb, vpX, vpY, vpW, vpH),
                                                scissorTestEnable_, scX, scY, scW, scH);

        // REMED-GFX-110: element = startIndex + local (never a byte offset), vertex = decoded
        // index + baseVertex (added exactly once). Every address below was validated above.
        const auto fetchVertex = [&](std::int64_t local) -> const std::uint8_t* {
            const std::int64_t vertexIndex =
                static_cast<std::int64_t>(
                    DecodeIndexElement(ibBase, thirtyTwoBit, kStartIndex + local)) + kBaseVertex;
            return vbBase + static_cast<std::size_t>(vertexIndex) * stride;
        };

        for (int i = 0; i < primitiveCount; ++i)
        {
            ClipVertex cv[3];
            for (int k = 0; k < 3; ++k)
            {
                const std::uint8_t* raw = fetchVertex(TriangleElementOffset(primitive, i, k));
                cv[k] = BuildPositionColorClipVertex(raw, combined);
            }

            std::array<ClipVertex, kMaxClippedTriangleVertices> clipped{};
            const int clippedCount = ClipTriangleToFrustum(cv, clipped);  // SOFTWARE-106
            if (clippedCount == 0)
                continue;

            std::array<RasterVertex, kMaxClippedTriangleVertices> rv{};
            for (int k = 0; k < clippedCount; ++k)
                rv[static_cast<std::size_t>(k)] =
                    ClipVertexToRasterVertex(clipped[static_cast<std::size_t>(k)], vpT);

            const bool wire = (fillMode_ == 1);
            for (int fan = 1; fan + 1 < clippedCount; ++fan)
            {
                unsigned edgeMask = kEdgeV1V2;
                if (fan == 1) edgeMask |= kEdgeV0V1;
                if (fan + 1 == clippedCount - 1) edgeMask |= kEdgeV2V0;
                RasterizeTriangle(fb, depthState, stencilState, cullMode_,
                                  depthBias_, slopeScaleDepthBias_, clip,
                                  rv[0], rv[static_cast<std::size_t>(fan)],
                                  rv[static_cast<std::size_t>(fan + 1)],
                                  colorWriteMasks_[0], multiSampleMask_, activeOcclusionQuery_,
                                  multiSampleAntiAlias_, wire, edgeMask);
            }
        }
    }

    // Phase S5/S6 (SOFTWARE-40..43, 50; SOFTWARE-108): the effect-aware draw path reads declared
    // attributes by XNA semantic and usage index, across every per-vertex stream. Only the
    // deliberately empty declaration of CNAEXT's legacy VertexBuffer(device,count) constructor
    // retains the historical canonical-stride decoder.

    void SoftwareRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb, const Matrix& world,
                                                   const Matrix& view, const Matrix& projection,
                                                   PrimitiveType primitive, int primitiveCount,
                                                   const GpuDrawParams& params)
    {
        if (primitiveCount <= 0)
            throw std::runtime_error("SoftwareRenderer::DrawPrimitivesEx: primitiveCount must be > 0");
        if (primitive != PrimitiveType::TriangleList && primitive != PrimitiveType::TriangleStrip &&
            primitive != PrimitiveType::LineList &&
            primitive != PrimitiveType::LineStrip && primitive != PrimitiveType::PointListEXT)
            throw std::runtime_error(
                "SoftwareRenderer::DrawPrimitivesEx: unsupported primitive topology");
        // Optional PBR maps and XNA's opaque-black null classic stock-effect samplers deliberately
        // proceed to the shared fragment path rather than failing the draw.

        if (g_cubeTrace.enabled) { ++g_cubeTrace.drawId; g_cubeTrace.family = "DrawPrimitives"; }

        // REMED-GFX-119: this path receives the public vertexStart. It was previously dropped, so
        // every non-indexed draw rendered the element-zero prefix of the bound buffer; only the
        // consumed count was ever right.
        const int vertexStart = params.vertexStart;
        const std::int64_t consumedVertexCount =
            PrimitiveElementCount(primitive, primitiveCount);
        // The renderer-contract fallback has no per-stream bounds metadata and may be CNAEXT's
        // empty-declaration CPU layout, so retain its strict guard. Public classic draws carry a
        // stream tuple and follow XNA's native forwarding semantics; individual missing records
        // become default attributes in fetchVertex below instead of forming invalid host pointers.
        if (params.vertexStreamCount == 0)
            ValidateNonIndexedAddressing(vb.GetVertexCount(), consumedVertexCount, vertexStart);

        const auto& swVb = static_cast<const SoftwareVertexBufferRenderer&>(vb);
        // The combined stride remains relevant only to the empty-declaration compatibility path;
        // declared streams below are resolved by semantic, never by this aggregate number.
        const std::size_t stride = CombinedVertexStrideOr(params, swVb.Stride());
        if (swVb.Declaration().IsEmpty() &&
            stride != 16 && stride != 20 && stride != 24 && stride != 32 &&
            stride != 48 && stride != 52 && stride != 56 && stride != 60 &&
            stride != 68 && stride != 76 && stride != 80)
            throw std::runtime_error(
                "SoftwareRenderer::DrawPrimitivesEx: unsupported vertex stride "
                "for a buffer with no VertexDeclaration");

        const std::uint8_t* base = swVb.Data().data();

        const Matrix combined = world * view * projection;
        SoftwareFramebuffer& fb = CurrentFramebuffer();
        const RasterDepthState depthState{
            depthTestEnabled_, depthWriteEnabled_, depthCompareFunction_}; // REMED-GFX-030 draw snapshot
        const RasterStencilState stencilState = SnapshotStencilState(*this); // SOFTWARE-121
        // REMED-GFX-079: see DrawColoredPrimitives -- the active viewport transform + clip.
        int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
        float vpMinDepth = 0.0f, vpMaxDepth = 1.0f;
        GetActiveViewportRaster(vpX, vpY, vpW, vpH, vpMinDepth, vpMaxDepth);
        const ViewportTransform vpT{static_cast<float>(vpX), static_cast<float>(vpY),
                                    static_cast<float>(vpW), static_cast<float>(vpH),
                                    vpMinDepth, vpMaxDepth, fb.multiSampleCount > 1};
        // REMED-GFX-080: effective raster clip = framebuffer ∩ Viewport ∩ (ScissorRectangle when
        // RasterizerState.ScissorTestEnable). The scissor is framebuffer-space, intersected after
        // the viewport clip (not viewport-local); disabled scissor leaves the viewport clip intact.
        int scX = 0, scY = 0, scW = 0, scH = 0;
        GetActiveScissor(scX, scY, scW, scH);
        const RasterClipRect clip = ScissorClip(ViewportClip(fb, vpX, vpY, vpW, vpH),
                                                scissorTestEnable_, scX, scY, scW, scH);

        // REMED-GFX-119: element = vertexStart + local (never a byte offset).
        // REMED-GFX-201: every bound per-vertex stream advances by the SAME element count, each
        // multiplied by its OWN stride and shifted by its OWN binding offset -- FNA3D's
        // `vertexStride * (vertexOffset + start)`, per stream.
        const auto fetchVertex = [&](std::int64_t local) -> CombinedVertexReader {
            CombinedVertexReader reader;
            reader.params = &params;
            reader.fallbackBuffer = &swVb;
            const std::int64_t element = vertexStart + local;
            if (params.vertexStreamCount == 0)
            {
                reader.recordBase[0] = base + static_cast<std::size_t>(element) * stride;
                return reader;
            }
            for (int s = 0; s < params.vertexStreamCount; ++s)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(s)];
                if (stream.instanceFrequency != 0 || !stream.vertexShaderInputUsed)
                    continue;
                const auto* streamVb =
                    static_cast<const SoftwareVertexBufferRenderer*>(stream.buffer);
                const std::int64_t streamElement =
                    static_cast<std::int64_t>(stream.vertexOffset) + element;
                if (streamElement < 0 || streamElement >= stream.vertexCount)
                    continue;
                reader.recordBase[static_cast<std::size_t>(s)] =
                    streamVb->Data().data() +
                    static_cast<std::size_t>(streamElement) *
                        static_cast<std::size_t>(stream.strideInBytes);
            }
            return reader;
        };
        // REMED-GFX-148: snapshot both static BlendState and dynamic BlendFactor once for this
        // public draw, beside the depth-state snapshot above.
        const SoftwareBlendState blendState = blendState_;
        const std::array<float, 4> blendFactor = blendFactor_;

        for (int i = 0; i < primitiveCount; ++i)
        {
            if (primitive == PrimitiveType::PointListEXT)
            {
                const CombinedVertexReader raw = fetchVertex(i);
                const ClipVertex cv = BuildGenericClipVertex(raw, stride, combined, params);
                if (IsInsideClipVolume(cv))
                    RasterizePointShaded(
                        fb, depthState, stencilState, blendState, blendFactor, params,
                        clip, ClipVertexToRasterVertex(cv, vpT), colorWriteMasks_[0], multiSampleMask_,
                        GetSamplerState(0), GetSamplerState(1), activeOcclusionQuery_);
                continue;
            }
            if (primitive == PrimitiveType::LineList || primitive == PrimitiveType::LineStrip)
            {
                const std::int64_t first =
                    (primitive == PrimitiveType::LineList) ? static_cast<std::int64_t>(i) * 2 : i;
                ClipVertex a = BuildGenericClipVertex(
                    fetchVertex(first), stride, combined, params);
                ClipVertex b = BuildGenericClipVertex(
                    fetchVertex(first + 1), stride, combined, params);
                if (ClipLineToFrustum(a, b))
                    RasterizeLineShaded(
                        fb, depthState, stencilState, blendState, blendFactor, params, clip,
                        ClipVertexToRasterVertex(a, vpT), ClipVertexToRasterVertex(b, vpT),
                        colorWriteMasks_[0], multiSampleMask_, GetSamplerState(0), GetSamplerState(1),
                        activeOcclusionQuery_, multiSampleAntiAlias_);
                continue;
            }

            ClipVertex cv[3];
            for (int k = 0; k < 3; ++k)
            {
                const CombinedVertexReader raw =
                    fetchVertex(TriangleElementOffset(primitive, i, k));
                cv[k] = BuildGenericClipVertex(raw, stride, combined, params);
            }

            std::array<ClipVertex, kMaxClippedTriangleVertices> clipped{};
            const int clippedCount = ClipTriangleToFrustum(cv, clipped);  // SOFTWARE-106
            if (clippedCount == 0)
                continue;

            std::array<RasterVertex, kMaxClippedTriangleVertices> rv{};
            for (int k = 0; k < clippedCount; ++k)
                rv[static_cast<std::size_t>(k)] =
                    ClipVertexToRasterVertex(clipped[static_cast<std::size_t>(k)], vpT);

            // REMED-GFX-079: clip 3D rasterization to framebuffer ∩ active Viewport (was the full
            // framebuffer). A default full-target viewport yields the same clip byte-for-byte.
            // SOFTWARE-106/107: preserve only the clipped polygon boundary in wireframe; top-left
            // coverage gives every internal fan diagonal to exactly one triangle in solid fill.
            const bool wire = (fillMode_ == 1);
            for (int fan = 1; fan + 1 < clippedCount; ++fan)
            {
                unsigned edgeMask = kEdgeV1V2;
                if (fan == 1) edgeMask |= kEdgeV0V1;
                if (fan + 1 == clippedCount - 1) edgeMask |= kEdgeV2V0;
                RasterizeTriangleShaded(fb, depthState, stencilState, blendState, blendFactor,
                                        cullMode_,
                                        depthBias_, slopeScaleDepthBias_, params,
                                        clip, rv[0], rv[static_cast<std::size_t>(fan)],
                                        rv[static_cast<std::size_t>(fan + 1)],
                                        colorWriteMasks_[0], multiSampleMask_, GetSamplerState(0),
                                        GetSamplerState(1), activeOcclusionQuery_,
                                        multiSampleAntiAlias_, wire, edgeMask);
            }
        }
    }

    void SoftwareRenderer::DrawIndexedPrimitivesEx(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        DrawIndexedPrimitivesInternal(
            vb, ib, world, view, projection, primitive, primitiveCount, params, false);
    }

    void SoftwareRenderer::DrawIndexedPrimitivesInternal(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params,
        bool applyInstanceStreams)
    {
        if (primitiveCount <= 0)
            throw std::runtime_error("SoftwareRenderer::DrawIndexedPrimitivesEx: primitiveCount must be > 0");
        if (primitive != PrimitiveType::TriangleList && primitive != PrimitiveType::TriangleStrip &&
            primitive != PrimitiveType::LineList &&
            primitive != PrimitiveType::LineStrip && primitive != PrimitiveType::PointListEXT)
            throw std::runtime_error(
                "SoftwareRenderer::DrawIndexedPrimitivesEx: unsupported primitive topology");
        // See DrawPrimitivesEx: optional PBR maps and null classic samplers resolve in shading.

        const auto& swVb = static_cast<const SoftwareVertexBufferRenderer&>(vb);
        const auto& swIb = static_cast<const SoftwareIndexBufferRenderer&>(ib);
        // See DrawPrimitivesEx: declared streams are semantic-driven; this aggregate is only the
        // legacy empty-declaration fallback key.
        const std::size_t stride = CombinedVertexStrideOr(params, swVb.Stride());
        if (swVb.Declaration().IsEmpty() &&
            stride != 16 && stride != 20 && stride != 24 && stride != 32 &&
            stride != 48 && stride != 52 && stride != 56 && stride != 60 &&
            stride != 68 && stride != 76 && stride != 80)
            throw std::runtime_error(
                "SoftwareRenderer::DrawIndexedPrimitivesEx: unsupported vertex stride "
                "for a buffer with no VertexDeclaration");

        if (g_cubeTrace.enabled) { ++g_cubeTrace.drawId; g_cubeTrace.family = "DrawIndexedPrimitives"; }

        const std::uint8_t* vbBase = swVb.Data().data();
        const std::uint8_t* ibBase = swIb.Data().data();
        const bool thirtyTwoBit = swIb.IsThirtyTwoBit();

        // REMED-GFX-110: this path receives the public startIndex/baseVertex. Both were previously
        // dropped, so every indexed draw rendered the element-zero prefix of the bound buffers.
        // minVertexIndex/numVertices stay hints -- they are deliberately not consulted here.
        const int startIndex = params.startIndex;
        const int baseVertex = params.baseVertex;
        const std::int64_t consumedIndexCount = PrimitiveElementCount(primitive, primitiveCount);
        // Strict validation remains for renderer-contract/legacy calls whose packed fallback
        // would otherwise form raw host pointers. Public declared-buffer draws intentionally match
        // XNA and pass native ranges through; missing index/vertex records are defaulted below.
        if (params.vertexStreamCount == 0 || swVb.Declaration().IsEmpty())
        {
            ValidateIndexedAddressing(ibBase, thirtyTwoBit, ib.GetIndexCount(),
                                      vb.GetVertexCount(), consumedIndexCount,
                                      startIndex, baseVertex);
        }

        const Matrix combined = world * view * projection;
        SoftwareFramebuffer& fb = CurrentFramebuffer();
        const RasterDepthState depthState{
            depthTestEnabled_, depthWriteEnabled_, depthCompareFunction_}; // REMED-GFX-030 draw snapshot
        const RasterStencilState stencilState = SnapshotStencilState(*this); // SOFTWARE-121
        // REMED-GFX-079: see DrawColoredPrimitives -- the active viewport transform + clip.
        int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
        float vpMinDepth = 0.0f, vpMaxDepth = 1.0f;
        GetActiveViewportRaster(vpX, vpY, vpW, vpH, vpMinDepth, vpMaxDepth);
        const ViewportTransform vpT{static_cast<float>(vpX), static_cast<float>(vpY),
                                    static_cast<float>(vpW), static_cast<float>(vpH),
                                    vpMinDepth, vpMaxDepth, fb.multiSampleCount > 1};
        // REMED-GFX-080: effective raster clip = framebuffer ∩ Viewport ∩ (ScissorRectangle when
        // RasterizerState.ScissorTestEnable). The scissor is framebuffer-space, intersected after
        // the viewport clip (not viewport-local); disabled scissor leaves the viewport clip intact.
        int scX = 0, scY = 0, scW = 0, scH = 0;
        GetActiveScissor(scX, scY, scW, scH);
        const RasterClipRect clip = ScissorClip(ViewportClip(fb, vpX, vpY, vpW, vpH),
                                                scissorTestEnable_, scX, scY, scW, scH);

        // REMED-GFX-110: element = startIndex + local (never a byte offset), vertex = decoded
        // index + baseVertex (added exactly once).
        // REMED-GFX-201: the decoded index plus baseVertex addresses EVERY bound per-vertex
        // stream, each shifted by its own binding offset and multiplied by its own stride.
        const auto fetchVertex = [&](std::int64_t local) -> CombinedVertexReader {
            const std::int64_t indexElement = static_cast<std::int64_t>(startIndex) + local;
            std::uint32_t decodedIndex = 0;
            if (indexElement >= 0 && indexElement < ib.GetIndexCount())
                decodedIndex = DecodeIndexElement(ibBase, thirtyTwoBit, indexElement);
            const std::int64_t vertexIndex =
                static_cast<std::int64_t>(decodedIndex) + baseVertex;
            CombinedVertexReader reader;
            reader.params = &params;
            reader.fallbackBuffer = &swVb;
            reader.useInstanceStreams = applyInstanceStreams;
            if (params.vertexStreamCount == 0)
            {
                reader.recordBase[0] =
                    vbBase + static_cast<std::size_t>(vertexIndex) * stride;
                return reader;
            }
            for (int s2 = 0; s2 < params.vertexStreamCount; ++s2)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(s2)];
                if (!stream.vertexShaderInputUsed)
                    continue;
                const auto* streamVb =
                    static_cast<const SoftwareVertexBufferRenderer*>(stream.buffer);
                const std::int64_t streamElement = stream.instanceFrequency > 0
                    ? static_cast<std::int64_t>(stream.vertexOffset)
                    : static_cast<std::int64_t>(stream.vertexOffset) + vertexIndex;
                if (streamElement < 0 || streamElement >= stream.vertexCount)
                    continue;
                reader.recordBase[static_cast<std::size_t>(s2)] =
                    streamVb->Data().data() +
                    static_cast<std::size_t>(streamElement) *
                        static_cast<std::size_t>(stream.strideInBytes);
            }
            return reader;
        };
        // REMED-GFX-148: one by-value state snapshot for the complete indexed public draw.
        const SoftwareBlendState blendState = blendState_;
        const std::array<float, 4> blendFactor = blendFactor_;

        for (int i = 0; i < primitiveCount; ++i)
        {
            if (primitive == PrimitiveType::PointListEXT)
            {
                const CombinedVertexReader raw = fetchVertex(i);
                const ClipVertex cv = BuildGenericClipVertex(raw, stride, combined, params);
                if (IsInsideClipVolume(cv))
                    RasterizePointShaded(
                        fb, depthState, stencilState, blendState, blendFactor, params,
                        clip, ClipVertexToRasterVertex(cv, vpT), colorWriteMasks_[0], multiSampleMask_,
                        GetSamplerState(0), GetSamplerState(1), activeOcclusionQuery_);
                continue;
            }
            if (primitive == PrimitiveType::LineList || primitive == PrimitiveType::LineStrip)
            {
                const std::int64_t first =
                    (primitive == PrimitiveType::LineList) ? static_cast<std::int64_t>(i) * 2 : i;
                ClipVertex a = BuildGenericClipVertex(
                    fetchVertex(first), stride, combined, params);
                ClipVertex b = BuildGenericClipVertex(
                    fetchVertex(first + 1), stride, combined, params);
                if (ClipLineToFrustum(a, b))
                    RasterizeLineShaded(
                        fb, depthState, stencilState, blendState, blendFactor, params, clip,
                        ClipVertexToRasterVertex(a, vpT), ClipVertexToRasterVertex(b, vpT),
                        colorWriteMasks_[0], multiSampleMask_, GetSamplerState(0), GetSamplerState(1),
                        activeOcclusionQuery_, multiSampleAntiAlias_);
                continue;
            }

            ClipVertex cv[3];
            for (int k = 0; k < 3; ++k)
            {
                const CombinedVertexReader raw =
                    fetchVertex(TriangleElementOffset(primitive, i, k));
                cv[k] = BuildGenericClipVertex(raw, stride, combined, params);
            }

            std::array<ClipVertex, kMaxClippedTriangleVertices> clipped{};
            const int clippedCount = ClipTriangleToFrustum(cv, clipped);  // SOFTWARE-106
            if (clippedCount == 0)
                continue;

            std::array<RasterVertex, kMaxClippedTriangleVertices> rv{};
            for (int k = 0; k < clippedCount; ++k)
                rv[static_cast<std::size_t>(k)] =
                    ClipVertexToRasterVertex(clipped[static_cast<std::size_t>(k)], vpT);

            // REMED-GFX-079: clip 3D rasterization to framebuffer ∩ active Viewport (was the full
            // framebuffer). A default full-target viewport yields the same clip byte-for-byte.
            // SOFTWARE-106/107: preserve only the clipped polygon boundary in wireframe; top-left
            // coverage gives every internal fan diagonal to exactly one triangle in solid fill.
            const bool wire = (fillMode_ == 1);
            for (int fan = 1; fan + 1 < clippedCount; ++fan)
            {
                unsigned edgeMask = kEdgeV1V2;
                if (fan == 1) edgeMask |= kEdgeV0V1;
                if (fan + 1 == clippedCount - 1) edgeMask |= kEdgeV2V0;
                RasterizeTriangleShaded(fb, depthState, stencilState, blendState, blendFactor,
                                        cullMode_,
                                        depthBias_, slopeScaleDepthBias_, params,
                                        clip, rv[0], rv[static_cast<std::size_t>(fan)],
                                        rv[static_cast<std::size_t>(fan + 1)],
                                        colorWriteMasks_[0], multiSampleMask_, GetSamplerState(0),
                                        GetSamplerState(1), activeOcclusionQuery_,
                                        multiSampleAntiAlias_, wire, edgeMask);
            }
        }
    }

    void SoftwareRenderer::DrawInstancedPrimitivesEx(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, int instanceCount,
        const GpuDrawParams& params)
    {
        if (instanceCount <= 0)
        {
            throw System::ArgumentOutOfRangeException(
                "instanceCount", std::to_string(instanceCount),
                "instanceCount must be greater than zero.");
        }

        // Normalize hand-built renderer-contract calls as well as public GraphicsDevice calls.
        // The latter always supply concrete strides/counts; SetInstancedVertexStreamsEXT permits
        // zero strides so a renderer can recover them from its own buffer resource.
        GpuDrawParams normalized = params;
        int nextInstanceLocation = 0;
        for (int i = 0; i < normalized.vertexStreamCount; ++i)
        {
            auto& stream = normalized.vertexStreams[static_cast<std::size_t>(i)];
            if (stream.buffer == nullptr)
                throw System::InvalidOperationException(
                    "Software instanced drawing requires every active vertex stream to own a buffer.");
            const auto* buffer =
                static_cast<const SoftwareVertexBufferRenderer*>(stream.buffer);
            if (stream.strideInBytes <= 0)
                stream.strideInBytes = static_cast<int>(buffer->Stride());
            if (stream.vertexCount <= 0)
                stream.vertexCount = buffer->GetVertexCount();

            if (stream.instanceFrequency <= 0)
                continue;
            const auto& elements = buffer->Declaration().GetElements();
            if (elements.empty() || nextInstanceLocation >= 4)
            {
                throw System::InvalidOperationException(
                    "Software instanced drawing requires a declared per-instance matrix within "
                    "the four stock-effect instance attribute locations.");
            }
            nextInstanceLocation += std::min<int>(
                static_cast<int>(elements.size()), 4 - nextInstanceLocation);
        }

        normalized.instanceCount = 1;
        for (int instance = 0; instance < instanceCount; ++instance)
        {
            GpuDrawParams current = normalized;
            for (int i = 0; i < current.vertexStreamCount; ++i)
            {
                auto& stream = current.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency > 0)
                {
                    stream.vertexOffset =
                        normalized.vertexStreams[static_cast<std::size_t>(i)].vertexOffset +
                        instance / stream.instanceFrequency;
                }
            }
            DrawIndexedPrimitivesInternal(
                vb, ib, world, view, projection, primitive, primitiveCount, current, true);
        }
    }
#else
    void SoftwareRenderer::DrawColoredPrimitives(const IVertexBufferRenderer&, const Matrix&,
                                                         const Matrix&, const Matrix&, PrimitiveType, int)
    {
        throw System::NotSupportedException(
            "Software's GDI 2D compilation unit does not include 3D primitive drawing.");
    }

    void SoftwareRenderer::DrawIndexedColoredPrimitives(
        const IVertexBufferRenderer&, const IIndexBufferRenderer&, const Matrix&, const Matrix&,
        const Matrix&, PrimitiveType, int)
    {
        throw System::NotSupportedException(
            "Software's GDI 2D compilation unit does not include indexed 3D primitive drawing.");
    }

    void SoftwareRenderer::DrawPrimitivesEx(const IVertexBufferRenderer&, const Matrix&,
                                                    const Matrix&, const Matrix&, PrimitiveType, int,
                                                    const GpuDrawParams&)
    {
        throw System::NotSupportedException(
            "Software's GDI 2D compilation unit does not include effect-aware 3D drawing.");
    }

    void SoftwareRenderer::DrawIndexedPrimitivesEx(
        const IVertexBufferRenderer&, const IIndexBufferRenderer&, const Matrix&, const Matrix&,
        const Matrix&, PrimitiveType, int, const GpuDrawParams&)
    {
        throw System::NotSupportedException(
            "Software's GDI 2D compilation unit does not include indexed effect-aware 3D drawing.");
    }

    void SoftwareRenderer::DrawInstancedPrimitivesEx(
        const IVertexBufferRenderer&, const IIndexBufferRenderer&, const Matrix&, const Matrix&,
        const Matrix&, PrimitiveType, int, int, const GpuDrawParams&)
    {
        throw System::NotSupportedException(
            "Software's GDI 2D compilation unit does not include instanced 3D drawing.");
    }
#endif
}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_SOFTWARE
    // plans/plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace Software { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> Software::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        auto renderer = std::make_unique<Software::SoftwareRenderer>(
            args.virtualWidth, args.virtualHeight,
            args.depthStencilFormat != 0, args.depthStencilFormat == 3);
        renderer->ApplyMultiSampleCount(args.multiSampleCount);
        return renderer;
    }
#endif
}
