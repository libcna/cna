#include "CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp"

#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Backends::Software
{
    namespace
    {
        using Vector3 = Microsoft::Xna::Framework::Vector3;
        using Vector4 = Microsoft::Xna::Framework::Vector4;

        // ---- Phase S4 rasterizer core ----
        //
        // Works entirely in CNA's own native row-major/row-vector Matrix convention (matching
        // IGraphicsBackend.hpp's own documented "combined = world * view * projection" order) --
        // no GPU/shader column-major conversion is needed since this backend never talks to a
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

        float EdgeFunction(float ax, float ay, float bx, float by, float px, float py)
        {
            return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
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

        /// Bilinear texture sample (SOFTWARE-80), clamp-to-edge at the boundaries -- matches this
        /// backend's own existing "texture address modes not honored, UVs are simply clamped"
        /// simplification (docs/software-backend.md) rather than adding real Wrap/Mirror support.
        /// At the very edge of the texture, clamping collapses both interpolation endpoints to the
        /// same texel, so this degrades cleanly to the old nearest-neighbor result right at the
        /// boundary rather than blending with a wrapped-around texel.
        void SampleBilinear(const SoftwareTextureBackend& texture, float u, float v,
                           float& r, float& g, float& b, float& a)
        {
            const int texW = std::max(1, texture.GetWidth());
            const int texH = std::max(1, texture.GetHeight());
            const auto& pixels = texture.Pixels();

            const float tx = u * static_cast<float>(texW) - 0.5f;
            const float ty = v * static_cast<float>(texH) - 0.5f;
            const int x0raw = static_cast<int>(std::floor(tx));
            const int y0raw = static_cast<int>(std::floor(ty));
            // Clamp x0/x1 (and y0/y1) independently from the RAW (pre-clamp) indices -- clamping
            // x0 first and then computing x1 = clamp(x0+1, ...) from the already-clamped x0 would
            // shift x1 to the wrong texel just outside the valid range (a real bug caught by
            // Software_Effects' own corner-sampling check: it produced a visible blend with the
            // neighboring texel right at the texture edge instead of correctly collapsing to a
            // single texel there).
            const int x0 = std::clamp(x0raw, 0, texW - 1);
            const int y0 = std::clamp(y0raw, 0, texH - 1);
            const int x1 = std::clamp(x0raw + 1, 0, texW - 1);
            const int y1 = std::clamp(y0raw + 1, 0, texH - 1);
            const float fx = std::clamp(tx - std::floor(tx), 0.0f, 1.0f);
            const float fy = std::clamp(ty - std::floor(ty), 0.0f, 1.0f);

            const auto sample = [&](int px, int py, int channel) -> float {
                const std::size_t idx = (static_cast<std::size_t>(py) * static_cast<std::size_t>(texW) +
                                        static_cast<std::size_t>(px)) * 4u + static_cast<std::size_t>(channel);
                return pixels[idx] / 255.0f;
            };

            const auto bilerp = [&](int channel) -> float {
                const float top = sample(x0, y0, channel) * (1.0f - fx) + sample(x1, y0, channel) * fx;
                const float bottom = sample(x0, y1, channel) * (1.0f - fx) + sample(x1, y1, channel) * fx;
                return top * (1.0f - fy) + bottom * fy;
            };

            r = bilerp(0);
            g = bilerp(1);
            b = bilerp(2);
            a = bilerp(3);
        }

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

        /// SOFTWARE-82: standard cube-map face selection (largest-magnitude axis picks the face,
        /// its sign picks Positive/Negative) and per-face UV projection, matching the classic
        /// OpenGL/D3D convention. Nearest-neighbor only (no cross-face bilinear filtering at cube
        /// seams -- a real, deliberate simplification, consistent with this backend's existing
        /// "correctness over performance/fidelity, simple wins over exhaustive" stance).
        void SampleCubeMap(const SoftwareTextureCubeBackend& cube, const Vector3& dir,
                          float& r, float& g, float& b, float& a)
        {
            const float ax = std::abs(dir.X), ay = std::abs(dir.Y), az = std::abs(dir.Z);
            int face; float u, v, ma;
            if (ax >= ay && ax >= az)
            {
                face = dir.X > 0.0f ? 0 : 1;               // PositiveX : NegativeX
                u = dir.X > 0.0f ? -dir.Z : dir.Z;
                v = -dir.Y;
                ma = ax;
            }
            else if (ay >= ax && ay >= az)
            {
                face = dir.Y > 0.0f ? 2 : 3;               // PositiveY : NegativeY
                u = dir.X;
                v = dir.Y > 0.0f ? dir.Z : -dir.Z;
                ma = ay;
            }
            else
            {
                face = dir.Z > 0.0f ? 4 : 5;               // PositiveZ : NegativeZ
                u = dir.Z > 0.0f ? dir.X : -dir.X;
                v = -dir.Y;
                ma = az;
            }
            const float s = std::clamp((u / ma + 1.0f) * 0.5f, 0.0f, 1.0f);
            const float t = std::clamp((v / ma + 1.0f) * 0.5f, 0.0f, 1.0f);

            const int size = std::max(1, cube.GetSize());
            const int px = std::clamp(static_cast<int>(s * static_cast<float>(size)), 0, size - 1);
            const int py = std::clamp(static_cast<int>(t * static_cast<float>(size)), 0, size - 1);
            const auto& pixels = cube.FacePixels(face);
            const std::size_t idx = (static_cast<std::size_t>(py) * static_cast<std::size_t>(size) +
                                    static_cast<std::size_t>(px)) * 4u;
            r = pixels[idx + 0] / 255.0f;
            g = pixels[idx + 1] / 255.0f;
            b = pixels[idx + 2] / 255.0f;
            a = pixels[idx + 3] / 255.0f;
        }

        /// SOFTWARE-81: whether a triangle with the given signed screen-space `area` should be
        /// culled under the given raw CullMode ordinal (0=None, 1=CullClockwiseFace,
        /// 2=CullCounterClockwiseFace). In this backend's screen-space convention (Y grows
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
        inline void WriteColoredFragment(SoftwareFramebuffer& fb, bool depthTestEnabled,
                                         const RasterClipRect& clip, int x, int y,
                                         float depth, float invW, float pr, float pg, float pb, float pa)
        {
            if (x < clip.minX || x > clip.maxX || y < clip.minY || y > clip.maxY)
                return;
            const std::size_t pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.width) +
                                           static_cast<std::size_t>(x);
            if (depthTestEnabled && depth > fb.depthBuffer[pixelIndex])
                return;
            const float r = pr / invW, g = pg / invW, b = pb / invW, a = pa / invW;
            fb.depthBuffer[pixelIndex] = depth;
            const std::size_t colorIndex = pixelIndex * 4;
            fb.color[colorIndex + 0] = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
            fb.color[colorIndex + 1] = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
            fb.color[colorIndex + 2] = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
            fb.color[colorIndex + 3] = static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
        }

        /// Fills one triangle into `fb` using a standard edge-function/barycentric rasterizer,
        /// with a per-pixel depth test against `fb.depthBuffer` when `depthTestEnabled` and
        /// backface culling per `cullMode` (SOFTWARE-81; raw ordinal, see ShouldCullTriangle()).
        /// REMED-GFX-082: when `wireframe`, only the edges selected by `edgeMask` are rasterized
        /// (line walk) instead of the interior fill -- culling and the zero-area reject are shared, so
        /// a culled/degenerate triangle emits no wire either.
        void RasterizeTriangle(SoftwareFramebuffer& fb, bool depthTestEnabled, int cullMode,
                               const RasterClipRect& clip,
                               const RasterVertex& v0, const RasterVertex& v1, const RasterVertex& v2,
                               bool wireframe = false, unsigned edgeMask = kEdgeAll)
        {
            const float area = EdgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
            if (area == 0.0f)
                return;  // degenerate (zero-area) triangle
            if (ShouldCullTriangle(area, cullMode))
                return;

            if (wireframe)
            {
                // REMED-GFX-082: rasterize the selected edges as perspective-correct lines, reusing
                // WriteColoredFragment (the same depth-test/write + clip path as the fill below).
                const auto drawEdge = [&](const RasterVertex& A, const RasterVertex& B) {
                    WalkWireEdge(clip, A, B, [&](int x, int y, float t) {
                        const float invW  = A.invW  + t * (B.invW  - A.invW);
                        const float depth = A.depth + t * (B.depth - A.depth);
                        WriteColoredFragment(fb, depthTestEnabled, clip, x, y, depth, invW,
                                             A.r + t * (B.r - A.r), A.g + t * (B.g - A.g),
                                             A.b + t * (B.b - A.b), A.a + t * (B.a - A.a));
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
            const int minX = std::max(clip.minX, static_cast<int>(std::floor(minXf)));
            const int maxX = std::min(clip.maxX, static_cast<int>(std::ceil(maxXf)));
            const int minY = std::max(clip.minY, static_cast<int>(std::floor(minYf)));
            const int maxY = std::min(clip.maxY, static_cast<int>(std::ceil(maxYf)));

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

                    const float lambda0 = w0 / area;
                    const float lambda1 = w1 / area;
                    const float lambda2 = w2 / area;

                    // Post-divide depth interpolates linearly in screen space -- no perspective
                    // correction needed for this one attribute (a well-known rasterization
                    // property), unlike color/UV below.
                    const float depth = lambda0 * v0.depth + lambda1 * v1.depth + lambda2 * v2.depth;
                    const float invW = lambda0 * v0.invW + lambda1 * v1.invW + lambda2 * v2.invW;
                    WriteColoredFragment(fb, depthTestEnabled, clip, x, y, depth, invW,
                                         lambda0 * v0.r + lambda1 * v1.r + lambda2 * v2.r,
                                         lambda0 * v0.g + lambda1 * v1.g + lambda2 * v2.g,
                                         lambda0 * v0.b + lambda1 * v1.b + lambda2 * v2.b,
                                         lambda0 * v0.a + lambda1 * v1.a + lambda2 * v2.a);
                }
            }
        }

        // ---- Phase S5/S6: generalized (textured/blended/effect-driven) rasterization ----

        /// Transforms a vertex whose byte layout is inferred from `stride` (plan_software.md
        /// design decision 2: 16=VertexPositionColor, 20=VertexPositionTexture,
        /// 24=VertexPositionColorTexture, 32=VertexPositionNormalTexture (SOFTWARE-82,
        /// EnvironmentMapEffect), 52=VertexPositionNormalTextureSkinned (SOFTWARE-82,
        /// SkinnedEffect)) into clip space, for the DrawPrimitivesEx/DrawIndexedPrimitivesEx path.
        /// `params.vertexColorEnabled` mirrors GpuDrawParams' own flag -- when false (or for a
        /// stride with no Color field at all), vertex color is treated as opaque white so it
        /// doesn't affect the eventual texture/diffuse modulation, matching a real Effect's own
        /// VertexColorEnabled=false behavior. Attributes are left un-premultiplied; near-plane
        /// clipping (SOFTWARE-83) happens on ClipVertex, before the perspective divide.
        ClipVertex BuildGenericClipVertex(const std::uint8_t* raw, std::size_t stride, const Matrix& combined,
                                          const GpuDrawParams& params)
        {
            Vector3 position;
            std::memcpy(&position, raw, sizeof(Vector3));
            Vector3 normal(0.0f, 0.0f, 1.0f);
            bool haveNormal = false;

            if (stride == 52 && params.skinned)
            {
                // VertexPositionNormalTextureSkinned: Position@0, Normal@12, TextureCoordinate@24,
                // BlendWeight@32 (4 floats), BlendIndices@48 (4 bytes). Blend up to
                // weightsPerVertex bone matrices (column-major, GpuDrawParams::boneTransforms'
                // own layout -- Task 895's "only sum the first N pairs" behavior) and apply the
                // blended matrix to Position/Normal BEFORE the standard World*View*Projection
                // transform below, mirroring FNA's own Skin(vin, boneCount) step.
                Vector4 blendWeight;
                std::memcpy(&blendWeight, raw + 32, sizeof(Vector4));
                std::uint8_t blendIndices[4];
                std::memcpy(blendIndices, raw + 48, 4);
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
                std::memcpy(&normal, raw + 12, sizeof(Vector3));
                normal = ApplyAffineColumnMajor(blended, normal, 0.0f);
                haveNormal = true;
            }

            const Vector4 clip = Vector4::Transform(position, combined);

            ClipVertex out;
            out.x = clip.X; out.y = clip.Y; out.z = clip.Z; out.w = clip.W;

            if (stride == 16)
            {
                UnpackColorBytes(raw + 12, out.r, out.g, out.b, out.a);
            }
            else if (stride == 20)
            {
                std::memcpy(&out.u, raw + 12, sizeof(float));
                std::memcpy(&out.v, raw + 16, sizeof(float));
            }
            else if (stride == 24)
            {
                UnpackColorBytes(raw + 12, out.r, out.g, out.b, out.a);
                std::memcpy(&out.u, raw + 16, sizeof(float));
                std::memcpy(&out.v, raw + 20, sizeof(float));
            }
            else if (stride == 32)
            {
                // VertexPositionNormalTexture: Position@0, Normal@12, TextureCoordinate@24.
                std::memcpy(&normal, raw + 12, sizeof(Vector3));
                haveNormal = true;
                std::memcpy(&out.u, raw + 24, sizeof(float));
                std::memcpy(&out.v, raw + 28, sizeof(float));
            }
            else if (stride == 52)
            {
                std::memcpy(&out.u, raw + 24, sizeof(float));
                std::memcpy(&out.v, raw + 28, sizeof(float));
            }

            if (haveNormal && params.envMapping)
            {
                // World-space position/normal for the reflection vector (SOFTWARE-82). Uses
                // World directly rather than the mathematically-correct WorldInverseTranspose for
                // the normal -- an intentional simplification, exact for uniform-scale/no-shear
                // World matrices and only distorting the reflection for non-uniform scale, which
                // this backend's own existing "correctness over full fidelity" stance accepts.
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
            const SoftwareTextureBackend* texture0;
            const SoftwareTextureBackend* texture1;
            const SoftwareTextureCubeBackend* envMap;
            bool useDualTexture;
            bool useEnvMap;
            bool needUV;
            bool blendEnabled;
            bool depthTestEnabled;
        };

        /// REMED-GFX-082: writes one already-interpolated shaded fragment -- the whole texture/diffuse/
        /// dual-texture/env-map/blend pipeline that used to live inline in RasterizeTriangleShaded's
        /// fill loop. `p*` are the perspective-premultiplied attribute sums (attr * invW), divided by
        /// invW here exactly as before, so the Solid fill stays byte-identical and the WireFrame line
        /// walk gets the identical shading along its edges.
        inline void WriteShadedFragment(SoftwareFramebuffer& fb, const ShadedContext& ctx,
                                        const RasterClipRect& clip, int x, int y, float depth, float invW,
                                        float pr, float pg, float pb, float pa, float pu, float pv,
                                        float pwpx, float pwpy, float pwpz, float pnx, float pny, float pnz)
        {
            if (x < clip.minX || x > clip.maxX || y < clip.minY || y > clip.maxY)
                return;
            const std::size_t pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.width) +
                                           static_cast<std::size_t>(x);
            if (ctx.depthTestEnabled && depth > fb.depthBuffer[pixelIndex])
                return;

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
                // (FNA's PSDualTexture) -- both textures reuse the SAME uv (this backend has no
                // genuine 2-UV vertex format; established precedent already set by this codebase's own
                // Vulkan dual_texture3d shaders).
                float t0r, t0g, t0b, t0a;
                SampleBilinear(*ctx.texture0, u, v, t0r, t0g, t0b, t0a);
                float t1r, t1g, t1b, t1a;
                SampleBilinear(*ctx.texture1, u, v, t1r, t1g, t1b, t1a);
                r *= (t0r * 2.0f) * t1r;
                g *= (t0g * 2.0f) * t1g;
                b *= (t0b * 2.0f) * t1b;
                a *= t0a * t1a;
            }
            else if (ctx.params.textureEnabled && ctx.texture0 != nullptr)
            {
                float texR, texG, texB, texA;
                SampleBilinear(*ctx.texture0, u, v, texR, texG, texB, texA);
                r *= texR;
                g *= texG;
                b *= texB;
                a *= texA;
            }

            r *= ctx.params.diffuseColor[0];
            g *= ctx.params.diffuseColor[1];
            b *= ctx.params.diffuseColor[2];
            a *= ctx.params.diffuseColor[3];

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
                SampleCubeMap(*ctx.envMap, reflDir, envR, envG, envB, envA);

                const float viewAngle = nDotE;
                const float blendFactor = ctx.params.fresnelEnabled
                    ? std::pow(std::max(1.0f - std::abs(viewAngle), 0.0f), ctx.params.fresnelFactor) * ctx.params.envMapAmount
                    : ctx.params.envMapAmount;

                r = r * (1.0f - blendFactor) + (envR * a) * blendFactor + ctx.params.envMapSpecular[0] * envA * a;
                g = g * (1.0f - blendFactor) + (envG * a) * blendFactor + ctx.params.envMapSpecular[1] * envA * a;
                b = b * (1.0f - blendFactor) + (envB * a) * blendFactor + ctx.params.envMapSpecular[2] * envA * a;
            }

            fb.depthBuffer[pixelIndex] = depth;

            const std::size_t colorIndex = pixelIndex * 4;
            if (!ctx.blendEnabled)
            {
                fb.color[colorIndex + 0] = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
                fb.color[colorIndex + 1] = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
                fb.color[colorIndex + 2] = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
                fb.color[colorIndex + 3] = static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
            }
            else
            {
                // Simplified "over" alpha compositing (design decision 7): result =
                // src*srcAlpha + dst*(1-srcAlpha) on all 4 channels -- one formula covering
                // AlphaBlend/NonPremultiplied/Additive-ish real BlendState presets alike, an
                // intentional v1 simplification rather than a full blend-equation interpreter.
                const float dstR = fb.color[colorIndex + 0] / 255.0f;
                const float dstG = fb.color[colorIndex + 1] / 255.0f;
                const float dstB = fb.color[colorIndex + 2] / 255.0f;
                const float dstA = fb.color[colorIndex + 3] / 255.0f;
                const float invA = 1.0f - a;
                fb.color[colorIndex + 0] = static_cast<std::uint8_t>(std::clamp(r * a + dstR * invA, 0.0f, 1.0f) * 255.0f);
                fb.color[colorIndex + 1] = static_cast<std::uint8_t>(std::clamp(g * a + dstG * invA, 0.0f, 1.0f) * 255.0f);
                fb.color[colorIndex + 2] = static_cast<std::uint8_t>(std::clamp(b * a + dstB * invA, 0.0f, 1.0f) * 255.0f);
                fb.color[colorIndex + 3] = static_cast<std::uint8_t>(std::clamp(a + dstA * invA, 0.0f, 1.0f) * 255.0f);
            }
        }

        /// General-purpose triangle fill for the DrawPrimitivesEx/DrawIndexedPrimitivesEx and
        /// SpriteBatch paths: adds nearest-neighbor texture sampling, diffuseColor modulation, and
        /// a simplified Opaque/AlphaBlend choice (design decisions 7/6) on top of RasterizeTriangle's
        /// depth-tested, perspective-correct color interpolation. Backface culling per `cullMode`
        /// (SOFTWARE-81; raw ordinal, see ShouldCullTriangle()). `params.dualTexture`/`envMapping`
        /// (SOFTWARE-82) select DualTextureEffect's second-texture blend or EnvironmentMapEffect's
        /// cube-map reflection on top of the same base texture/diffuse/vertex-color path -- no
        /// per-light diffuse lighting is computed for either (design decision 6: no lighting
        /// engine in v1), so the "lit" base color is just vertexColor*diffuseColor*texture0, the
        /// same simplification already used for the plain BasicEffect path.
        void RasterizeTriangleShaded(SoftwareFramebuffer& fb, bool depthTestEnabled, bool blendEnabled,
                                     int cullMode, const GpuDrawParams& params, const RasterClipRect& clip,
                                     const RasterVertex& v0, const RasterVertex& v1, const RasterVertex& v2,
                                     bool wireframe = false, unsigned edgeMask = kEdgeAll)
        {
            const auto* texture0 = dynamic_cast<const SoftwareTextureBackend*>(params.texture0);
            const auto* texture1 = dynamic_cast<const SoftwareTextureBackend*>(params.texture1);
            const auto* envMap = dynamic_cast<const SoftwareTextureCubeBackend*>(params.envMap);
            const bool useDualTexture = params.dualTexture && texture0 != nullptr && texture1 != nullptr;
            const bool useEnvMap = params.envMapping && envMap != nullptr;
            const bool needUV = useDualTexture || useEnvMap || (params.textureEnabled && texture0 != nullptr);
            const ShadedContext ctx{params, texture0, texture1, envMap, useDualTexture, useEnvMap,
                                    needUV, blendEnabled, depthTestEnabled};

            const float area = EdgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
            if (area == 0.0f)
                return;
            if (ShouldCullTriangle(area, cullMode))
                return;

            if (wireframe)
            {
                // REMED-GFX-082: rasterize the selected edges as perspective-correct shaded lines,
                // reusing WriteShadedFragment (identical texture/diffuse/env-map/blend + depth path).
                const auto drawEdge = [&](const RasterVertex& A, const RasterVertex& B) {
                    WalkWireEdge(clip, A, B, [&](int x, int y, float t) {
                        const float invW  = A.invW  + t * (B.invW  - A.invW);
                        const float depth = A.depth + t * (B.depth - A.depth);
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
            const int minX = std::max(clip.minX, static_cast<int>(std::floor(minXf)));
            const int maxX = std::min(clip.maxX, static_cast<int>(std::ceil(maxXf)));
            const int minY = std::max(clip.minY, static_cast<int>(std::floor(minYf)));
            const int maxY = std::min(clip.maxY, static_cast<int>(std::ceil(maxYf)));

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

                    const float lambda0 = w0 / area;
                    const float lambda1 = w1 / area;
                    const float lambda2 = w2 / area;

                    const float depth = lambda0 * v0.depth + lambda1 * v1.depth + lambda2 * v2.depth;
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
                                        lambda0 * v0.nz + lambda1 * v1.nz + lambda2 * v2.nz);
                }
            }
        }
    }

    // ---- SoftwareFramebuffer ----

    void SoftwareFramebuffer::Resize(int w, int h)
    {
        width = w;
        height = h;
        color.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 0u);
        depthBuffer.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 1.0f);
    }

    void SoftwareFramebuffer::ClearColor(float r, float g, float b, float a)
    {
        const std::uint8_t rb = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
        const std::uint8_t gb = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
        const std::uint8_t bb = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
        const std::uint8_t ab = static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
        const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        for (std::size_t i = 0; i < pixelCount; ++i)
        {
            color[i * 4 + 0] = rb;
            color[i * 4 + 1] = gb;
            color[i * 4 + 2] = bb;
            color[i * 4 + 3] = ab;
        }
    }

    void SoftwareFramebuffer::ClearDepthValue(float depthValue)
    {
        std::fill(depthBuffer.begin(), depthBuffer.end(), depthValue);
    }

    // ---- SoftwareVertexBufferBackend ----

    SoftwareVertexBufferBackend::SoftwareVertexBufferBackend(int vertexCapacity)
        : capacity_(vertexCapacity)
    {
    }

    void SoftwareVertexBufferBackend::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        if (vertex_count < 0 || vertex_count > capacity_)
            throw std::runtime_error("SoftwareVertexBufferBackend::SetData: vertex_count exceeds capacity");
        if (stride_in_bytes == 0)
            throw std::runtime_error("SoftwareVertexBufferBackend::SetData: stride_in_bytes must be > 0");

        vertexCount_ = vertex_count;
        stride_ = stride_in_bytes;
        const std::size_t byteCount = static_cast<std::size_t>(vertex_count) * stride_in_bytes;
        data_.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + byteCount);
    }

    void SoftwareVertexBufferBackend::SetDataWithOptions(const void* data, int vertex_count,
                                                         std::size_t stride_in_bytes, SetDataOptions)
    {
        SetData(data, vertex_count, stride_in_bytes);
    }

    // ---- SoftwareIndexBufferBackend ----

    SoftwareIndexBufferBackend::SoftwareIndexBufferBackend(int indexCapacity, bool thirtyTwoBit)
        : capacity_(indexCapacity), thirtyTwoBit_(thirtyTwoBit)
    {
    }

    void SoftwareIndexBufferBackend::Upload(const void* data, int index_count, bool dataIsThirtyTwoBit)
    {
        if (index_count < 0 || index_count > capacity_)
            throw std::runtime_error("SoftwareIndexBufferBackend: index_count exceeds capacity");
        if (dataIsThirtyTwoBit != thirtyTwoBit_)
            throw std::runtime_error("SoftwareIndexBufferBackend: SetData bit-width does not match the buffer's declared width");

        indexCount_ = index_count;
        const std::size_t elementSize = dataIsThirtyTwoBit ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const std::size_t byteCount = static_cast<std::size_t>(index_count) * elementSize;
        data_.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + byteCount);
    }

    void SoftwareIndexBufferBackend::SetData16(const void* data, int index_count) { Upload(data, index_count, false); }
    void SoftwareIndexBufferBackend::SetData32(const void* data, int index_count) { Upload(data, index_count, true); }
    void SoftwareIndexBufferBackend::SetData16WithOptions(const void* data, int index_count, SetDataOptions)
    { Upload(data, index_count, false); }
    void SoftwareIndexBufferBackend::SetData32WithOptions(const void* data, int index_count, SetDataOptions)
    { Upload(data, index_count, true); }

    // ---- SoftwareTextureBackend ----

    SoftwareTextureBackend::SoftwareTextureBackend(const ImageData& data)
        : width_(data.width), height_(data.height)
    {
        pixels_.assign(data.pixels.begin(), data.pixels.end());
    }

    SoftwareTextureBackend::SoftwareTextureBackend(int width, int height)
        : width_(width), height_(height)
    {
        pixels_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0u);
    }

    void SoftwareTextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (rgba == nullptr)
            throw std::runtime_error("SoftwareTextureBackend::UpdatePixels: rgba must not be null");
        const std::size_t rowBytes = static_cast<std::size_t>(width_) * 4u;
        const std::size_t effectiveStride = stride > 0 ? static_cast<std::size_t>(stride) : rowBytes;
        pixels_.resize(rowBytes * static_cast<std::size_t>(height_));
        for (int y = 0; y < height_; ++y)
        {
            std::copy(rgba + static_cast<std::size_t>(y) * effectiveStride,
                     rgba + static_cast<std::size_t>(y) * effectiveStride + rowBytes,
                     pixels_.begin() + static_cast<std::ptrdiff_t>(y) * static_cast<std::ptrdiff_t>(rowBytes));
        }
    }

    void SoftwareTextureBackend::UpdatePixelsLevel(int, const uint8_t*, int, int)
    {
        // Mip levels beyond level 0 aren't stored in v1 (no mipmapping support, plan_software.md
        // Boundaries) -- accepted as a no-op rather than throwing, matching HEADLESS-12's own
        // precedent for the same real scope trim.
    }

    // ---- SoftwareTextureCubeBackend (SOFTWARE-82) ----

    SoftwareTextureCubeBackend::SoftwareTextureCubeBackend(int size) : size_(size)
    {
        const std::size_t faceBytes = static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u;
        for (auto& face : faces_)
            face.assign(faceBytes, 0u);
    }

    void SoftwareTextureCubeBackend::SetData(int face, int level, int x, int y, int w, int h,
                                             const void* data, int)
    {
        if (level != 0 || data == nullptr || face < 0 || face > 5)
            return;  // mirrors SoftwareTextureBackend::UpdatePixelsLevel's no-op-beyond-level-0 precedent
        const auto* src = static_cast<const std::uint8_t*>(data);
        std::vector<std::uint8_t>& pixels = faces_[static_cast<std::size_t>(face)];
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        for (int row = 0; row < h; ++row)
        {
            const std::size_t dstOffset = (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(size_) +
                                          static_cast<std::size_t>(x)) * 4u;
            std::copy(src + static_cast<std::size_t>(row) * rowBytes,
                     src + static_cast<std::size_t>(row) * rowBytes + rowBytes,
                     pixels.begin() + static_cast<std::ptrdiff_t>(dstOffset));
        }
    }

    void SoftwareTextureCubeBackend::GetData(int face, int level, int x, int y, int w, int h,
                                             void* data, int) const
    {
        if (level != 0 || data == nullptr || face < 0 || face > 5)
            return;
        auto* dst = static_cast<std::uint8_t*>(data);
        const std::vector<std::uint8_t>& pixels = faces_[static_cast<std::size_t>(face)];
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        for (int row = 0; row < h; ++row)
        {
            const std::size_t srcOffset = (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(size_) +
                                          static_cast<std::size_t>(x)) * 4u;
            std::copy(pixels.begin() + static_cast<std::ptrdiff_t>(srcOffset),
                     pixels.begin() + static_cast<std::ptrdiff_t>(srcOffset) + static_cast<std::ptrdiff_t>(rowBytes),
                     dst + static_cast<std::size_t>(row) * rowBytes);
        }
    }

    // ---- SoftwareRenderTargetBackend ----

    SoftwareRenderTargetBackend::SoftwareRenderTargetBackend(int w, int h, int depthFormat, bool mipMap,
                                                             int multiSampleCount)
        : depthFormat_(depthFormat), mipMap_(mipMap), multiSampleCount_(multiSampleCount)
    {
        framebuffer_.Resize(w, h);
    }

    void SoftwareRenderTargetBackend::UpdatePixels(const uint8_t* rgba, int)
    {
        if (rgba == nullptr) return;
        const std::size_t byteCount = static_cast<std::size_t>(framebuffer_.width) *
                                       static_cast<std::size_t>(framebuffer_.height) * 4u;
        framebuffer_.color.assign(rgba, rgba + byteCount);
    }

    // ---- SoftwareEffectBackend ----

    bool SoftwareEffectBackend::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        if (vertSrc.empty() && fragSrc.empty())
            throw std::runtime_error("SoftwareEffectBackend::CompileProgram: both vertSrc and fragSrc are empty");
        compiled_ = true;
        return true;
    }

    // ---- SoftwareSpriteBatchBackend ----
    // Phase S6 (SOFTWARE-51): a SpriteBatch::Draw() call is just a textured quad (2 triangles)
    // placed directly in screen-pixel space (SpriteBatch never goes through World*View*Projection,
    // unlike 3D draws) -- reuses RasterizeTriangleShaded, the same rasterizer core DrawPrimitivesEx
    // uses. The quad-corner construction (destinationRectangle/sourceRectangle/origin/rotation/
    // SpriteEffects) mirrors EasyGLGraphicsBackend::EasyGLSpriteBatchBackend::Draw()'s own proven
    // formula, adapted to feed this backend's rasterizer directly instead of a GPU vertex buffer.

    SoftwareSpriteBatchBackend::SoftwareSpriteBatchBackend(SoftwareGraphicsBackend& owner) : owner_(owner) {}

    void SoftwareSpriteBatchBackend::Begin()
    {
        if (begun_)
            throw std::runtime_error("SoftwareSpriteBatchBackend::Begin: Begin() called without a matching End()");
        begun_ = true;
    }

    void SoftwareSpriteBatchBackend::End()
    {
        if (!begun_)
            throw std::runtime_error("SoftwareSpriteBatchBackend::End: End() called without a matching Begin()");
        begun_ = false;
    }

    void SoftwareSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
             Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()), Color(255, 255, 255, 255),
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
    }

    void SoftwareSpriteBatchBackend::Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
             SpriteEffects::None, 0.0f);
    }

    void SoftwareSpriteBatchBackend::Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color, float rotation,
                                          const Vector2& origin, SpriteEffects effects, float layerDepth)
    {
        if (!begun_)
            throw std::runtime_error("SoftwareSpriteBatchBackend::Draw: Draw() called before Begin()");

        const auto* swTexture = dynamic_cast<const SoftwareTextureBackend*>(&texture);

        const float texW = static_cast<float>(std::max(1, texture.GetWidth()));
        const float texH = static_cast<float>(std::max(1, texture.GetHeight()));
        float u1 = static_cast<float>(sourceRectangle.X) / texW;
        float v1 = static_cast<float>(sourceRectangle.Y) / texH;
        float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) / texW;
        float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / texH;
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0) std::swap(u1, u2);
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0) std::swap(v1, v2);

        const float r = color.getRProperty() / 255.0f;
        const float g = color.getGProperty() / 255.0f;
        const float b = color.getBProperty() / 255.0f;
        const float a = color.getAProperty() / 255.0f;

        const float dx = static_cast<float>(destinationRectangle.X);
        const float dy = static_cast<float>(destinationRectangle.Y);
        const float dw = static_cast<float>(destinationRectangle.Width);
        const float dh = static_cast<float>(destinationRectangle.Height);
        const float sw = static_cast<float>(std::max(1, sourceRectangle.Width));
        const float sh = static_cast<float>(std::max(1, sourceRectangle.Height));
        const float ox = origin.X;
        const float oy = origin.Y;
        const float scaleX = dw / sw;
        const float scaleY = dh / sh;

        const float p0x = (0.0f - ox) * scaleX, p0y = (0.0f - oy) * scaleY;
        const float p1x = (sw - ox) * scaleX, p1y = (0.0f - oy) * scaleY;
        const float p2x = (sw - ox) * scaleX, p2y = (sh - oy) * scaleY;
        const float p3x = (0.0f - ox) * scaleX, p3y = (sh - oy) * scaleY;

        const float cosR = std::cos(rotation);
        const float sinR = std::sin(rotation);

        SoftwareFramebuffer& fb = owner_.CurrentFramebuffer();

        // REMED-GFX-073: SpriteBatch coordinates are VIEWPORT-LOCAL. FNA builds the sprite ortho
        // from Viewport.Width/Height (sprite (0,0) = the viewport's top-left), and the rasterizer
        // viewport then positions the [-1,1] result at Viewport.X/Y. The Software backend places
        // quads directly in pixel space, so the equivalent is: build the viewport-local corner
        // (destinationRectangle/origin/rotation/scale + the SpriteBatch transformMatrix, all in
        // viewport-local space), then add the Viewport origin. Viewport.X/Y are NOT transformed by
        // transformMatrix (they position the already-transformed result), and pixels outside the
        // viewport are clipped by RasterizeTriangleShaded via `clip` below.
        int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
        owner_.GetActiveViewport(vpX, vpY, vpW, vpH);

        const auto placeCorner = [&](float px, float py) -> Vector2 {
            const float rx = dx + px * cosR - py * sinR;
            const float ry = dy + px * sinR + py * cosR;
            // SpriteBatch::SetTransformMatrix()'s optional 2D transform, applied as a point
            // transform (z=0) on the viewport-local corner...
            const Vector3 transformed = Vector3::Transform(Vector3(rx, ry, 0.0f), transformMatrix_);
            // ...then position at the viewport origin (added AFTER transformMatrix).
            return Vector2(transformed.X + static_cast<float>(vpX),
                           transformed.Y + static_cast<float>(vpY));
        };

        const Vector2 c0 = placeCorner(p0x, p0y);
        const Vector2 c1 = placeCorner(p1x, p1y);
        const Vector2 c2 = placeCorner(p2x, p2y);
        const Vector2 c3 = placeCorner(p3x, p3y);

        const RasterVertex rv0 = MakeScreenSpaceVertex(c0.X, c0.Y, layerDepth, r, g, b, a, u1, v1);
        const RasterVertex rv1 = MakeScreenSpaceVertex(c1.X, c1.Y, layerDepth, r, g, b, a, u2, v1);
        const RasterVertex rv2 = MakeScreenSpaceVertex(c2.X, c2.Y, layerDepth, r, g, b, a, u2, v2);
        const RasterVertex rv3 = MakeScreenSpaceVertex(c3.X, c3.Y, layerDepth, r, g, b, a, u1, v2);

        const bool depthTestEnabled = owner_.IsDepthTestEnabled();
        const bool blendEnabled = owner_.IsBlendEnabled();
        const int cullMode = owner_.GetCullMode();
        // REMED-GFX-080: effective raster clip = framebuffer ∩ Viewport ∩ (ScissorRectangle when
        // RasterizerState.ScissorTestEnable). The scissor is framebuffer-space, intersected after
        // the viewport clip (not viewport-local); disabled scissor leaves the viewport clip intact.
        int scX = 0, scY = 0, scW = 0, scH = 0;
        owner_.GetActiveScissor(scX, scY, scW, scH);
        const RasterClipRect clip = ScissorClip(ViewportClip(fb, vpX, vpY, vpW, vpH),
                                                owner_.IsScissorTestEnabled(), scX, scY, scW, scH);
        GpuDrawParams spriteParams;
        spriteParams.texture0 = swTexture;
        spriteParams.textureEnabled = true;
        // REMED-GFX-082: honor RasterizerState.FillMode for the sprite's two quad triangles too. Each
        // draws ALL THREE of its edges (kEdgeAll), so a wireframe sprite shows its quad outline plus
        // the internal triangle-split diagonal -- real submitted geometry, matching D3D11/FNA (not
        // suppressed like the 3D near-plane clip diagonal). Passed THROUGH SpriteBatch.Begin's
        // RasterizerState via GraphicsDevice (REMED-GFX-081).
        const bool wire = (owner_.GetFillMode() == 1);
        RasterizeTriangleShaded(fb, depthTestEnabled, blendEnabled, cullMode, spriteParams, clip,
                                rv0, rv1, rv2, wire, kEdgeAll);
        RasterizeTriangleShaded(fb, depthTestEnabled, blendEnabled, cullMode, spriteParams, clip,
                                rv2, rv3, rv0, wire, kEdgeAll);
    }

    // ---- SoftwareGraphicsBackend ----

    SoftwareGraphicsBackend::SoftwareGraphicsBackend(int virtualWidth, int virtualHeight)
        : virtualWidth_(virtualWidth), virtualHeight_(virtualHeight)
    {
        backbuffer_.Resize(virtualWidth > 0 ? virtualWidth : 1024, virtualHeight > 0 ? virtualHeight : 768);
    }

    SoftwareGraphicsBackend::~SoftwareGraphicsBackend() = default;

    SoftwareFramebuffer& SoftwareGraphicsBackend::CurrentFramebuffer()
    {
        return currentRenderTarget_ != nullptr ? currentRenderTarget_->Framebuffer() : backbuffer_;
    }

    const SoftwareFramebuffer& SoftwareGraphicsBackend::CurrentFramebuffer() const
    {
        return currentRenderTarget_ != nullptr ? currentRenderTarget_->Framebuffer() : backbuffer_;
    }

    void SoftwareGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        CurrentFramebuffer().ClearColor(r, g, b, a);
    }

    void SoftwareGraphicsBackend::Present() {}

    void SoftwareGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        const SoftwareFramebuffer& fb = CurrentFramebuffer();
        width = fb.width;
        height = fb.height;
    }

    void SoftwareGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
        if (currentRenderTarget_ == nullptr)
            backbuffer_.Resize(width, height);
    }

    void SoftwareGraphicsBackend::SetPresentationMode(int) {}

    void SoftwareGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (w < 0 || h < 0)
            throw std::runtime_error("SoftwareGraphicsBackend::ReadBackbuffer: negative width/height");

        const SoftwareFramebuffer& fb = CurrentFramebuffer();
        for (int row = 0; row < h; ++row)
        {
            const int srcY = y + row;
            for (int col = 0; col < w; ++col)
            {
                const int srcX = x + col;
                const std::size_t dstIndex = (static_cast<std::size_t>(row) * static_cast<std::size_t>(w) +
                                              static_cast<std::size_t>(col)) * 4u;
                if (srcX < 0 || srcX >= fb.width || srcY < 0 || srcY >= fb.height)
                {
                    pixels[dstIndex + 0] = 0;
                    pixels[dstIndex + 1] = 0;
                    pixels[dstIndex + 2] = 0;
                    pixels[dstIndex + 3] = 0;
                    continue;
                }
                const std::size_t srcIndex = (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(fb.width) +
                                              static_cast<std::size_t>(srcX)) * 4u;
                pixels[dstIndex + 0] = fb.color[srcIndex + 0];
                pixels[dstIndex + 1] = fb.color[srcIndex + 1];
                pixels[dstIndex + 2] = fb.color[srcIndex + 2];
                pixels[dstIndex + 3] = fb.color[srcIndex + 3];
            }
        }
    }

    std::unique_ptr<ITextureBackend> SoftwareGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<SoftwareTextureBackend>(data);
    }

    std::unique_ptr<ITextureCubeBackend> SoftwareGraphicsBackend::CreateTextureCube(int size, bool, int)
    {
        return std::make_unique<SoftwareTextureCubeBackend>(size);
    }

    bool SoftwareGraphicsBackend::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            // REMED-CONTENT-004: Texture3D remains an explicit, documented v1 scope boundary for
            // this backend (see this header's own "Boundaries" comment) -- CreateTexture3D() keeps
            // IGraphicsBackend's shared default (returns nullptr). Reported here so Texture3D's own
            // constructor can fail cleanly instead of silently discarding every SetData()/GetData()
            // call.
            case CNA::GraphicsCapability::Texture3D:
                return false;
            default:
                return true;
        }
    }

    std::unique_ptr<ISpriteBatchBackend> SoftwareGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<SoftwareSpriteBatchBackend>(*this);
    }

    std::unique_ptr<IRenderTargetBackend> SoftwareGraphicsBackend::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<SoftwareRenderTargetBackend>(w, h, depthFormat, mipMap, multiSampleCount);
    }

    void SoftwareGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        if (currentRenderTarget_ != nullptr)
            currentRenderTarget_->UnbindAsRenderTarget();
        currentRenderTarget_ = static_cast<SoftwareRenderTargetBackend*>(rt);
        if (currentRenderTarget_ != nullptr)
            currentRenderTarget_->BindAsRenderTarget();
    }

    std::unique_ptr<IEffectBackend> SoftwareGraphicsBackend::CreateEffectBackend(const std::string& vertSrc,
                                                                                const std::string& fragSrc)
    {
        auto effect = std::make_unique<SoftwareEffectBackend>();
        effect->CompileProgram(vertSrc, fragSrc);
        return effect;
    }

    void SoftwareGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                  int colorDstBlend, int alphaDstBlend, int, int)
    {
        // Blend::One=0, Blend::Zero=1 -> Opaque preset (src=One, dst=Zero), the only combination
        // v1 treats as "no blending" -- matches EasyGLGraphicsBackend::ApplyBlendState's own exact
        // Opaque-detection formula (design decision 7: only Opaque/AlphaBlend distinguished in v1;
        // any other combination is treated as the simplified AlphaBlend case).
        blendEnabled_ = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                          alphaSrcBlend == 0 && alphaDstBlend == 1);
    }

    void SoftwareGraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool, int, bool, int, int, int, int, int,
                                                         int, int, bool, int, int, int, int)
    {
        depthTestEnabled_ = depthEnable;
    }

    void SoftwareGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                                       float, float)
    {
        cullMode_ = cullMode;
        // REMED-GFX-082: capture FillMode (previously discarded). 0=Solid, 1=WireFrame -- the raster
        // paths render only triangle edges when WireFrame. Independent of CullMode/ScissorTestEnable.
        fillMode_ = fillMode;
        // REMED-GFX-080: capture ScissorTestEnable (previously discarded). Independent of the stored
        // ScissorRectangle -- toggling this on/off enables/disables the same stored rectangle.
        scissorTestEnable_ = scissorTestEnable;
    }

    void SoftwareGraphicsBackend::ApplySamplerState(int slot, int, int, int, int)
    {
        if (slot < 0 || slot >= 16)
            throw std::runtime_error("SoftwareGraphicsBackend::ApplySamplerState: slot must be 0..15");
    }

    // REMED-GFX-080: store the ScissorRectangle so the raster paths can intersect it into their
    // effective clip when scissor testing is enabled (previously a no-op, so ScissorRectangle never
    // clipped anything). GraphicsDevice pushes this on every setScissorRectangleProperty() and
    // resets it to the full target on each RenderTarget transition, so this single field is always
    // relative to the currently active target. The rectangle is stored regardless of the current
    // ScissorTestEnable flag -- it becomes active if a later RasterizerState enables scissor testing.
    void SoftwareGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        scissorSet_ = true;
        scissorX_ = x;
        scissorY_ = y;
        scissorWidth_ = w;
        scissorHeight_ = h;
    }

    // REMED-GFX-073: store the viewport so the SpriteBatch path can place its viewport-local quads
    // at (x,y) and clip them to (x,y,w,h). GraphicsDevice pushes this on every setViewportProperty()
    // and resets it to the full target on each RenderTarget transition, so this single field is
    // always relative to the currently active target. (The 3D DrawPrimitivesEx path still maps NDC
    // over the full framebuffer -- a separate, non-SpriteBatch viewport gap; see the remediation
    // notes.)
    void SoftwareGraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        viewportSet_ = true;
        viewportX_ = x;
        viewportY_ = y;
        viewportWidth_ = w;
        viewportHeight_ = h;
        viewportMinDepth_ = minDepth;
        viewportMaxDepth_ = maxDepth;
    }

    void SoftwareGraphicsBackend::GetActiveViewport(int& x, int& y, int& w, int& h) const
    {
        if (!viewportSet_)
        {
            const SoftwareFramebuffer& fb = CurrentFramebuffer();
            x = 0;
            y = 0;
            w = fb.width;
            h = fb.height;
            return;
        }
        x = viewportX_;
        y = viewportY_;
        w = viewportWidth_;
        h = viewportHeight_;
    }

    // REMED-GFX-080: mirrors GetActiveViewport -- returns the stored ScissorRectangle, or the full
    // current framebuffer when none was set (so enabling scissor testing without an explicit
    // rectangle is an inert clip, matching XNA's default full-target ScissorRectangle).
    void SoftwareGraphicsBackend::GetActiveScissor(int& x, int& y, int& w, int& h) const
    {
        if (!scissorSet_)
        {
            const SoftwareFramebuffer& fb = CurrentFramebuffer();
            x = 0;
            y = 0;
            w = fb.width;
            h = fb.height;
            return;
        }
        x = scissorX_;
        y = scissorY_;
        w = scissorWidth_;
        h = scissorHeight_;
    }

    void SoftwareGraphicsBackend::GetActiveViewportRaster(int& x, int& y, int& w, int& h,
                                                          float& minDepth, float& maxDepth) const
    {
        GetActiveViewport(x, y, w, h);
        // The depth range defaults to the full [0,1] until a custom viewport is set (matching
        // GetActiveViewport's full-framebuffer x/y/w/h fallback); once SetViewport has run, the
        // stored MinDepth/MaxDepth apply. This is the single point where the 3D path resolves its
        // viewport, so all four draw entry points stay consistent.
        minDepth = viewportSet_ ? viewportMinDepth_ : 0.0f;
        maxDepth = viewportSet_ ? viewportMaxDepth_ : 1.0f;
    }

    void SoftwareGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        SoftwareFramebuffer& fb = CurrentFramebuffer();
        fb.ClearColor(r, g, b, a);
        fb.ClearDepthValue(depth);
    }

    void SoftwareGraphicsBackend::ClearDepth(float depth) { CurrentFramebuffer().ClearDepthValue(depth); }
    void SoftwareGraphicsBackend::ClearStencil(int) {}
    void SoftwareGraphicsBackend::ClearDepthAndStencil(float depth, int) { CurrentFramebuffer().ClearDepthValue(depth); }
    void SoftwareGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int)
    { CurrentFramebuffer().ClearColor(r, g, b, a); }
    void SoftwareGraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int)
    { ClearColorAndDepth(r, g, b, a, depth); }

    void SoftwareGraphicsBackend::SetDepthTestEnabled(bool enabled) { depthTestEnabled_ = enabled; }
    void SoftwareGraphicsBackend::SetBlendEnabled(bool) {}
    void SoftwareGraphicsBackend::SetDepthWriteEnabled(bool) {}

    std::unique_ptr<IVertexBufferBackend> SoftwareGraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<SoftwareVertexBufferBackend>(vertex_capacity);
    }

    std::unique_ptr<IIndexBufferBackend> SoftwareGraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<SoftwareIndexBufferBackend>(index_capacity, false);
    }

    std::unique_ptr<IIndexBufferBackend> SoftwareGraphicsBackend::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<SoftwareIndexBufferBackend>(index_capacity, true);
    }

    // Phase S4 (SOFTWARE-30..34): real transform/rasterize/depth-test pipeline. TriangleList only
    // in v1 (the owner's own stated minimal first-version scope) -- other PrimitiveType values
    // throw rather than silently misrendering.
    void SoftwareGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb, const Matrix& world,
                                                        const Matrix& view, const Matrix& projection,
                                                        PrimitiveType primitive, int primitiveCount)
    {
        if (primitiveCount <= 0)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawColoredPrimitives: primitiveCount must be > 0");
        if (primitive != PrimitiveType::TriangleList)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawColoredPrimitives: only TriangleList is supported in v1");
        if (primitiveCount * 3 > vb.GetVertexCount())
            throw std::runtime_error(
                "SoftwareGraphicsBackend::DrawColoredPrimitives: primitiveCount needs more vertices than the bound buffer has");

        const auto& swVb = static_cast<const SoftwareVertexBufferBackend&>(vb);
        const std::uint8_t* base = swVb.Data().data();
        const std::size_t stride = swVb.Stride();

        const Matrix combined = world * view * projection;
        SoftwareFramebuffer& fb = CurrentFramebuffer();
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

        for (int i = 0; i < primitiveCount; ++i)
        {
            ClipVertex cv[3];
            for (int k = 0; k < 3; ++k)
            {
                const std::uint8_t* raw = base + static_cast<std::size_t>(i * 3 + k) * stride;
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
            RasterizeTriangle(fb, depthTestEnabled_, cullMode_, clip, rv[0], rv[1], rv[2], wire, mask0);
            if (clippedCount == 4)
                RasterizeTriangle(fb, depthTestEnabled_, cullMode_, clip, rv[0], rv[2], rv[3], wire,
                                  kEdgeV1V2 | kEdgeV2V0);
        }
    }

    void SoftwareGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                                               const Matrix& world, const Matrix& view, const Matrix& projection,
                                                               PrimitiveType primitive, int primitiveCount)
    {
        if (primitiveCount <= 0)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawIndexedColoredPrimitives: primitiveCount must be > 0");
        if (primitive != PrimitiveType::TriangleList)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawIndexedColoredPrimitives: only TriangleList is supported in v1");
        if (primitiveCount * 3 > ib.GetIndexCount())
            throw std::runtime_error(
                "SoftwareGraphicsBackend::DrawIndexedColoredPrimitives: primitiveCount needs more indices than the bound buffer has");

        const auto& swVb = static_cast<const SoftwareVertexBufferBackend&>(vb);
        const auto& swIb = static_cast<const SoftwareIndexBufferBackend&>(ib);
        const std::uint8_t* vbBase = swVb.Data().data();
        const std::size_t stride = swVb.Stride();
        const std::uint8_t* ibBase = swIb.Data().data();
        const bool thirtyTwoBit = swIb.IsThirtyTwoBit();

        const Matrix combined = world * view * projection;
        SoftwareFramebuffer& fb = CurrentFramebuffer();
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

        const auto readIndex = [&](int i) -> std::uint32_t {
            if (thirtyTwoBit)
            {
                std::uint32_t v;
                std::memcpy(&v, ibBase + static_cast<std::size_t>(i) * sizeof(std::uint32_t), sizeof(std::uint32_t));
                return v;
            }
            std::uint16_t v;
            std::memcpy(&v, ibBase + static_cast<std::size_t>(i) * sizeof(std::uint16_t), sizeof(std::uint16_t));
            return v;
        };

        for (int i = 0; i < primitiveCount; ++i)
        {
            ClipVertex cv[3];
            for (int k = 0; k < 3; ++k)
            {
                const std::uint32_t idx = readIndex(i * 3 + k);
                const std::uint8_t* raw = vbBase + static_cast<std::size_t>(idx) * stride;
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
            RasterizeTriangle(fb, depthTestEnabled_, cullMode_, clip, rv[0], rv[1], rv[2], wire, mask0);
            if (clippedCount == 4)
                RasterizeTriangle(fb, depthTestEnabled_, cullMode_, clip, rv[0], rv[2], rv[3], wire,
                                  kEdgeV1V2 | kEdgeV2V0);
        }
    }

    // Phase S5/S6 (SOFTWARE-40..43, 50): the effect-aware draw path -- stride-inferred vertex
    // layout (design decision 2), nearest-neighbor/bilinear texture sampling, diffuseColor
    // modulation, and the simplified Opaque/AlphaBlend choice (design decision 7).
    // dualTexture/envMapping/skinned are supported (SOFTWARE-82; strides 32/52, see
    // BuildGenericClipVertex/RasterizeTriangleShaded) but without any per-light diffuse lighting
    // sum -- lightingEnabled/fogEnabled remain out of scope for v1 (design decision 6).
    void SoftwareGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb, const Matrix& world,
                                                   const Matrix& view, const Matrix& projection,
                                                   PrimitiveType primitive, int primitiveCount,
                                                   const GpuDrawParams& params)
    {
        if (primitiveCount <= 0)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawPrimitivesEx: primitiveCount must be > 0");
        if (primitive != PrimitiveType::TriangleList)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawPrimitivesEx: only TriangleList is supported in v1");
        if (params.textureEnabled && params.texture0 == nullptr)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawPrimitivesEx: TextureEnabled=true but texture0 is null");
        if (params.dualTexture && params.texture1 == nullptr)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawPrimitivesEx: dualTexture=true but texture1 is null");
        if (params.envMapping && params.envMap == nullptr)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawPrimitivesEx: envMapping=true but envMap is null");
        if (primitiveCount * 3 > vb.GetVertexCount())
            throw std::runtime_error(
                "SoftwareGraphicsBackend::DrawPrimitivesEx: primitiveCount needs more vertices than the bound buffer has");

        const auto& swVb = static_cast<const SoftwareVertexBufferBackend&>(vb);
        const std::size_t stride = swVb.Stride();
        if (stride != 16 && stride != 20 && stride != 24 && stride != 32 && stride != 52)
            throw std::runtime_error(
                "SoftwareGraphicsBackend::DrawPrimitivesEx: unsupported vertex stride "
                "(only 16/20/24/32/52 supported in v1)");

        const std::uint8_t* base = swVb.Data().data();

        const Matrix combined = world * view * projection;
        SoftwareFramebuffer& fb = CurrentFramebuffer();
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

        for (int i = 0; i < primitiveCount; ++i)
        {
            ClipVertex cv[3];
            for (int k = 0; k < 3; ++k)
            {
                const std::uint8_t* raw = base + static_cast<std::size_t>(i * 3 + k) * stride;
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
            RasterizeTriangleShaded(fb, depthTestEnabled_, blendEnabled_, cullMode_, params,
                                    clip, rv[0], rv[1], rv[2], wire, mask0);
            if (clippedCount == 4)
                RasterizeTriangleShaded(fb, depthTestEnabled_, blendEnabled_, cullMode_, params,
                                        clip, rv[0], rv[2], rv[3], wire, kEdgeV1V2 | kEdgeV2V0);
        }
    }

    void SoftwareGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                                          const Matrix& world, const Matrix& view,
                                                          const Matrix& projection, PrimitiveType primitive,
                                                          int primitiveCount, const GpuDrawParams& params)
    {
        if (primitiveCount <= 0)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawIndexedPrimitivesEx: primitiveCount must be > 0");
        if (primitive != PrimitiveType::TriangleList)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawIndexedPrimitivesEx: only TriangleList is supported in v1");
        if (params.textureEnabled && params.texture0 == nullptr)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawIndexedPrimitivesEx: TextureEnabled=true but texture0 is null");
        if (params.dualTexture && params.texture1 == nullptr)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawIndexedPrimitivesEx: dualTexture=true but texture1 is null");
        if (params.envMapping && params.envMap == nullptr)
            throw std::runtime_error("SoftwareGraphicsBackend::DrawIndexedPrimitivesEx: envMapping=true but envMap is null");
        if (primitiveCount * 3 > ib.GetIndexCount())
            throw std::runtime_error(
                "SoftwareGraphicsBackend::DrawIndexedPrimitivesEx: primitiveCount needs more indices than the bound buffer has");

        const auto& swVb = static_cast<const SoftwareVertexBufferBackend&>(vb);
        const auto& swIb = static_cast<const SoftwareIndexBufferBackend&>(ib);
        const std::size_t stride = swVb.Stride();
        if (stride != 16 && stride != 20 && stride != 24 && stride != 32 && stride != 52)
            throw std::runtime_error(
                "SoftwareGraphicsBackend::DrawIndexedPrimitivesEx: unsupported vertex stride "
                "(only 16/20/24/32/52 supported in v1)");

        const std::uint8_t* vbBase = swVb.Data().data();
        const std::uint8_t* ibBase = swIb.Data().data();
        const bool thirtyTwoBit = swIb.IsThirtyTwoBit();

        const Matrix combined = world * view * projection;
        SoftwareFramebuffer& fb = CurrentFramebuffer();
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

        const auto readIndex = [&](int i) -> std::uint32_t {
            if (thirtyTwoBit)
            {
                std::uint32_t val;
                std::memcpy(&val, ibBase + static_cast<std::size_t>(i) * sizeof(std::uint32_t), sizeof(std::uint32_t));
                return val;
            }
            std::uint16_t val;
            std::memcpy(&val, ibBase + static_cast<std::size_t>(i) * sizeof(std::uint16_t), sizeof(std::uint16_t));
            return val;
        };

        for (int i = 0; i < primitiveCount; ++i)
        {
            ClipVertex cv[3];
            for (int k = 0; k < 3; ++k)
            {
                const std::uint32_t idx = readIndex(i * 3 + k);
                const std::uint8_t* raw = vbBase + static_cast<std::size_t>(idx) * stride;
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
            RasterizeTriangleShaded(fb, depthTestEnabled_, blendEnabled_, cullMode_, params,
                                    clip, rv[0], rv[1], rv[2], wire, mask0);
            if (clippedCount == 4)
                RasterizeTriangleShaded(fb, depthTestEnabled_, blendEnabled_, cullMode_, params,
                                        clip, rv[0], rv[2], rv[3], wire, kEdgeV1V2 | kEdgeV2V0);
        }
    }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Software::SoftwareGraphicsBackend>(args.virtualWidth, args.virtualHeight);
    }
}
