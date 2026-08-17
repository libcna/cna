#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorMatrixEffect.hpp"

#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
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
            float u = 0.0f, v = 0.0f;   ///< Texture coordinate * invW (Phase S5).
            /// World-space position/normal * invW (SOFTWARE-82, EnvironmentMapEffect only) --
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
            const float sourceTerm = source[channel] *
                BlendFactorComponent(sourceFactor, channel, source, destination, constant);
            const float destinationTerm = destination[channel] *
                BlendFactorComponent(destinationFactor, channel, source, destination, constant);
            return std::clamp(ApplyBlendFunction(function, sourceTerm, destinationTerm), 0.0f, 1.0f);
        }

        /// One CPU stencil state snapshot.  The shared framebuffer stores one unsigned 8-bit
        /// stencil value per pixel.  GDI uses this through SpriteBatch for 2D masking; retaining
        /// it in the shared rasterizer also keeps a target switch from losing its stencil image.
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

        void WritePassingDepth(SoftwareFramebuffer& fb, const RasterDepthState& state,
                               std::size_t pixelIndex, float depth)
        {
            // As on the correct EasyGL/D3D paths, disabling the depth test disables the complete
            // depth operation even if a custom state happens to retain writeEnable=true.
            if (state.testEnabled && state.writeEnabled)
            {
                if (fb.depthBuffer.empty())
                    throw std::logic_error(
                        "Software rasterizer received enabled depth state without depth storage.");
                fb.depthBuffer[pixelIndex] = depth;
            }
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

        void WriteStencil(SoftwareFramebuffer& fb, const RasterStencilState& state,
                          std::size_t pixelIndex, int operation)
        {
            if (fb.stencilBuffer.empty())
                throw std::logic_error(
                    "Software rasterizer received enabled stencil state without stencil storage.");
            const std::uint8_t oldValue = fb.stencilBuffer[pixelIndex];
            const std::uint8_t operationValue =
                ApplyStencilOperation(oldValue, state.reference, operation);
            fb.stencilBuffer[pixelIndex] = static_cast<std::uint8_t>(
                (oldValue & static_cast<std::uint8_t>(~state.writeMask)) |
                (operationValue & state.writeMask));
        }

#ifndef CNA_SOFTWARE_2D_ONLY
        /// One vertex in clip space (before the perspective divide), attributes NOT premultiplied
        /// by W (SOFTWARE-83). Clip space is still linear -- position and attributes can both be
        /// interpolated with a plain lerp here, unlike the post-divide RasterVertex above.
        struct ClipVertex
        {
            float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
            float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
            float u = 0.0f, v = 0.0f;
            /// World-space position/normal (SOFTWARE-82, EnvironmentMapEffect only).
            float wpx = 0.0f, wpy = 0.0f, wpz = 0.0f;
            float nx = 0.0f, ny = 0.0f, nz = 1.0f;
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
        /// into clip space. Attributes are left un-premultiplied -- near-plane clipping (SOFTWARE-83)
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
            out.wpx = a.wpx + t * (b.wpx - a.wpx);
            out.wpy = a.wpy + t * (b.wpy - a.wpy);
            out.wpz = a.wpz + t * (b.wpz - a.wpz);
            out.nx = a.nx + t * (b.nx - a.nx);
            out.ny = a.ny + t * (b.ny - a.ny);
            out.nz = a.nz + t * (b.nz - a.nz);
            return out;
        }

        /// SOFTWARE-83: clips a triangle against the single near-plane half-space `w > kNearEpsilon`
        /// using Sutherland-Hodgman, writing up to 4 output vertices to `out` and returning the
        /// count (0 = triangle entirely behind the near plane and fully discarded; 3 = no clipping
        /// needed or one corner clipped off; 4 = two corners clipped off, forming a quad). Preserves
        /// the input winding order, so backface culling (SOFTWARE-81) on the result stays correct.
        int ClipTriangleNearPlane(const ClipVertex verts[3], ClipVertex out[4])
        {
            constexpr float kNearEpsilon = 1e-5f;
            int count = 0;
            for (int i = 0; i < 3; ++i)
            {
                const ClipVertex& cur = verts[i];
                const ClipVertex& prev = verts[(i + 2) % 3];
                const bool curIn = cur.w > kNearEpsilon;
                const bool prevIn = prev.w > kNearEpsilon;
                if (curIn != prevIn)
                {
                    const float t = (kNearEpsilon - prev.w) / (cur.w - prev.w);
                    out[count++] = LerpClipVertex(prev, cur, t);
                }
                if (curIn)
                    out[count++] = cur;
            }
            return count;
        }

        /// Clips a line segment against the same near-plane half-space as triangles.  A point is
        /// accepted by checking its W against this boundary directly in the draw path.
        bool ClipLineNearPlane(ClipVertex& a, ClipVertex& b)
        {
            constexpr float kNearEpsilon = 1e-5f;
            const bool aIn = a.w > kNearEpsilon;
            const bool bIn = b.w > kNearEpsilon;
            if (!aIn && !bIn)
                return false;
            if (aIn && bIn)
                return true;

            const float t = (kNearEpsilon - a.w) / (b.w - a.w);
            const ClipVertex intersection = LerpClipVertex(a, b, t);
            if (!aIn) a = intersection;
            else      b = intersection;
            return true;
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
        RasterVertex ClipVertexToRasterVertex(const ClipVertex& cv, const ViewportTransform& vp)
        {
            const float invW = 1.0f / cv.w;
            const float ndcX = cv.x * invW;
            const float ndcY = cv.y * invW;
            const float ndcZ = cv.z * invW;

            RasterVertex out;
            out.x = (ndcX * 0.5f + 0.5f) * vp.width + vp.x;
            out.y = (1.0f - (ndcY * 0.5f + 0.5f)) * vp.height + vp.y;
            out.depth = vp.minDepth + ndcZ * (vp.maxDepth - vp.minDepth);
            out.invW = invW;
            out.r = cv.r * invW;
            out.g = cv.g * invW;
            out.b = cv.b * invW;
            out.a = cv.a * invW;
            out.u = cv.u * invW;
            out.v = cv.v * invW;
            out.wpx = cv.wpx * invW;
            out.wpy = cv.wpy * invW;
            out.wpz = cv.wpz * invW;
            out.nx = cv.nx * invW;
            out.ny = cv.ny * invW;
            out.nz = cv.nz * invW;
            return out;
        }
#endif

        float EdgeFunction(float ax, float ay, float bx, float by, float px, float py)
        {
            return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
        }

        /// REMED-GFX-083: the Software depth buffer's minimum resolvable difference "r" -- the unit
        /// RasterizerState.DepthBias is expressed in. GraphicsDevice forwards the public XNA float
        /// UNSCALED to every renderer (see setRasterizerStateProperty); the GPU drivers then multiply it
        /// by their own depth-format r (D3D11RasterizerStateCache rounds it into the 24-bit-UNORM
        /// DepthBias INT, Vulkan/EasyGL feed it to vkCmdSetDepthBias/glPolygonOffset). The Software buffer
        /// is float32 in [0,1]; 2^-24 is the minimum resolvable difference of XNA's canonical Depth24
        /// format (and D3D11's 24-bit UNORM path), so a given DepthBias yields the same constant offset
        /// here as on the D3D11 renderer -- cross-renderer observable parity without a per-primitive
        /// max-z-exponent computation.
        constexpr float kDepthBiasUnit = 1.0f / static_cast<float>(1 << 24);

        /// REMED-GFX-083: the effective per-triangle depth offset added to every fragment's post-viewport
        /// depth, matching the OpenGL/D3D/Vulkan polygon-offset contract the GPU renderers map onto:
        ///   offset = slopeScaleDepthBias * m + depthBias * r
        /// where m = max(|dz/dx|, |dz/dy|) is the triangle's maximum depth slope in raster space (screen
        /// x/y pixels, post-viewport window depth) -- computed ONCE per triangle from the depth plane,
        /// never per fragment -- and r is the depth buffer's minimum resolvable difference (kDepthBiasUnit).
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
            return slopeScaleDepthBias * maxDepthSlope + depthBias * kDepthBiasUnit;
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
            const auto& pixels = texture.ColorPixels(level);

            const auto fetch = [&](int px, int py, int channel) -> float {
                const std::size_t idx = (static_cast<std::size_t>(py) * static_cast<std::size_t>(texW) +
                                        static_cast<std::size_t>(px)) * 4u + static_cast<std::size_t>(channel);
                return pixels[idx] / 255.0f;
            };

            const bool point = magnify ? FilterMagnifiesWithPoint(sampler.filter)
                                       : FilterMinifiesWithPoint(sampler.filter);
            if (point)
            {
                const int x = AddressTexel(FloorToTexelIndex(u * static_cast<float>(texW)),
                                           texW, sampler.addressU);
                const int y = AddressTexel(FloorToTexelIndex(v * static_cast<float>(texH)),
                                           texH, sampler.addressV);
                r = fetch(x, y, 0);
                g = fetch(x, y, 1);
                b = fetch(x, y, 2);
                a = fetch(x, y, 3);
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
            const auto bilerp = [&](int channel) -> float {
                const float t00 = fetch(x0, y0, channel);
                const float t10 = fetch(x1, y0, channel);
                const float t01 = fetch(x0, y1, channel);
                const float t11 = fetch(x1, y1, channel);
                const float top = t00 + (t10 - t00) * fx;
                const float bottom = t01 + (t11 - t01) * fx;
                return top + (bottom - top) * fy;
            };

            r = bilerp(0);
            g = bilerp(1);
            b = bilerp(2);
            a = bilerp(3);

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
        ///      ONCE per triangle by TriangleTexelRate -- no per-fragment derivative work, no
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
        void SampleTexture(const SoftwareColorSurface& texture, const SoftwareSamplerState& sampler,
                           bool magnify, float lambda, float u, float v,
                           float& r, float& g, float& b, float& a)
        {
            const int levels = std::max(1, texture.ColorLevelCount());
            // No chain, or a magnifying footprint: there is no level below 0 to select, so the mip
            // component has nothing to decide and the magnification filter owns the result.
            if (levels <= 1 || magnify || !(lambda > 0.0f))
            {
                SampleLevel(texture, 0, sampler, magnify, u, v, r, g, b, a);
                return;
            }

            const float maxLevel = static_cast<float>(levels - 1);
            const float clamped = std::min(lambda, maxLevel);

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

        /// REMED-GFX-150: whether this triangle MAGNIFIES the given texture (a destination pixel
        /// covers at most one texel) or minifies it.
        ///
        /// XNA's TextureFilter names a SEPARATE minification and magnification filter for ordinals
        /// 5..8 (MinLinearMagPoint*, MinPointMagLinear*); the other five use one filter for both, so
        /// this classification is ignored for them and cannot perturb Point or Linear.
        ///
        /// REMED-GFX-175: the same rate is now also the LOD, so this returns rho itself rather than
        /// the magnify/minify bit it used to. The classification is unchanged -- rho <= 1 is
        /// magnification -- so nothing REMED-GFX-150 established moves; what is new is that the
        /// caller can also take log2(rho) and select a mip level with it.
        ///
        /// The rate is the standard rho = max(|d(s,t)/dx|, |d(s,t)/dy|) in texels per pixel,
        /// evaluated ONCE per triangle from its three vertices: exact for the affine SpriteBatch
        /// quads (invW == 1 throughout), and a per-triangle estimate for perspective 3D geometry,
        /// where the true rate varies across the triangle. RasterVertex stores u,v premultiplied by
        /// invW, so they are un-premultiplied here first. A degenerate triangle reports a rate of 1
        /// -- magnification, level 0 -- because its rate is undefined and it covers no pixels worth
        /// minifying.
        /// REMED-GFX-182: the screen-space part of the rate, shared by the 2D and cube paths so
        /// there is exactly one footprint formula on this renderer. @p s0..@p t2 are the source
        /// coordinates in TEXELS at the triangle's three vertices; how they were obtained -- a UV
        /// attribute for an ordinary texture, a reflection vector projected onto a cube face for the
        /// environment map -- is the caller's business and is the only thing that differs.
        float ScreenSpaceTexelRate(const RasterVertex& v0, const RasterVertex& v1, const RasterVertex& v2,
                                   float s0, float s1, float s2, float t0, float t1, float t2)
        {
            const float area2 = (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
            if (!(std::abs(area2) > 1e-12f)) return 1.0f;

            const float dsdx = ((s1 - s0) * (v2.y - v0.y) - (s2 - s0) * (v1.y - v0.y)) / area2;
            const float dsdy = ((s2 - s0) * (v1.x - v0.x) - (s1 - s0) * (v2.x - v0.x)) / area2;
            const float dtdx = ((t1 - t0) * (v2.y - v0.y) - (t2 - t0) * (v1.y - v0.y)) / area2;
            const float dtdy = ((t2 - t0) * (v1.x - v0.x) - (t1 - t0) * (v2.x - v0.x)) / area2;

            const float rhoX = std::sqrt(dsdx * dsdx + dtdx * dtdx);
            const float rhoY = std::sqrt(dsdy * dsdy + dtdy * dtdy);
            const float rho = std::max(rhoX, rhoY);
            // The `!(rho > 1)` form rejects NaN too, so a non-finite rate collapses onto
            // magnification/level 0 rather than propagating into a level index.
            return !(rho > 1.0f) ? 1.0f : rho;
        }

        float TriangleTexelRate(const RasterVertex& v0, const RasterVertex& v1, const RasterVertex& v2,
                                int texW, int texH)
        {
            const float w0 = (v0.invW != 0.0f) ? v0.invW : 1.0f;
            const float w1 = (v1.invW != 0.0f) ? v1.invW : 1.0f;
            const float w2 = (v2.invW != 0.0f) ? v2.invW : 1.0f;
            return ScreenSpaceTexelRate(v0, v1, v2,
                                        v0.u / w0 * static_cast<float>(texW),
                                        v1.u / w1 * static_cast<float>(texW),
                                        v2.u / w2 * static_cast<float>(texW),
                                        v0.v / w0 * static_cast<float>(texH),
                                        v1.v / w1 * static_cast<float>(texH),
                                        v2.v / w2 * static_cast<float>(texH));
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
            CubeFaceSurface(const SoftwareTextureCubeRenderer& cube, int face)
                : cube_(cube), face_(face) {}

            [[nodiscard]] int ColorWidth() const override { return std::max(1, cube_.GetSize()); }
            [[nodiscard]] int ColorHeight() const override { return std::max(1, cube_.GetSize()); }
            [[nodiscard]] const std::vector<std::uint8_t>& ColorPixels() const override
            { return cube_.FacePixels(face_); }

            [[nodiscard]] int ColorLevelCount() const override { return cube_.FaceLevelCount(face_); }
            [[nodiscard]] int ColorWidth(int level) const override { return cube_.FaceDim(level); }
            [[nodiscard]] int ColorHeight(int level) const override { return cube_.FaceDim(level); }
            [[nodiscard]] const std::vector<std::uint8_t>& ColorPixels(int level) const override
            { return cube_.FacePixels(face_, level); }

        private:
            const SoftwareTextureCubeRenderer& cube_;
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
        void SampleCubeMap(const SoftwareTextureCubeRenderer& cube, const SoftwareSamplerState& sampler,
                          bool magnify, float lambda, const Vector3& dir,
                          float& r, float& g, float& b, float& a)
        {
            int face = 0;
            float s = 0.5f, t = 0.5f;
            SelectCubeFace(dir, face, s, t);

            const CubeFaceSurface surface(cube, face);
            const SoftwareSamplerState cubeSampler = CubeSamplerFor(sampler);

            const long long fetchesBefore = g_samplerTrace.texelFetches;
            SampleTexture(surface, cubeSampler, magnify, lambda, s, t, r, g, b, a);

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
        /// The reflection direction is evaluated at the three vertices with the same expression the
        /// fragment path uses, the face is chosen from their SUM (the triangle's dominant direction,
        /// so all three project onto one face), and the three face-local coordinates in texels then
        /// go through the shared ScreenSpaceTexelRate. A vertex that does not lie in the chosen
        /// face's hemisphere, or a triangle whose normals or eye vector degenerate, reports a rate
        /// of 1 -- magnification, level 0 -- which is the same "deterministic rather than clever"
        /// fallback a degenerate 2D triangle already gets.
        float TriangleCubeTexelRate(const RasterVertex& v0, const RasterVertex& v1, const RasterVertex& v2,
                                    const float* eyeWorld, int faceDim)
        {
            const RasterVertex* verts[3] = {&v0, &v1, &v2};
            Vector3 dirs[3];
            Vector3 sum(0.0f, 0.0f, 0.0f);
            for (int k = 0; k < 3; ++k)
            {
                const RasterVertex& rv = *verts[k];
                const float invW = (rv.invW != 0.0f) ? rv.invW : 1.0f;
                const float wpx = rv.wpx / invW, wpy = rv.wpy / invW, wpz = rv.wpz / invW;
                float nx = rv.nx / invW, ny = rv.ny / invW, nz = rv.nz / invW;
                const float nLen = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (!(nLen > 1e-8f)) return 1.0f;
                nx /= nLen; ny /= nLen; nz /= nLen;

                float ex = eyeWorld[0] - wpx, ey = eyeWorld[1] - wpy, ez = eyeWorld[2] - wpz;
                const float eLen = std::sqrt(ex * ex + ey * ey + ez * ez);
                if (!(eLen > 1e-8f)) return 1.0f;
                ex /= eLen; ey /= eLen; ez /= eLen;

                const float nDotE = nx * ex + ny * ey + nz * ez;
                dirs[k] = Vector3(2.0f * nDotE * nx - ex, 2.0f * nDotE * ny - ey, 2.0f * nDotE * nz - ez);
                sum = Vector3(sum.X + dirs[k].X, sum.Y + dirs[k].Y, sum.Z + dirs[k].Z);
            }

            int face = 0;
            float cs = 0.5f, ct = 0.5f;
            SelectCubeFace(sum, face, cs, ct);

            float s[3], t[3];
            for (int k = 0; k < 3; ++k)
                if (!CubeFaceLocal(dirs[k], face, s[k], t[k])) return 1.0f;

            const float dim = static_cast<float>(std::max(1, faceDim));
            return ScreenSpaceTexelRate(v0, v1, v2,
                                        s[0] * dim, s[1] * dim, s[2] * dim,
                                        t[0] * dim, t[1] * dim, t[2] * dim);
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

        /// A filled polygon split into two triangles must give its shared diagonal to exactly one
        /// triangle.  The edge-function fill is intentionally inclusive on all three edges for an
        /// individual triangle; without this narrow exclusion, an alpha-blended quad draws every
        /// pixel on an exactly representable diagonal twice.  `edgeValues` map to edges V1-V2,
        /// V2-V0 and V0-V1 respectively (the barycentric convention used below).
        [[nodiscard]] bool IsExcludedFillEdge(unsigned excludedEdges, float edgeV1V2,
                                              float edgeV2V0, float edgeV0V1, float area)
        {
            if (excludedEdges == 0u)
                return false;
            // The scale keeps this a boundary test after transformations rather than a thin
            // interior strip, while accepting harmless arithmetic noise on a shared edge.
            const float tolerance = std::max(1.0e-6f, std::fabs(area) * 1.0e-6f);
            return ((excludedEdges & kEdgeV1V2) != 0u && std::fabs(edgeV1V2) <= tolerance) ||
                   ((excludedEdges & kEdgeV2V0) != 0u && std::fabs(edgeV2V0) <= tolerance) ||
                   ((excludedEdges & kEdgeV0V1) != 0u && std::fabs(edgeV0V1) <= tolerance);
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

        /// REMED-GFX-082: walks the integer pixels of a wire edge between two screen-space
        /// RasterVertices, invoking `emit(x, y, t)` per pixel with the ORIGINAL segment parameter t
        /// (so the caller interpolates depth/color/uv perspective-correctly at t). The segment is first
        /// clipped to `clip`, then sampled with a DDA at unit-pixel spacing so the line is gap-free; the
        /// per-pixel float-bounds guard also rejects any non-finite coordinate before the int cast.
        template <typename EmitFn>
        void WalkWireEdge(const RasterClipRect& clip, const RasterVertex& a, const RasterVertex& b,
                          EmitFn&& emit)
        {
            float t0, t1;
            if (!ClipSegmentToRect(a.x, a.y, b.x, b.y, clip, t0, t1))
                return;
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float cax = a.x + t0 * dx, cay = a.y + t0 * dy;
            const float cbx = a.x + t1 * dx, cby = a.y + t1 * dy;
            int steps = static_cast<int>(std::ceil(std::max(std::fabs(cbx - cax), std::fabs(cby - cay))));
            if (steps < 1)
                steps = 1;
            for (int i = 0; i <= steps; ++i)
            {
                const float s = static_cast<float>(i) / static_cast<float>(steps);
                const float t = t0 + s * (t1 - t0);
                const float px = a.x + t * dx;
                const float py = a.y + t * dy;
                if (!(px >= static_cast<float>(clip.minX) && px <= static_cast<float>(clip.maxX) + 1.0f &&
                      py >= static_cast<float>(clip.minY) && py <= static_cast<float>(clip.maxY) + 1.0f))
                    continue;  // also rejects NaN/inf (all comparisons false) before the int cast
                emit(static_cast<int>(std::floor(px)), static_cast<int>(std::floor(py)), t);
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
                                         const RasterClipRect& clip, int x, int y,
                                         float depth, float invW, float pr, float pg, float pb, float pa,
                                         int colorWriteMask, unsigned int multiSampleMask)
        {
            if (x < clip.minX || x > clip.maxX || y < clip.minY || y > clip.maxY)
                return;
            // REMED-GFX-077: Software is single-sample, so only MultiSampleMask bit 0 is meaningful.
            // Bit 0 clear ⇒ the one sample is not covered ⇒ nothing is written (neither colour nor
            // depth), matching XNA "no samples written". Default (0xFFFFFFFF) keeps bit 0 set.
            if ((multiSampleMask & 1u) == 0u)
                return;
            if (g_samplerTrace.enabled) { g_samplerTrace.fragX = x; g_samplerTrace.fragY = y; }
            const std::size_t pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.width) +
                                           static_cast<std::size_t>(x);
            // REMED-GFX-030: comparison precedes every color/depth write.
            if (depthState.testEnabled)
            {
                if (fb.depthBuffer.empty())
                    throw std::logic_error(
                        "Software rasterizer received enabled depth state without depth storage.");
                if (!DepthFragmentPasses(depthState, depth, fb.depthBuffer[pixelIndex]))
                    return;
            }
            const float r = pr / invW, g = pg / invW, b = pb / invW, a = pa / invW;
            // Depth is written independently of the colour write mask (REMED-GFX-077 Phase 11:
            // ColorWriteChannels controls only colour writes, never depth). REMED-GFX-030: a
            // passing fragment updates depth only when DepthBufferWriteEnable is true.
            WritePassingDepth(fb, depthState, pixelIndex, depth);
            const std::size_t colorIndex = pixelIndex * 4;
            // REMED-GFX-077: gate each channel by BlendState.ColorWriteChannels — a masked-off channel
            // keeps its existing destination byte (identity), the XNA semantic.
            if (ColorWriteHasRed  (colorWriteMask)) fb.color[colorIndex + 0] = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
            if (ColorWriteHasGreen(colorWriteMask)) fb.color[colorIndex + 1] = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
            if (ColorWriteHasBlue (colorWriteMask)) fb.color[colorIndex + 2] = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
            if (ColorWriteHasAlpha(colorWriteMask)) fb.color[colorIndex + 3] = static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
        }

        /// Fills one triangle into `fb` using a standard edge-function/barycentric rasterizer,
        /// with a per-pixel depth test/write against `fb.depthBuffer` per `depthState` and
        /// backface culling per `cullMode` (SOFTWARE-81; raw ordinal, see ShouldCullTriangle()).
        /// REMED-GFX-082: when `wireframe`, only the edges selected by `edgeMask` are rasterized
        /// (line walk) instead of the interior fill -- culling and the zero-area reject are shared, so
        /// a culled/degenerate triangle emits no wire either.
        void RasterizeTriangle(SoftwareFramebuffer& fb, const RasterDepthState& depthState, int cullMode,
                               float depthBias, float slopeScaleDepthBias,
                               const RasterClipRect& clip,
                               const RasterVertex& v0, const RasterVertex& v1, const RasterVertex& v2,
                               int colorWriteMask, unsigned int multiSampleMask,
                               bool wireframe = false, unsigned edgeMask = kEdgeAll,
                               unsigned fillExcludedEdges = 0u)
        {
            const float area = EdgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
            if (area == 0.0f)
                return;  // degenerate (zero-area) triangle
            if (ShouldCullTriangle(area, cullMode))
                return;

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
                    WalkWireEdge(clip, A, B, [&](int x, int y, float t) {
                        const float invW  = A.invW  + t * (B.invW  - A.invW);
                        float depth = A.depth + t * (B.depth - A.depth);
                        if (hasBias) depth = std::clamp(depth + biasOffset, 0.0f, 1.0f);  // REMED-GFX-083
                        WriteColoredFragment(fb, depthState, clip, x, y, depth, invW,
                                             A.r + t * (B.r - A.r), A.g + t * (B.g - A.g),
                                             A.b + t * (B.b - A.b), A.a + t * (B.a - A.a),
                                             colorWriteMask, multiSampleMask);
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

                    const bool inside = (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) ||
                                        (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
                    if (!inside)
                        continue;
                    if (IsExcludedFillEdge(fillExcludedEdges, w0, w1, w2, area))
                        continue;

                    const float lambda0 = w0 / area;
                    const float lambda1 = w1 / area;
                    const float lambda2 = w2 / area;

                    // Post-divide depth interpolates linearly in screen space -- no perspective
                    // correction needed for this one attribute (a well-known rasterization
                    // property), unlike color/UV below.
                    float depth = lambda0 * v0.depth + lambda1 * v1.depth + lambda2 * v2.depth;
                    if (hasBias) depth = std::clamp(depth + biasOffset, 0.0f, 1.0f);  // REMED-GFX-083
                    const float invW = lambda0 * v0.invW + lambda1 * v1.invW + lambda2 * v2.invW;
                    WriteColoredFragment(fb, depthState, clip, x, y, depth, invW,
                                         lambda0 * v0.r + lambda1 * v1.r + lambda2 * v2.r,
                                         lambda0 * v0.g + lambda1 * v1.g + lambda2 * v2.g,
                                         lambda0 * v0.b + lambda1 * v1.b + lambda2 * v2.b,
                                         lambda0 * v0.a + lambda1 * v1.a + lambda2 * v2.a,
                                         colorWriteMask, multiSampleMask);
                }
            }
        }
#endif

        // ---- Phase S5/S6: generalized (textured/blended/effect-driven) rasterization ----

#ifndef CNA_SOFTWARE_2D_ONLY
        /// Transforms a vertex whose byte layout is inferred from `stride` (plan_software.md
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
        /// clipping (SOFTWARE-83) happens on ClipVertex, before the perspective divide.
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
            std::array<const std::uint8_t*, kMaxVertexStreams> recordBase{};

            [[nodiscard]] const std::uint8_t* At(int combinedByteOffset) const
            {
                const GpuVertexStreamSlot slot =
                    MapCombinedOffsetToStream(*params, combinedByteOffset);
                return recordBase[static_cast<std::size_t>(slot.streamIndex)] +
                       slot.byteOffsetInStream;
            }
        };

        ClipVertex BuildGenericClipVertex(const CombinedVertexReader& raw, std::size_t stride,
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

            const Vector4 clip = Vector4::Transform(position, combined);

            ClipVertex out;
            out.x = clip.X; out.y = clip.Y; out.z = clip.Z; out.w = clip.W;

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
                    std::memcpy(&out.u, raw.At(48), sizeof(float));
                    std::memcpy(&out.v, raw.At(52), sizeof(float));
                }
                // plan_gltf.md GLTF-462: stride 60's last four bytes were reserved padding and are
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
                    std::memcpy(&out.u, raw.At(68), sizeof(float));
                    std::memcpy(&out.v, raw.At(72), sizeof(float));
                }
                // plan_gltf.md GLTF-463: stride 80 is the stride-76 skinned PBR record with a packed
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

            if (haveNormal && params.envMapping)
            {
                // World-space position/normal for the reflection vector (SOFTWARE-82). Uses
                // World directly rather than the mathematically-correct WorldInverseTranspose for
                // the normal -- an intentional simplification, exact for uniform-scale/no-shear
                // World matrices and only distorting the reflection for non-uniform scale, which
                // this renderer's own existing "correctness over full fidelity" stance accepts.
                const Vector3 worldPos = ApplyAffineColumnMajor(params.worldColMajor, position, 1.0f);
                Vector3 worldNormal = ApplyAffineColumnMajor(params.worldColMajor, normal, 0.0f);
                const float len = std::sqrt(worldNormal.X * worldNormal.X + worldNormal.Y * worldNormal.Y +
                                            worldNormal.Z * worldNormal.Z);
                if (len > 1e-8f)
                {
                    worldNormal.X /= len; worldNormal.Y /= len; worldNormal.Z /= len;
                }
                out.wpx = worldPos.X; out.wpy = worldPos.Y; out.wpz = worldPos.Z;
                out.nx = worldNormal.X; out.ny = worldNormal.Y; out.nz = worldNormal.Z;
            }

            if (!params.vertexColorEnabled)
            {
                out.r = out.g = out.b = out.a = 1.0f;
            }
            return out;
        }
#endif

        /// Builds a RasterVertex directly from already-final screen-space pixel coordinates, with
        /// no perspective divide needed (invW=1) -- used by SpriteBatch's own 2D quads, which are
        /// placed directly in screen space rather than going through World*View*Projection.
        RasterVertex MakeScreenSpaceVertex(float x, float y, float depth,
                                           float r, float g, float b, float a, float u, float v)
        {
            RasterVertex out;
            out.x = x;
            out.y = y;
            out.depth = depth;
            out.invW = 1.0f;
            out.r = r; out.g = g; out.b = b; out.a = a;
            out.u = u; out.v = v;
            return out;
        }

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
            const SoftwareTextureCubeRenderer* envMap;
            bool useDualTexture;
            bool useEnvMap;
            bool needUV;
            SoftwareBlendState blendState;
            std::array<float, 4> blendFactor;
            RasterDepthState depthState; // REMED-GFX-030: per-draw test/write/function snapshot
            RasterStencilState stencilState; // GDI-026: per-draw 8-bit stencil snapshot
            int colorWriteMask;           // REMED-GFX-077: raw XNA ColorWriteChannels (bit0=R..bit3=A)
            unsigned int multiSampleMask; // REMED-GFX-077: single-sample ⇒ only bit 0 is meaningful
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
            // REMED-GFX-182: the reflection cube's own magnification classification and
            // level-of-detail. The SAMPLER is `sampler1` -- the cube and DualTextureEffect's second
            // texture share slot 1, exactly as they share binding 1 on every GPU renderer -- but the
            // FOOTPRINT cannot be shared: `lambda1` is derived from `texture1`, which an
            // EnvironmentMapEffect draw does not bind at all, and a cube's footprint comes from the
            // reflection vector rather than from the UV attribute.
            bool magnifyCube;
            float lambdaCube;
        };

        /// REMED-GFX-082: writes one already-interpolated shaded fragment -- the whole texture/diffuse/
        /// dual-texture/env-map/blend pipeline that used to live inline in RasterizeTriangleShaded's
        /// fill loop. `p*` are the perspective-premultiplied attribute sums (attr * invW), divided by
        /// invW here exactly as before, so the Solid fill stays byte-identical and the WireFrame line
        /// walk gets the identical shading along its edges.
        inline void WriteShadedFragment(SoftwareFramebuffer& fb, const ShadedContext& ctx,
                                        const RasterClipRect& clip, int x, int y, float depth, float invW,
                                        float pr, float pg, float pb, float pa, float pu, float pv,
                                        float pwpx, float pwpy, float pwpz, float pnx, float pny, float pnz,
                                        unsigned int coverageMask = 0xFFFFFFFFu)
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
            if (ctx.stencilState.testEnabled && fb.stencilBuffer.empty())
                throw std::logic_error(
                    "Software rasterizer received enabled stencil state without stencil storage.");
            // Stencil runs before depth and shading, matching the GPU fragment-test order. GDI-073
            // deliberately keeps one stencil byte per PIXEL: after sample-mask/coverage rejection,
            // this comparison/operation runs once for the triangle fragment and gates its complete
            // active colour-sample set. It does not claim a per-sample depth/stencil attachment.
            // A failed stencil test updates only StencilFail and must not reach depth or colour.
            if (ctx.stencilState.testEnabled && !StencilComparisonPasses(
                    ctx.stencilState.reference, fb.stencilBuffer[pixelIndex],
                    ctx.stencilState.readMask, ctx.stencilState.compareFunction))
            {
                WriteStencil(fb, ctx.stencilState, pixelIndex, ctx.stencilState.failOperation);
                return;
            }
            // REMED-GFX-030: comparison precedes shading and every color/depth write.
            if (ctx.depthState.testEnabled && fb.depthBuffer.empty())
                throw std::logic_error(
                    "Software rasterizer received enabled depth state without depth storage.");
            if (ctx.depthState.testEnabled &&
                !DepthFragmentPasses(ctx.depthState, depth, fb.depthBuffer[pixelIndex]))
            {
                if (ctx.stencilState.testEnabled)
                    WriteStencil(fb, ctx.stencilState, pixelIndex,
                                 ctx.stencilState.depthFailOperation);
                return;
            }

            if (ctx.stencilState.testEnabled)
                WriteStencil(fb, ctx.stencilState, pixelIndex, ctx.stencilState.passOperation);

            float r = pr / invW, g = pg / invW, b = pb / invW, a = pa / invW;

            float u = 0.0f, v = 0.0f;
            if (ctx.needUV)
            {
                u = pu / invW;
                v = pv / invW;
            }

            if (ctx.useDualTexture)
            {
                // DualTextureEffect (SOFTWARE-82): color.rgb*=2; color *= overlay*diffuse
                // (FNA's PSDualTexture) -- both textures reuse the SAME uv (this renderer has no
                // genuine 2-UV vertex format; established precedent already set by this codebase's own
                // Vulkan dual_texture3d shaders).
                float t0r, t0g, t0b, t0a;
                SampleTexture(*ctx.texture0, ctx.sampler0, ctx.magnify0, ctx.lambda0, u, v,
                              t0r, t0g, t0b, t0a);
                float t1r, t1g, t1b, t1a;
                SampleTexture(*ctx.texture1, ctx.sampler1, ctx.magnify1, ctx.lambda1, u, v,
                              t1r, t1g, t1b, t1a);
                r *= (t0r * 2.0f) * t1r;
                g *= (t0g * 2.0f) * t1g;
                b *= (t0b * 2.0f) * t1b;
                a *= t0a * t1a;
            }
            else if (ctx.params.textureEnabled && ctx.texture0 != nullptr)
            {
                float texR, texG, texB, texA;
                SampleTexture(*ctx.texture0, ctx.sampler0, ctx.magnify0, ctx.lambda0, u, v,
                              texR, texG, texB, texA);
                r *= texR;
                g *= texG;
                b *= texB;
                a *= texA;
            }

            r *= ctx.params.diffuseColor[0];
            g *= ctx.params.diffuseColor[1];
            b *= ctx.params.diffuseColor[2];
            a *= ctx.params.diffuseColor[3];

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
                // EnvironmentMapEffect (SOFTWARE-82), FNA's PSEnvMap/PSEnvMapSpecular formula, minus
                // the per-light diffuse sum (design decision 6): base color is what r/g/b/a already
                // are at this point (vertexColor*diffuseColor*texture0), used as-is.
                const float wpx = pwpx / invW;
                const float wpy = pwpy / invW;
                const float wpz = pwpz / invW;
                float nx = pnx / invW;
                float ny = pny / invW;
                float nz = pnz / invW;
                const float nLen = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (nLen > 1e-8f) { nx /= nLen; ny /= nLen; nz /= nLen; }

                float ex = ctx.params.eyePositionWorld[0] - wpx;
                float ey = ctx.params.eyePositionWorld[1] - wpy;
                float ez = ctx.params.eyePositionWorld[2] - wpz;
                const float eLen = std::sqrt(ex * ex + ey * ey + ez * ez);
                if (eLen > 1e-8f) { ex /= eLen; ey /= eLen; ez /= eLen; }

                // reflect(-E, N) = 2*dot(N,E)*N - E (HLSL's reflect(I,N) = I-2*dot(N,I)*N with I=-E).
                const float nDotE = nx * ex + ny * ey + nz * ez;
                const Vector3 reflDir(2.0f * nDotE * nx - ex, 2.0f * nDotE * ny - ey, 2.0f * nDotE * nz - ez);
                float envR, envG, envB, envA;
                // REMED-GFX-182: the cube is filtered by the PUBLIC SamplerStates[1] this draw
                // captured, through the same sampler every ordinary texture goes through.
                SampleCubeMap(*ctx.envMap, ctx.sampler1, ctx.magnifyCube, ctx.lambdaCube, reflDir,
                              envR, envG, envB, envA);

                const float viewAngle = nDotE;
                const float blendFactor = ctx.params.fresnelEnabled
                    ? std::pow(std::max(1.0f - std::abs(viewAngle), 0.0f), ctx.params.fresnelFactor) * ctx.params.envMapAmount
                    : ctx.params.envMapAmount;

                r = r * (1.0f - blendFactor) + (envR * a) * blendFactor + ctx.params.envMapSpecular[0] * envA * a;
                g = g * (1.0f - blendFactor) + (envG * a) * blendFactor + ctx.params.envMapSpecular[1] * envA * a;
                b = b * (1.0f - blendFactor) + (envB * a) * blendFactor + ctx.params.envMapSpecular[2] * envA * a;

                if (g_cubeTrace.enabled) PrintCubeTraceLine(r, g, b);   // REMED-GFX-182
            }
#endif

            // Depth is written independently of the colour write mask (REMED-GFX-077 Phase 11:
            // ColorWriteChannels never gates depth — only the colour channels below).
            // REMED-GFX-030: DepthRead reaches this point but leaves stored depth untouched.
            WritePassingDepth(fb, ctx.depthState, pixelIndex, depth);

            // REMED-GFX-077: final colour channels (opaque store or exact XNA blend result). Each channel is
            // gated by BlendState.ColorWriteChannels — a masked-off channel keeps its existing
            // destination byte (identity), applied AFTER blending (Phase 10). The common All(15)
            // path writes every channel exactly as before.
            const std::array<float, 4> source{r, g, b, a};
            const auto writeBlendedColor = [&](std::uint8_t* destinationBytes) {
                std::array<float, 4> output = source;
                if (!ctx.blendState.IsOpaqueIdentity())
                {
                    const std::array<float, 4> destination{
                        destinationBytes[0] / 255.0f, destinationBytes[1] / 255.0f,
                        destinationBytes[2] / 255.0f, destinationBytes[3] / 255.0f,
                    };
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
                for (float& channel : output)
                    channel = std::clamp(channel, 0.0f, 1.0f);
                if (ColorWriteHasRed(ctx.colorWriteMask))
                    destinationBytes[0] = static_cast<std::uint8_t>(output[0] * 255.0f);
                if (ColorWriteHasGreen(ctx.colorWriteMask))
                    destinationBytes[1] = static_cast<std::uint8_t>(output[1] * 255.0f);
                if (ColorWriteHasBlue(ctx.colorWriteMask))
                    destinationBytes[2] = static_cast<std::uint8_t>(output[2] * 255.0f);
                if (ColorWriteHasAlpha(ctx.colorWriteMask))
                    destinationBytes[3] = static_cast<std::uint8_t>(output[3] * 255.0f);
            };
            if (!fb.HasMultiSampleColor())
            {
                writeBlendedColor(fb.color.data() + pixelIndex * 4u);
                return;
            }
            for (int sample = 0; sample < 4; ++sample)
            {
                if ((activeSamples & (1u << sample)) == 0u)
                    continue;
                writeBlendedColor(fb.multiSampleColor.data() +
                                  (pixelIndex * 4u + static_cast<std::size_t>(sample)) * 4u);
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
            const SoftwareSamplerState& sampler1)
        {
            const auto* texture0 = dynamic_cast<const SoftwareColorSurface*>(params.texture0);
            const auto* texture1 = dynamic_cast<const SoftwareColorSurface*>(params.texture1);
#ifndef CNA_SOFTWARE_2D_ONLY
            const auto* envMap = dynamic_cast<const SoftwareTextureCubeRenderer*>(params.envMap);
#else
            const SoftwareTextureCubeRenderer* envMap = nullptr;
#endif
            const bool useDualTexture = params.dualTexture && texture0 != nullptr && texture1 != nullptr;
#ifndef CNA_SOFTWARE_2D_ONLY
            const bool useEnvMap = params.envMapping && envMap != nullptr;
#else
            constexpr bool useEnvMap = false;
#endif
            const bool needUV = useDualTexture || useEnvMap ||
                                (params.textureEnabled && texture0 != nullptr);
            return ShadedContext{params, texture0, texture1, envMap, useDualTexture, useEnvMap,
                                 needUV, blendState, blendFactor, depthState, stencilState,
                                 colorWriteMask, multiSampleMask, sampler0, sampler1,
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
            const SoftwareSamplerState& sampler0, const SoftwareSamplerState& sampler1)
        {
            const ShadedContext ctx = MakeLinearShadedContext(
                params, depthState, stencilState, blendState, blendFactor, colorWriteMask,
                multiSampleMask, sampler0, sampler1);
            WalkWireEdge(clip, a, b, [&](int x, int y, float t) {
                const float invW = a.invW + t * (b.invW - a.invW);
                WriteShadedFragment(
                    fb, ctx, clip, x, y, a.depth + t * (b.depth - a.depth), invW,
                    a.r + t * (b.r - a.r), a.g + t * (b.g - a.g),
                    a.b + t * (b.b - a.b), a.a + t * (b.a - a.a),
                    a.u + t * (b.u - a.u), a.v + t * (b.v - a.v),
                    a.wpx + t * (b.wpx - a.wpx), a.wpy + t * (b.wpy - a.wpy),
                    a.wpz + t * (b.wpz - a.wpz), a.nx + t * (b.nx - a.nx),
                    a.ny + t * (b.ny - a.ny), a.nz + t * (b.nz - a.nz));
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
            const SoftwareSamplerState& sampler1)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
                return;
            const ShadedContext ctx = MakeLinearShadedContext(
                params, depthState, stencilState, blendState, blendFactor, colorWriteMask,
                multiSampleMask, sampler0, sampler1);
            WriteShadedFragment(fb, ctx, clip,
                                static_cast<int>(std::floor(point.x)),
                                static_cast<int>(std::floor(point.y)),
                                point.depth, point.invW,
                                point.r, point.g, point.b, point.a, point.u, point.v,
                                point.wpx, point.wpy, point.wpz,
                                point.nx, point.ny, point.nz);
        }

        /// General-purpose triangle fill for the DrawPrimitivesEx/DrawIndexedPrimitivesEx and
        /// SpriteBatch paths: adds nearest-neighbor texture sampling, diffuseColor modulation, and
        /// the complete XNA BlendState equation on top of RasterizeTriangle's
        /// depth-tested, perspective-correct color interpolation. Backface culling per `cullMode`
        /// (SOFTWARE-81; raw ordinal, see ShouldCullTriangle()). `params.dualTexture`/`envMapping`
        /// (SOFTWARE-82) select DualTextureEffect's second-texture blend or EnvironmentMapEffect's
        /// cube-map reflection on top of the same base texture/diffuse/vertex-color path -- no
        /// per-light diffuse lighting is computed for either (design decision 6: no lighting
        /// engine in v1), so the "lit" base color is just vertexColor*diffuseColor*texture0, the
        /// same simplification already used for the plain BasicEffect path.
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
                                     bool wireframe = false, unsigned edgeMask = kEdgeAll,
                                     unsigned fillExcludedEdges = 0u)
        {
            // REMED-GFX-124: the cast target is the colour-storage capability, not a concrete
            // renderer class, so both a SoftwareTextureRenderer and a SoftwareRenderTargetRenderer
            // resolve here. A foreign renderer still resolves to nullptr exactly as before.
            const auto* texture0 = dynamic_cast<const SoftwareColorSurface*>(params.texture0);
            const auto* texture1 = dynamic_cast<const SoftwareColorSurface*>(params.texture1);
#ifndef CNA_SOFTWARE_2D_ONLY
            const auto* envMap = dynamic_cast<const SoftwareTextureCubeRenderer*>(params.envMap);
#else
            const SoftwareTextureCubeRenderer* envMap = nullptr;
#endif
            const bool useDualTexture = params.dualTexture && texture0 != nullptr && texture1 != nullptr;
#ifndef CNA_SOFTWARE_2D_ONLY
            const bool useEnvMap = params.envMapping && envMap != nullptr;
#else
            constexpr bool useEnvMap = false;
#endif
            const bool needUV = useDualTexture || useEnvMap || (params.textureEnabled && texture0 != nullptr);
            // REMED-GFX-150: classify magnification once per triangle per bound texture. Only XNA
            // filters 5..8 distinguish the two halves, so this is inert for Point, Linear and
            // Anisotropic; it is computed only when a texture is actually sampled.
            // REMED-GFX-175: one texel rate per bound texture, resolved once per triangle, feeding
            // BOTH the magnification classification REMED-GFX-150 established and the level-of-detail
            // the mip component needs. The two can never disagree because they come from one number.
            const float rho0 = (texture0 != nullptr)
                ? TriangleTexelRate(v0, v1, v2, std::max(1, texture0->ColorWidth()),
                                    std::max(1, texture0->ColorHeight()))
                : 1.0f;
            const float rho1 = (texture1 != nullptr)
                ? TriangleTexelRate(v0, v1, v2, std::max(1, texture1->ColorWidth()),
                                    std::max(1, texture1->ColorHeight()))
                : 1.0f;
            const bool magnify0 = !(rho0 > 1.0f);
            const bool magnify1 = !(rho1 > 1.0f);
            // REMED-GFX-182: the cube's own footprint, resolved once per triangle from the SAME
            // reflection expression the fragment path uses and only when a cube is actually bound.
#ifndef CNA_SOFTWARE_2D_ONLY
            const float rhoCube = useEnvMap
                ? TriangleCubeTexelRate(v0, v1, v2, params.eyePositionWorld,
                                        std::max(1, envMap->GetSize()))
                : 1.0f;
#else
            constexpr float rhoCube = 1.0f;
#endif
            const ShadedContext ctx{params, texture0, texture1, envMap, useDualTexture, useEnvMap,
                                    needUV, blendState, blendFactor,
                                    depthState, stencilState, colorWriteMask, multiSampleMask,
                                    sampler0, sampler1, magnify0, magnify1,
                                    LodFromTexelRate(rho0), LodFromTexelRate(rho1),
                                    !(rhoCube > 1.0f), LodFromTexelRate(rhoCube)};

            const float area = EdgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
            if (area == 0.0f)
                return;
            if (ShouldCullTriangle(area, cullMode))
                return;
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
                // GDI-073: the established DDA visits whole pixels and intentionally omits a
                // geometric coverage mask, so each visited wire pixel writes every sample enabled
                // by MultiSampleMask when a sample plane is active. This is crisp pixel wireframe,
                // not subpixel line AA.
                const auto drawEdge = [&](const RasterVertex& A, const RasterVertex& B) {
                    WalkWireEdge(clip, A, B, [&](int x, int y, float t) {
                        const float invW  = A.invW  + t * (B.invW  - A.invW);
                        float depth = A.depth + t * (B.depth - A.depth);
                        if (hasBias) depth = std::clamp(depth + biasOffset, 0.0f, 1.0f);  // REMED-GFX-083
                        WriteShadedFragment(fb, ctx, clip, x, y, depth, invW,
                                            A.r + t * (B.r - A.r), A.g + t * (B.g - A.g),
                                            A.b + t * (B.b - A.b), A.a + t * (B.a - A.a),
                                            A.u + t * (B.u - A.u), A.v + t * (B.v - A.v),
                                            A.wpx + t * (B.wpx - A.wpx), A.wpy + t * (B.wpy - A.wpy),
                                            A.wpz + t * (B.wpz - A.wpz), A.nx + t * (B.nx - A.nx),
                                            A.ny + t * (B.ny - A.ny), A.nz + t * (B.nz - A.nz));
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
                    if (!fb.HasMultiSampleColor())
                    {
                        const bool inside = (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) ||
                                            (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
                        if (!inside || IsExcludedFillEdge(fillExcludedEdges, w0, w1, w2, area))
                            continue;
                    }
                    else
                    {
                        // Four actual coverage samples at (1/4,1/4), (3/4,1/4),
                        // (1/4,3/4), (3/4,3/4). A partially covered edge therefore blends only
                        // its covered samples and ResolveColor() averages them before GDI blits.
                        coverageMask = 0u;
                        for (int sample = 0; sample < 4; ++sample)
                        {
                            const float sampleX = static_cast<float>(x) +
                                ((sample & 1) == 0 ? 0.25f : 0.75f);
                            const float sampleY = static_cast<float>(y) +
                                (sample < 2 ? 0.25f : 0.75f);
                            const float sampleW0 = EdgeFunction(v1.x, v1.y, v2.x, v2.y,
                                                                sampleX, sampleY);
                            const float sampleW1 = EdgeFunction(v2.x, v2.y, v0.x, v0.y,
                                                                sampleX, sampleY);
                            const float sampleW2 = EdgeFunction(v0.x, v0.y, v1.x, v1.y,
                                                                sampleX, sampleY);
                            const bool sampleInside =
                                (sampleW0 >= 0.0f && sampleW1 >= 0.0f && sampleW2 >= 0.0f) ||
                                (sampleW0 <= 0.0f && sampleW1 <= 0.0f && sampleW2 <= 0.0f);
                            if (sampleInside && !IsExcludedFillEdge(fillExcludedEdges, sampleW0,
                                                                    sampleW1, sampleW2, area))
                                coverageMask |= 1u << sample;
                        }
                        if (coverageMask == 0u)
                            continue;
                    }

                    const float lambda0 = w0 / area;
                    const float lambda1 = w1 / area;
                    const float lambda2 = w2 / area;

                    float depth = lambda0 * v0.depth + lambda1 * v1.depth + lambda2 * v2.depth;
                    if (hasBias) depth = std::clamp(depth + biasOffset, 0.0f, 1.0f);  // REMED-GFX-083
                    const float invW = lambda0 * v0.invW + lambda1 * v1.invW + lambda2 * v2.invW;
                    WriteShadedFragment(fb, ctx, clip, x, y, depth, invW,
                                        lambda0 * v0.r + lambda1 * v1.r + lambda2 * v2.r,
                                        lambda0 * v0.g + lambda1 * v1.g + lambda2 * v2.g,
                                        lambda0 * v0.b + lambda1 * v1.b + lambda2 * v2.b,
                                        lambda0 * v0.a + lambda1 * v1.a + lambda2 * v2.a,
                                        lambda0 * v0.u + lambda1 * v1.u + lambda2 * v2.u,
                                        lambda0 * v0.v + lambda1 * v1.v + lambda2 * v2.v,
                                        lambda0 * v0.wpx + lambda1 * v1.wpx + lambda2 * v2.wpx,
                                        lambda0 * v0.wpy + lambda1 * v1.wpy + lambda2 * v2.wpy,
                                        lambda0 * v0.wpz + lambda1 * v1.wpz + lambda2 * v2.wpz,
                                        lambda0 * v0.nx + lambda1 * v1.nx + lambda2 * v2.nx,
                                        lambda0 * v0.ny + lambda1 * v1.ny + lambda2 * v2.ny,
                                        lambda0 * v0.nz + lambda1 * v1.nz + lambda2 * v2.nz,
                                        coverageMask);
                }
            }
        }

#ifndef CNA_SOFTWARE_2D_ONLY
        // ---- REMED-GFX-110: indexed addressing and bounds ----
        //
        // Shared by both CPU indexed raster paths so they cannot drift apart again. The public
        // contract reconciled by REMED-GFX-106 is:
        //
        //   consumed element  = startIndex + localIndex          (an ELEMENT offset, never bytes)
        //   decoded index     = 16- or 32-bit value at that element, per the buffer's own width
        //   fetched vertex    = decoded index + baseVertex       (added exactly once)
        //
        // minVertexIndex/numVertices are validation hints: they never add to a decoded index,
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

        /// Validates the complete consumed range -- including every decoded vertex address --
        /// before a single vertex byte is read, so an out-of-range index can never form an
        /// invalid pointer into the CPU vertex storage. All arithmetic is 64-bit, so neither a
        /// large primitiveCount nor an index near UINT32_MAX can wrap into an apparently valid
        /// address. Throws the public CNA range exception rather than any implementation type.
        /// REMED-GFX-201: the element count every bound per-vertex stream can satisfy, once each
        /// stream's own binding offset is deducted. A multi-stream draw addresses every stream with
        /// the same element number, so the shortest stream bounds the draw -- validating only the
        /// one `vb` names would let a short secondary stream be read past its end.
        int SmallestAddressableVertexCount(const GpuDrawParams& params, int fallback)
        {
            if (params.vertexStreamCount == 0)
                return fallback;
            int smallest = fallback;
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency != 0)
                    continue;
                smallest = std::min(smallest, stream.vertexCount - stream.vertexOffset);
            }
            return std::max(smallest, 0);
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
            // CNA's public contract rejects a negative baseVertex before renderer dispatch; the
            // CPU paths address real host storage, so they re-assert it rather than trust it.
            if (baseVertex < 0)
            {
                throw System::ArgumentOutOfRangeException(
                    "baseVertex", std::to_string(baseVertex),
                    "baseVertex must not be negative.");
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

        /// Validates the complete consumed vertex range before a single vertex byte is read, so an
        /// out-of-buffer request can never form an invalid pointer into the CPU vertex storage. All
        /// arithmetic is 64-bit, so neither an extreme primitiveCount nor a large vertexStart can
        /// wrap into an apparently valid address, and the stride multiply happens only afterwards.
        /// Throws the public CNA range exception rather than any implementation type, and rejects
        /// rather than clamps.
        void ValidateNonIndexedAddressing(int availableVertexCount,
                                          std::int64_t consumedVertexCount, int vertexStart)
        {
            // CNA's public contract rejects a negative vertexStart before renderer dispatch; the CPU
            // paths address real host storage, so they re-assert it rather than trust it.
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

    SoftwareTextureCubeRenderer::SoftwareTextureCubeRenderer(int size, bool mipMap)
        : size_(size)
        , levelCount_(mipMap ? CalculateCubeMipLevels(size) : 1)
    {
        levels_.resize(static_cast<std::size_t>(levelCount_));
        supplied_.assign(static_cast<std::size_t>(levelCount_), std::array<bool, 6>{});
        for (int level = 0; level < levelCount_; ++level)
        {
            const int dim = LevelDim(level);
            const std::size_t faceBytes = static_cast<std::size_t>(dim) * static_cast<std::size_t>(dim) * 4u;
            for (auto& face : levels_[static_cast<std::size_t>(level)])
                face.assign(faceBytes, 0u);
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
        // REMED-GFX-135: `level != 0` used to be a silent early `return` -- the shared layer had no
        // way to tell that apart from a completed upload, so a mipmapped cube accepted every level
        // and kept only level 0. Every level TextureCube declares now has real storage, and
        // anything outside the chain is refused rather than swallowed.
        if (data == nullptr || face < 0 || face > 5) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int dim = LevelDim(level);
        if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > dim || y + h > dim) return false;
        if (dataLength < w * h * 4) return false;

        const auto* src = static_cast<const std::uint8_t*>(data);
        std::vector<std::uint8_t>& pixels =
            levels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)];
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        for (int row = 0; row < h; ++row)
        {
            const std::size_t dstOffset = (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(dim) +
                                          static_cast<std::size_t>(x)) * 4u;
            std::copy(src + static_cast<std::size_t>(row) * rowBytes,
                     src + static_cast<std::size_t>(row) * rowBytes + rowBytes,
                     pixels.begin() + static_cast<std::ptrdiff_t>(dstOffset));
        }

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

    bool SoftwareTextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                             void* data, int dataLength) const
    {
        // REMED-GFX-130: every rejection below used to be a silent `return`, which the shared layer
        // turned into a complete transparent-black face rather than a refusal.
        if (data == nullptr || face < 0 || face > 5)
            return false;
        if (level < 0 || level >= levelCount_)
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

#endif

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
        const Vector2& c0, const Vector2& c1, const Vector2& c2, const Vector2& c3,
        float layerDepth, float r, float g, float b, float a,
        float u1, float v1, float u2, float v2,
        Effect* customEffect, const SoftwareSamplerState& spriteSampler)
    {
        SoftwareFramebuffer& fb = CurrentFramebuffer();
        int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
        GetActiveViewport(vpX, vpY, vpW, vpH);
        const RasterVertex rv0 = MakeScreenSpaceVertex(c0.X, c0.Y, layerDepth, r, g, b, a, u1, v1);
        const RasterVertex rv1 = MakeScreenSpaceVertex(c1.X, c1.Y, layerDepth, r, g, b, a, u2, v1);
        const RasterVertex rv2 = MakeScreenSpaceVertex(c2.X, c2.Y, layerDepth, r, g, b, a, u2, v2);
        const RasterVertex rv3 = MakeScreenSpaceVertex(c3.X, c3.Y, layerDepth, r, g, b, a, u1, v2);

        // REMED-GFX-030: snapshot the complete depth tuple for this submitted sprite draw.
        const RasterDepthState depthState{IsDepthTestEnabled(),
                                          IsDepthWriteEnabled(), GetDepthCompareFunction()};
        const RasterStencilState stencilState{
            IsStencilTestEnabled(), GetStencilCompareFunction(),
            GetStencilPassOperation(), GetStencilFailOperation(),
            GetStencilDepthFailOperation(),
            static_cast<std::uint8_t>(GetStencilReadMask()),
            static_cast<std::uint8_t>(GetStencilWriteMask()),
            static_cast<std::uint8_t>(GetReferenceStencil())};
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
        const float quadMinX = std::min({c0.X, c1.X, c2.X, c3.X});
        const float quadMinY = std::min({c0.Y, c1.Y, c2.Y, c3.Y});
        const float quadMaxX = std::max({c0.X, c1.X, c2.X, c3.X});
        const float quadMaxY = std::max({c0.Y, c1.Y, c2.Y, c3.Y});
        int damageMinX = 0, damageMinY = 0, damageMaxX = -1, damageMaxY = -1;
        if (CalculateRasterBounds(quadMinX, quadMinY, quadMaxX, quadMaxY, clip,
                                  damageMinX, damageMinY, damageMaxX, damageMaxY))
        {
            OnSpriteRasterBounds(damageMinX, damageMinY, damageMaxX, damageMaxY);
        }
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
        RasterizeTriangleShaded(fb, depthState, stencilState, blendState, blendFactor,
                                cullMode, depthBias, slopeScaleDepthBias,
                                spriteParams, clip, rv0, rv1, rv2,
                                GetColorWriteMask(), GetMultiSampleMask(),
                                spriteSampler, spriteSampler, wire, kEdgeAll, kEdgeV2V0);
        RasterizeTriangleShaded(fb, depthState, stencilState, blendState, blendFactor,
                                cullMode, depthBias, slopeScaleDepthBias,
                                spriteParams, clip, rv2, rv3, rv0,
                                GetColorWriteMask(), GetMultiSampleMask(),
                                spriteSampler, spriteSampler, wire, kEdgeAll);
    }

    std::unique_ptr<ITextureCubeRenderer> SoftwareRenderer::CreateTextureCube(int size, bool mipMap, int)
    {
#ifdef CNA_SOFTWARE_2D_ONLY
        (void)size;
        (void)mipMap;
        throw System::NotSupportedException(
            "Software's GDI 2D compilation unit does not include TextureCube resources.");
#else
        // REMED-GFX-135: `mipMap` used to be discarded here, so a mipmapped TextureCube reported a
        // LevelCount whose storage did not exist and every mip upload was dropped in silence.
        return std::make_unique<SoftwareTextureCubeRenderer>(size, mipMap);
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

    // Phase S4 (SOFTWARE-30..34): real transform/rasterize/depth-test pipeline. TriangleList only
    // in v1 (the owner's own stated minimal first-version scope) -- other PrimitiveType values
    // throw rather than silently misrendering.
#ifndef CNA_SOFTWARE_2D_ONLY
    void SoftwareRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world,
                                                        const Matrix& view, const Matrix& projection,
                                                        PrimitiveType primitive, int primitiveCount)
    {
        if (primitiveCount <= 0)
            throw std::runtime_error("SoftwareRenderer::DrawColoredPrimitives: primitiveCount must be > 0");
        if (primitive != PrimitiveType::TriangleList)
            throw std::runtime_error("SoftwareRenderer::DrawColoredPrimitives: only TriangleList is supported in v1");

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
        // REMED-GFX-079: map NDC over the active GraphicsDevice.Viewport (X/Y offset, Width/Height
        // sub-scale, MinDepth/MaxDepth range) and clip rasterization to framebuffer ∩ Viewport --
        // a default full-target viewport reduces to the pre-GFX-079 full-framebuffer mapping.
        int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
        float vpMinDepth = 0.0f, vpMaxDepth = 1.0f;
        GetActiveViewportRaster(vpX, vpY, vpW, vpH, vpMinDepth, vpMaxDepth);
        const ViewportTransform vpT{static_cast<float>(vpX), static_cast<float>(vpY),
                                    static_cast<float>(vpW), static_cast<float>(vpH),
                                    vpMinDepth, vpMaxDepth};
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
                const std::uint8_t* raw = fetchVertex(static_cast<std::int64_t>(i) * 3 + k);
                cv[k] = BuildPositionColorClipVertex(raw, combined);
            }

            ClipVertex clipped[4];
            const int clippedCount = ClipTriangleNearPlane(cv, clipped);  // SOFTWARE-83
            if (clippedCount == 0)
                continue;

            RasterVertex rv[4];
            for (int k = 0; k < clippedCount; ++k)
                rv[k] = ClipVertexToRasterVertex(clipped[k], vpT);

            // REMED-GFX-082: FillMode.WireFrame outlines the visible clipped polygon. For a near-plane
            // clipped quad (clippedCount==4) the two fan triangles share the diagonal rv0-rv2, so each
            // masks that shared edge -- only the real polygon boundary rv0-rv1-rv2-rv3 is drawn.
            const bool wire = (fillMode_ == 1);
            const unsigned mask0 = (clippedCount == 4) ? (kEdgeV0V1 | kEdgeV1V2) : kEdgeAll;
            RasterizeTriangle(fb, depthState, cullMode_, depthBias_, slopeScaleDepthBias_, clip,
                              rv[0], rv[1], rv[2], colorWriteMask_, multiSampleMask_, wire, mask0,
                              clippedCount == 4 ? kEdgeV2V0 : 0u);
            if (clippedCount == 4)
                RasterizeTriangle(fb, depthState, cullMode_, depthBias_, slopeScaleDepthBias_, clip,
                                  rv[0], rv[2], rv[3], colorWriteMask_, multiSampleMask_, wire, kEdgeV1V2 | kEdgeV2V0);
        }
    }

    void SoftwareRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                                               const Matrix& world, const Matrix& view, const Matrix& projection,
                                                               PrimitiveType primitive, int primitiveCount)
    {
        if (primitiveCount <= 0)
            throw std::runtime_error("SoftwareRenderer::DrawIndexedColoredPrimitives: primitiveCount must be > 0");
        if (primitive != PrimitiveType::TriangleList)
            throw std::runtime_error("SoftwareRenderer::DrawIndexedColoredPrimitives: only TriangleList is supported in v1");

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
        // REMED-GFX-079: see DrawColoredPrimitives -- the active viewport transform + clip.
        int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
        float vpMinDepth = 0.0f, vpMaxDepth = 1.0f;
        GetActiveViewportRaster(vpX, vpY, vpW, vpH, vpMinDepth, vpMaxDepth);
        const ViewportTransform vpT{static_cast<float>(vpX), static_cast<float>(vpY),
                                    static_cast<float>(vpW), static_cast<float>(vpH),
                                    vpMinDepth, vpMaxDepth};
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
                const std::uint8_t* raw = fetchVertex(static_cast<std::int64_t>(i) * 3 + k);
                cv[k] = BuildPositionColorClipVertex(raw, combined);
            }

            ClipVertex clipped[4];
            const int clippedCount = ClipTriangleNearPlane(cv, clipped);  // SOFTWARE-83
            if (clippedCount == 0)
                continue;

            RasterVertex rv[4];
            for (int k = 0; k < clippedCount; ++k)
                rv[k] = ClipVertexToRasterVertex(clipped[k], vpT);

            // REMED-GFX-082: FillMode.WireFrame outlines the visible clipped polygon. For a near-plane
            // clipped quad (clippedCount==4) the two fan triangles share the diagonal rv0-rv2, so each
            // masks that shared edge -- only the real polygon boundary rv0-rv1-rv2-rv3 is drawn.
            const bool wire = (fillMode_ == 1);
            const unsigned mask0 = (clippedCount == 4) ? (kEdgeV0V1 | kEdgeV1V2) : kEdgeAll;
            RasterizeTriangle(fb, depthState, cullMode_, depthBias_, slopeScaleDepthBias_, clip,
                              rv[0], rv[1], rv[2], colorWriteMask_, multiSampleMask_, wire, mask0,
                              clippedCount == 4 ? kEdgeV2V0 : 0u);
            if (clippedCount == 4)
                RasterizeTriangle(fb, depthState, cullMode_, depthBias_, slopeScaleDepthBias_, clip,
                                  rv[0], rv[2], rv[3], colorWriteMask_, multiSampleMask_, wire, kEdgeV1V2 | kEdgeV2V0);
        }
    }

    // Phase S5/S6 (SOFTWARE-40..43, 50): the effect-aware draw path -- stride-inferred vertex
    // layout (design decision 2), nearest-neighbor/bilinear texture sampling, diffuseColor
    // modulation, and the complete colour/alpha BlendState equation.
    // dualTexture/envMapping/skinned are supported (SOFTWARE-82; strides 32/52, see
    // BuildGenericClipVertex/RasterizeTriangleShaded) but without any per-light diffuse lighting
    // sum -- lightingEnabled/fogEnabled remain out of scope for v1 (design decision 6).
    // REMED-GFX-DECL-GUARD: the declaration-fidelity boundary. BuildGenericClipVertex reads its
    // attributes at byte offsets chosen by the stride alone (REMED-GFX-217), so a declaration
    // those offsets cannot represent is refused before any vertex is fetched. A stride outside the
    // canonical 16/20/24/32/48/52/56/60/68/76 set is left to this renderer's own established
    // out-of-table rejection, which is already loud and deterministic.
    static void RequireFaithfulDeclarationEXT(const IVertexBufferRenderer& vb, const char* route)
    {
        const auto& swVb = static_cast<const SoftwareVertexBufferRenderer&>(vb);
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            swVb.Declaration(), static_cast<int>(swVb.Stride()),
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt, "Software", route);
    }

    void SoftwareRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb, const Matrix& world,
                                                   const Matrix& view, const Matrix& projection,
                                                   PrimitiveType primitive, int primitiveCount,
                                                   const GpuDrawParams& params)
    {
        RequireFaithfulDeclarationEXT(vb, "ordinary-nonindexed");
        if (primitiveCount <= 0)
            throw std::runtime_error("SoftwareRenderer::DrawPrimitivesEx: primitiveCount must be > 0");
        if (primitive != PrimitiveType::TriangleList && primitive != PrimitiveType::LineList &&
            primitive != PrimitiveType::LineStrip && primitive != PrimitiveType::PointListEXT)
            throw std::runtime_error(
                "SoftwareRenderer::DrawPrimitivesEx: unsupported primitive topology");
        // Stock PBR and Skinned effects deliberately keep texturing enabled when their optional
        // base map is unbound. The native shader backends bind white in that case; this rasterizer's
        // existing null-texture branch is the exact CPU equivalent (factor/vertex colour unchanged).
        if (params.dualTexture && params.texture1 == nullptr)
            throw std::runtime_error("SoftwareRenderer::DrawPrimitivesEx: dualTexture=true but texture1 is null");
        if (params.envMapping && params.envMap == nullptr)
            throw std::runtime_error("SoftwareRenderer::DrawPrimitivesEx: envMapping=true but envMap is null");

        if (g_cubeTrace.enabled) { ++g_cubeTrace.drawId; g_cubeTrace.family = "DrawPrimitives"; }

        // REMED-GFX-119: this path receives the public vertexStart. It was previously dropped, so
        // every non-indexed draw rendered the element-zero prefix of the bound buffer; only the
        // consumed count was ever right.
        const int vertexStart = params.vertexStart;
        const std::int64_t consumedVertexCount =
            PrimitiveElementCount(primitive, primitiveCount);
        ValidateNonIndexedAddressing(
            SmallestAddressableVertexCount(params, vb.GetVertexCount()),
            consumedVertexCount, vertexStart);

        const auto& swVb = static_cast<const SoftwareVertexBufferRenderer&>(vb);
        // REMED-GFX-201: the layout the shader sees spans every bound per-vertex stream, so the
        // stride that selects it is their sum -- which is this one stream's own stride whenever a
        // single buffer is bound.
        const std::size_t stride = CombinedVertexStrideOr(params, swVb.Stride());
        if (stride != 16 && stride != 20 && stride != 24 && stride != 32 &&
            stride != 48 && stride != 52 && stride != 56 && stride != 60 && stride != 80 &&
            stride != 68 && stride != 76)
            throw std::runtime_error(
                "SoftwareRenderer::DrawPrimitivesEx: unsupported vertex stride "
                "(only 16/20/24/32/48/52/56/60/68/76 supported in v1)");

        const std::uint8_t* base = swVb.Data().data();

        const Matrix combined = world * view * projection;
        SoftwareFramebuffer& fb = CurrentFramebuffer();
        const RasterDepthState depthState{
            depthTestEnabled_, depthWriteEnabled_, depthCompareFunction_}; // REMED-GFX-030 draw snapshot
        // REMED-GFX-079: see DrawColoredPrimitives -- the active viewport transform + clip.
        int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
        float vpMinDepth = 0.0f, vpMaxDepth = 1.0f;
        GetActiveViewportRaster(vpX, vpY, vpW, vpH, vpMinDepth, vpMaxDepth);
        const ViewportTransform vpT{static_cast<float>(vpX), static_cast<float>(vpY),
                                    static_cast<float>(vpW), static_cast<float>(vpH),
                                    vpMinDepth, vpMaxDepth};
        // REMED-GFX-080: effective raster clip = framebuffer ∩ Viewport ∩ (ScissorRectangle when
        // RasterizerState.ScissorTestEnable). The scissor is framebuffer-space, intersected after
        // the viewport clip (not viewport-local); disabled scissor leaves the viewport clip intact.
        int scX = 0, scY = 0, scW = 0, scH = 0;
        GetActiveScissor(scX, scY, scW, scH);
        const RasterClipRect clip = ScissorClip(ViewportClip(fb, vpX, vpY, vpW, vpH),
                                                scissorTestEnable_, scX, scY, scW, scH);

        // REMED-GFX-119: element = vertexStart + local (never a byte offset); the stride multiply
        // happens only after the element range above was validated.
        // REMED-GFX-201: every bound per-vertex stream advances by the SAME element count, each
        // multiplied by its OWN stride and shifted by its OWN binding offset -- FNA3D's
        // `vertexStride * (vertexOffset + start)`, per stream.
        const auto fetchVertex = [&](std::int64_t local) -> CombinedVertexReader {
            CombinedVertexReader reader;
            reader.params = &params;
            const std::int64_t element = vertexStart + local;
            if (params.vertexStreamCount == 0)
            {
                reader.recordBase[0] = base + static_cast<std::size_t>(element) * stride;
                return reader;
            }
            for (int s = 0; s < params.vertexStreamCount; ++s)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(s)];
                const auto* streamVb =
                    static_cast<const SoftwareVertexBufferRenderer*>(stream.buffer);
                reader.recordBase[static_cast<std::size_t>(s)] =
                    streamVb->Data().data() +
                    static_cast<std::size_t>(stream.vertexOffset + element) *
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
                if (cv.w > 1e-5f)
                    RasterizePointShaded(
                        fb, depthState, RasterStencilState{}, blendState, blendFactor, params,
                        clip, ClipVertexToRasterVertex(cv, vpT), colorWriteMask_, multiSampleMask_,
                        GetSamplerState(0), GetSamplerState(1));
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
                if (ClipLineNearPlane(a, b))
                    RasterizeLineShaded(
                        fb, depthState, RasterStencilState{}, blendState, blendFactor, params, clip,
                        ClipVertexToRasterVertex(a, vpT), ClipVertexToRasterVertex(b, vpT),
                        colorWriteMask_, multiSampleMask_, GetSamplerState(0), GetSamplerState(1));
                continue;
            }

            ClipVertex cv[3];
            for (int k = 0; k < 3; ++k)
            {
                const CombinedVertexReader raw =
                    fetchVertex(static_cast<std::int64_t>(i) * 3 + k);
                cv[k] = BuildGenericClipVertex(raw, stride, combined, params);
            }

            ClipVertex clipped[4];
            const int clippedCount = ClipTriangleNearPlane(cv, clipped);  // SOFTWARE-83
            if (clippedCount == 0)
                continue;

            RasterVertex rv[4];
            for (int k = 0; k < clippedCount; ++k)
                rv[k] = ClipVertexToRasterVertex(clipped[k], vpT);

            // REMED-GFX-079: clip 3D rasterization to framebuffer ∩ active Viewport (was the full
            // framebuffer). A default full-target viewport yields the same clip byte-for-byte.
            // REMED-GFX-082: FillMode.WireFrame outlines the visible clipped polygon; the two fan
            // triangles of a near-plane clipped quad mask their shared rv0-rv2 diagonal edge.
            const bool wire = (fillMode_ == 1);
            const unsigned mask0 = (clippedCount == 4) ? (kEdgeV0V1 | kEdgeV1V2) : kEdgeAll;
            RasterizeTriangleShaded(fb, depthState, RasterStencilState{}, blendState, blendFactor,
                                    cullMode_,
                                    depthBias_, slopeScaleDepthBias_, params,
                                    clip, rv[0], rv[1], rv[2], colorWriteMask_, multiSampleMask_,
                                    GetSamplerState(0), GetSamplerState(1), wire, mask0,
                                    clippedCount == 4 ? kEdgeV2V0 : 0u);
            if (clippedCount == 4)
                RasterizeTriangleShaded(fb, depthState, RasterStencilState{}, blendState, blendFactor,
                                        cullMode_,
                                        depthBias_, slopeScaleDepthBias_, params,
                                        clip, rv[0], rv[2], rv[3], colorWriteMask_, multiSampleMask_,
                                        GetSamplerState(0), GetSamplerState(1), wire, kEdgeV1V2 | kEdgeV2V0);
        }
    }

    void SoftwareRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                                          const Matrix& world, const Matrix& view,
                                                          const Matrix& projection, PrimitiveType primitive,
                                                          int primitiveCount, const GpuDrawParams& params)
    {
        RequireFaithfulDeclarationEXT(vb, "ordinary-indexed");
        if (primitiveCount <= 0)
            throw std::runtime_error("SoftwareRenderer::DrawIndexedPrimitivesEx: primitiveCount must be > 0");
        if (primitive != PrimitiveType::TriangleList && primitive != PrimitiveType::LineList &&
            primitive != PrimitiveType::LineStrip && primitive != PrimitiveType::PointListEXT)
            throw std::runtime_error(
                "SoftwareRenderer::DrawIndexedPrimitivesEx: unsupported primitive topology");
        // See DrawPrimitivesEx: an unbound optional base map is the white fallback, not an error.
        if (params.dualTexture && params.texture1 == nullptr)
            throw std::runtime_error("SoftwareRenderer::DrawIndexedPrimitivesEx: dualTexture=true but texture1 is null");
        if (params.envMapping && params.envMap == nullptr)
            throw std::runtime_error("SoftwareRenderer::DrawIndexedPrimitivesEx: envMapping=true but envMap is null");

        const auto& swVb = static_cast<const SoftwareVertexBufferRenderer&>(vb);
        const auto& swIb = static_cast<const SoftwareIndexBufferRenderer&>(ib);
        // REMED-GFX-201: see DrawPrimitivesEx -- the selecting stride is the sum of the bound
        // per-vertex streams' strides, identical to this one stream's whenever one is bound.
        const std::size_t stride = CombinedVertexStrideOr(params, swVb.Stride());
        if (stride != 16 && stride != 20 && stride != 24 && stride != 32 &&
            stride != 48 && stride != 52 && stride != 56 && stride != 60 && stride != 80 &&
            stride != 68 && stride != 76)
            throw std::runtime_error(
                "SoftwareRenderer::DrawIndexedPrimitivesEx: unsupported vertex stride "
                "(only 16/20/24/32/48/52/56/60/68/76 supported in v1)");

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
        ValidateIndexedAddressing(ibBase, thirtyTwoBit, ib.GetIndexCount(),
                                  SmallestAddressableVertexCount(params, vb.GetVertexCount()),
                                  consumedIndexCount, startIndex, baseVertex);

        const Matrix combined = world * view * projection;
        SoftwareFramebuffer& fb = CurrentFramebuffer();
        const RasterDepthState depthState{
            depthTestEnabled_, depthWriteEnabled_, depthCompareFunction_}; // REMED-GFX-030 draw snapshot
        // REMED-GFX-079: see DrawColoredPrimitives -- the active viewport transform + clip.
        int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
        float vpMinDepth = 0.0f, vpMaxDepth = 1.0f;
        GetActiveViewportRaster(vpX, vpY, vpW, vpH, vpMinDepth, vpMaxDepth);
        const ViewportTransform vpT{static_cast<float>(vpX), static_cast<float>(vpY),
                                    static_cast<float>(vpW), static_cast<float>(vpH),
                                    vpMinDepth, vpMaxDepth};
        // REMED-GFX-080: effective raster clip = framebuffer ∩ Viewport ∩ (ScissorRectangle when
        // RasterizerState.ScissorTestEnable). The scissor is framebuffer-space, intersected after
        // the viewport clip (not viewport-local); disabled scissor leaves the viewport clip intact.
        int scX = 0, scY = 0, scW = 0, scH = 0;
        GetActiveScissor(scX, scY, scW, scH);
        const RasterClipRect clip = ScissorClip(ViewportClip(fb, vpX, vpY, vpW, vpH),
                                                scissorTestEnable_, scX, scY, scW, scH);

        // REMED-GFX-110: element = startIndex + local (never a byte offset), vertex = decoded
        // index + baseVertex (added exactly once). Every address below was validated above.
        // REMED-GFX-201: the decoded index plus baseVertex addresses EVERY bound per-vertex
        // stream, each shifted by its own binding offset and multiplied by its own stride.
        const auto fetchVertex = [&](std::int64_t local) -> CombinedVertexReader {
            const std::int64_t vertexIndex =
                static_cast<std::int64_t>(
                    DecodeIndexElement(ibBase, thirtyTwoBit, startIndex + local)) + baseVertex;
            CombinedVertexReader reader;
            reader.params = &params;
            if (params.vertexStreamCount == 0)
            {
                reader.recordBase[0] =
                    vbBase + static_cast<std::size_t>(vertexIndex) * stride;
                return reader;
            }
            for (int s2 = 0; s2 < params.vertexStreamCount; ++s2)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(s2)];
                const auto* streamVb =
                    static_cast<const SoftwareVertexBufferRenderer*>(stream.buffer);
                reader.recordBase[static_cast<std::size_t>(s2)] =
                    streamVb->Data().data() +
                    static_cast<std::size_t>(stream.vertexOffset + vertexIndex) *
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
                if (cv.w > 1e-5f)
                    RasterizePointShaded(
                        fb, depthState, RasterStencilState{}, blendState, blendFactor, params,
                        clip, ClipVertexToRasterVertex(cv, vpT), colorWriteMask_, multiSampleMask_,
                        GetSamplerState(0), GetSamplerState(1));
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
                if (ClipLineNearPlane(a, b))
                    RasterizeLineShaded(
                        fb, depthState, RasterStencilState{}, blendState, blendFactor, params, clip,
                        ClipVertexToRasterVertex(a, vpT), ClipVertexToRasterVertex(b, vpT),
                        colorWriteMask_, multiSampleMask_, GetSamplerState(0), GetSamplerState(1));
                continue;
            }

            ClipVertex cv[3];
            for (int k = 0; k < 3; ++k)
            {
                const CombinedVertexReader raw =
                    fetchVertex(static_cast<std::int64_t>(i) * 3 + k);
                cv[k] = BuildGenericClipVertex(raw, stride, combined, params);
            }

            ClipVertex clipped[4];
            const int clippedCount = ClipTriangleNearPlane(cv, clipped);  // SOFTWARE-83
            if (clippedCount == 0)
                continue;

            RasterVertex rv[4];
            for (int k = 0; k < clippedCount; ++k)
                rv[k] = ClipVertexToRasterVertex(clipped[k], vpT);

            // REMED-GFX-079: clip 3D rasterization to framebuffer ∩ active Viewport (was the full
            // framebuffer). A default full-target viewport yields the same clip byte-for-byte.
            // REMED-GFX-082: FillMode.WireFrame outlines the visible clipped polygon; the two fan
            // triangles of a near-plane clipped quad mask their shared rv0-rv2 diagonal edge.
            const bool wire = (fillMode_ == 1);
            const unsigned mask0 = (clippedCount == 4) ? (kEdgeV0V1 | kEdgeV1V2) : kEdgeAll;
            RasterizeTriangleShaded(fb, depthState, RasterStencilState{}, blendState, blendFactor,
                                    cullMode_,
                                    depthBias_, slopeScaleDepthBias_, params,
                                    clip, rv[0], rv[1], rv[2], colorWriteMask_, multiSampleMask_,
                                    GetSamplerState(0), GetSamplerState(1), wire, mask0,
                                    clippedCount == 4 ? kEdgeV2V0 : 0u);
            if (clippedCount == 4)
                RasterizeTriangleShaded(fb, depthState, RasterStencilState{}, blendState, blendFactor,
                                        cullMode_,
                                        depthBias_, slopeScaleDepthBias_, params,
                                        clip, rv[0], rv[2], rv[3], colorWriteMask_, multiSampleMask_,
                                        GetSamplerState(0), GetSamplerState(1), wire, kEdgeV1V2 | kEdgeV2V0);
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
#endif
}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_SOFTWARE
    // plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace Software { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> Software::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Software::SoftwareRenderer>(args.virtualWidth, args.virtualHeight);
    }
#endif
}
