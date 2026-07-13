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
        /// into screen space. Returns false (vertex not usable) if the vertex is behind or on the
        /// near plane -- the caller culls the whole triangle in that case (SOFTWARE-34: minimal
        /// near-plane handling, no polygon clipping in v1).
        bool TransformPositionColorVertex(const std::uint8_t* raw, const Matrix& combined,
                                          int viewportWidth, int viewportHeight, RasterVertex& out)
        {
            Vector3 position;
            std::memcpy(&position, raw, sizeof(Vector3));

            const Vector4 clip = Vector4::Transform(position, combined);
            if (clip.W <= 1e-5f)
                return false;

            const float invW = 1.0f / clip.W;
            const float ndcX = clip.X * invW;
            const float ndcY = clip.Y * invW;
            const float ndcZ = clip.Z * invW;

            out.x = (ndcX * 0.5f + 0.5f) * static_cast<float>(viewportWidth);
            out.y = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(viewportHeight);
            out.depth = ndcZ;
            out.invW = invW;

            float r, g, b, a;
            UnpackColorBytes(raw + sizeof(Vector3), r, g, b, a);
            out.r = r * invW;
            out.g = g * invW;
            out.b = b * invW;
            out.a = a * invW;
            return true;
        }

        float EdgeFunction(float ax, float ay, float bx, float by, float px, float py)
        {
            return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
        }

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

        /// Fills one triangle into `fb` using a standard edge-function/barycentric rasterizer,
        /// with a per-pixel depth test against `fb.depthBuffer` when `depthTestEnabled`. Accepts
        /// either triangle winding order (no backface culling in v1 -- CullNone-equivalent
        /// always, a real scope simplification: SOFTWARE-32 is about proving correct rasterization
        /// exists at all, not the full RasterizerState feature set).
        void RasterizeTriangle(SoftwareFramebuffer& fb, bool depthTestEnabled,
                               const RasterVertex& v0, const RasterVertex& v1, const RasterVertex& v2)
        {
            const float area = EdgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
            if (area == 0.0f)
                return;  // degenerate (zero-area) triangle

            const float minXf = std::min({v0.x, v1.x, v2.x});
            const float maxXf = std::max({v0.x, v1.x, v2.x});
            const float minYf = std::min({v0.y, v1.y, v2.y});
            const float maxYf = std::max({v0.y, v1.y, v2.y});

            const int minX = std::max(0, static_cast<int>(std::floor(minXf)));
            const int maxX = std::min(fb.width - 1, static_cast<int>(std::ceil(maxXf)));
            const int minY = std::max(0, static_cast<int>(std::floor(minYf)));
            const int maxY = std::min(fb.height - 1, static_cast<int>(std::ceil(maxYf)));

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

                    const std::size_t pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.width) +
                                                   static_cast<std::size_t>(x);
                    if (depthTestEnabled && depth > fb.depthBuffer[pixelIndex])
                        continue;

                    const float invW = lambda0 * v0.invW + lambda1 * v1.invW + lambda2 * v2.invW;
                    const float r = (lambda0 * v0.r + lambda1 * v1.r + lambda2 * v2.r) / invW;
                    const float g = (lambda0 * v0.g + lambda1 * v1.g + lambda2 * v2.g) / invW;
                    const float b = (lambda0 * v0.b + lambda1 * v1.b + lambda2 * v2.b) / invW;
                    const float a = (lambda0 * v0.a + lambda1 * v1.a + lambda2 * v2.a) / invW;

                    fb.depthBuffer[pixelIndex] = depth;

                    const std::size_t colorIndex = pixelIndex * 4;
                    fb.color[colorIndex + 0] = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
                    fb.color[colorIndex + 1] = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
                    fb.color[colorIndex + 2] = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
                    fb.color[colorIndex + 3] = static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
                }
            }
        }

        // ---- Phase S5/S6: generalized (textured/blended/effect-driven) rasterization ----

        /// Transforms a vertex whose byte layout is inferred from `stride` (plan_software.md
        /// design decision 2: 16=VertexPositionColor, 20=VertexPositionTexture,
        /// 24=VertexPositionColorTexture) into screen space, for the DrawPrimitivesEx/
        /// DrawIndexedPrimitivesEx path. `vertexColorEnabled` mirrors GpuDrawParams' own flag --
        /// when false, vertex color is treated as opaque white so it doesn't affect the eventual
        /// texture/diffuse modulation, matching a real Effect's own VertexColorEnabled=false
        /// behavior. Returns false (cull the whole triangle) for the same near-plane reason as
        /// TransformPositionColorVertex.
        bool TransformGenericVertex(const std::uint8_t* raw, std::size_t stride, const Matrix& combined,
                                    int viewportWidth, int viewportHeight, bool vertexColorEnabled,
                                    RasterVertex& out)
        {
            Vector3 position;
            std::memcpy(&position, raw, sizeof(Vector3));

            const Vector4 clip = Vector4::Transform(position, combined);
            if (clip.W <= 1e-5f)
                return false;

            const float invW = 1.0f / clip.W;
            const float ndcX = clip.X * invW;
            const float ndcY = clip.Y * invW;
            const float ndcZ = clip.Z * invW;

            out.x = (ndcX * 0.5f + 0.5f) * static_cast<float>(viewportWidth);
            out.y = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(viewportHeight);
            out.depth = ndcZ;
            out.invW = invW;

            float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
            float u = 0.0f, v = 0.0f;
            if (stride == 16)
            {
                UnpackColorBytes(raw + 12, r, g, b, a);
            }
            else if (stride == 20)
            {
                std::memcpy(&u, raw + 12, sizeof(float));
                std::memcpy(&v, raw + 16, sizeof(float));
            }
            else if (stride == 24)
            {
                UnpackColorBytes(raw + 12, r, g, b, a);
                std::memcpy(&u, raw + 16, sizeof(float));
                std::memcpy(&v, raw + 20, sizeof(float));
            }

            if (!vertexColorEnabled)
            {
                r = g = b = a = 1.0f;
            }

            out.r = r * invW;
            out.g = g * invW;
            out.b = b * invW;
            out.a = a * invW;
            out.u = u * invW;
            out.v = v * invW;
            return true;
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

        /// General-purpose triangle fill for the DrawPrimitivesEx/DrawIndexedPrimitivesEx and
        /// SpriteBatch paths: adds nearest-neighbor texture sampling, diffuseColor modulation, and
        /// a simplified Opaque/AlphaBlend choice (design decisions 7/6) on top of RasterizeTriangle's
        /// depth-tested, perspective-correct color interpolation.
        void RasterizeTriangleShaded(SoftwareFramebuffer& fb, bool depthTestEnabled, bool blendEnabled,
                                     bool textureEnabled, const SoftwareTextureBackend* texture,
                                     float diffuseR, float diffuseG, float diffuseB, float diffuseA,
                                     const RasterVertex& v0, const RasterVertex& v1, const RasterVertex& v2)
        {
            const float area = EdgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
            if (area == 0.0f)
                return;

            const float minXf = std::min({v0.x, v1.x, v2.x});
            const float maxXf = std::max({v0.x, v1.x, v2.x});
            const float minYf = std::min({v0.y, v1.y, v2.y});
            const float maxYf = std::max({v0.y, v1.y, v2.y});

            const int minX = std::max(0, static_cast<int>(std::floor(minXf)));
            const int maxX = std::min(fb.width - 1, static_cast<int>(std::ceil(maxXf)));
            const int minY = std::max(0, static_cast<int>(std::floor(minYf)));
            const int maxY = std::min(fb.height - 1, static_cast<int>(std::ceil(maxYf)));

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

                    const std::size_t pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.width) +
                                                   static_cast<std::size_t>(x);
                    if (depthTestEnabled && depth > fb.depthBuffer[pixelIndex])
                        continue;

                    const float invW = lambda0 * v0.invW + lambda1 * v1.invW + lambda2 * v2.invW;
                    float r = (lambda0 * v0.r + lambda1 * v1.r + lambda2 * v2.r) / invW;
                    float g = (lambda0 * v0.g + lambda1 * v1.g + lambda2 * v2.g) / invW;
                    float b = (lambda0 * v0.b + lambda1 * v1.b + lambda2 * v2.b) / invW;
                    float a = (lambda0 * v0.a + lambda1 * v1.a + lambda2 * v2.a) / invW;

                    if (textureEnabled && texture != nullptr)
                    {
                        const float u = (lambda0 * v0.u + lambda1 * v1.u + lambda2 * v2.u) / invW;
                        const float v = (lambda0 * v0.v + lambda1 * v1.v + lambda2 * v2.v) / invW;
                        float texR, texG, texB, texA;
                        SampleBilinear(*texture, u, v, texR, texG, texB, texA);
                        r *= texR;
                        g *= texG;
                        b *= texB;
                        a *= texA;
                    }

                    r *= diffuseR;
                    g *= diffuseG;
                    b *= diffuseB;
                    a *= diffuseA;

                    fb.depthBuffer[pixelIndex] = depth;

                    const std::size_t colorIndex = pixelIndex * 4;
                    if (!blendEnabled)
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
                        // AlphaBlend/NonPremultiplied/Additive-ish real BlendState presets alike,
                        // an intentional v1 simplification rather than a full blend-equation
                        // interpreter.
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

        const auto placeCorner = [&](float px, float py) -> Vector2 {
            const float rx = dx + px * cosR - py * sinR;
            const float ry = dy + px * sinR + py * cosR;
            // SpriteBatch::SetTransformMatrix()'s optional 2D transform, applied as a point
            // transform (z=0) directly on the already screen-space corner.
            const Vector3 transformed = Vector3::Transform(Vector3(rx, ry, 0.0f), transformMatrix_);
            return Vector2(transformed.X, transformed.Y);
        };

        const Vector2 c0 = placeCorner(p0x, p0y);
        const Vector2 c1 = placeCorner(p1x, p1y);
        const Vector2 c2 = placeCorner(p2x, p2y);
        const Vector2 c3 = placeCorner(p3x, p3y);

        const RasterVertex rv0 = MakeScreenSpaceVertex(c0.X, c0.Y, layerDepth, r, g, b, a, u1, v1);
        const RasterVertex rv1 = MakeScreenSpaceVertex(c1.X, c1.Y, layerDepth, r, g, b, a, u2, v1);
        const RasterVertex rv2 = MakeScreenSpaceVertex(c2.X, c2.Y, layerDepth, r, g, b, a, u2, v2);
        const RasterVertex rv3 = MakeScreenSpaceVertex(c3.X, c3.Y, layerDepth, r, g, b, a, u1, v2);

        SoftwareFramebuffer& fb = owner_.CurrentFramebuffer();
        const bool depthTestEnabled = owner_.IsDepthTestEnabled();
        const bool blendEnabled = owner_.IsBlendEnabled();
        RasterizeTriangleShaded(fb, depthTestEnabled, blendEnabled, true, swTexture, 1.0f, 1.0f, 1.0f, 1.0f,
                                rv0, rv1, rv2);
        RasterizeTriangleShaded(fb, depthTestEnabled, blendEnabled, true, swTexture, 1.0f, 1.0f, 1.0f, 1.0f,
                                rv2, rv3, rv0);
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

    void SoftwareGraphicsBackend::ApplyRasterizerState(int, int, bool, float, float) {}

    void SoftwareGraphicsBackend::ApplySamplerState(int slot, int, int, int, int)
    {
        if (slot < 0 || slot >= 16)
            throw std::runtime_error("SoftwareGraphicsBackend::ApplySamplerState: slot must be 0..15");
    }

    void SoftwareGraphicsBackend::SetScissorRect(int, int, int, int) {}

    void SoftwareGraphicsBackend::SetViewport(int, int, int, int, float, float) {}

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
        int vw = 0, vh = 0;
        GetViewportSize(vw, vh);

        for (int i = 0; i < primitiveCount; ++i)
        {
            RasterVertex rv[3];
            bool allValid = true;
            for (int k = 0; k < 3; ++k)
            {
                const std::uint8_t* raw = base + static_cast<std::size_t>(i * 3 + k) * stride;
                if (!TransformPositionColorVertex(raw, combined, vw, vh, rv[k]))
                {
                    allValid = false;
                    break;
                }
            }
            if (!allValid)
                continue;  // SOFTWARE-34: minimal near-plane handling -- cull, don't clip, in v1.

            RasterizeTriangle(fb, depthTestEnabled_, rv[0], rv[1], rv[2]);
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
        int vw = 0, vh = 0;
        GetViewportSize(vw, vh);

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
            RasterVertex rv[3];
            bool allValid = true;
            for (int k = 0; k < 3; ++k)
            {
                const std::uint32_t idx = readIndex(i * 3 + k);
                const std::uint8_t* raw = vbBase + static_cast<std::size_t>(idx) * stride;
                if (!TransformPositionColorVertex(raw, combined, vw, vh, rv[k]))
                {
                    allValid = false;
                    break;
                }
            }
            if (!allValid)
                continue;

            RasterizeTriangle(fb, depthTestEnabled_, rv[0], rv[1], rv[2]);
        }
    }

    // Phase S5/S6 (SOFTWARE-40..43, 50): the effect-aware draw path -- stride-inferred vertex
    // layout (design decision 2), nearest-neighbor texture sampling, diffuseColor modulation, and
    // the simplified Opaque/AlphaBlend choice (design decision 7). lightingEnabled/fogEnabled/
    // dualTexture/envMapping/skinned are all explicitly out of scope for v1 (design decision 6) --
    // GpuDrawParams' other fields are simply not read here.
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
        if (primitiveCount * 3 > vb.GetVertexCount())
            throw std::runtime_error(
                "SoftwareGraphicsBackend::DrawPrimitivesEx: primitiveCount needs more vertices than the bound buffer has");

        const auto& swVb = static_cast<const SoftwareVertexBufferBackend&>(vb);
        const std::size_t stride = swVb.Stride();
        if (stride != 16 && stride != 20 && stride != 24)
            throw std::runtime_error(
                "SoftwareGraphicsBackend::DrawPrimitivesEx: unsupported vertex stride (only 16/20/24 supported in v1)");

        const auto* texture = dynamic_cast<const SoftwareTextureBackend*>(params.texture0);
        const std::uint8_t* base = swVb.Data().data();

        const Matrix combined = world * view * projection;
        SoftwareFramebuffer& fb = CurrentFramebuffer();
        int vw = 0, vh = 0;
        GetViewportSize(vw, vh);

        for (int i = 0; i < primitiveCount; ++i)
        {
            RasterVertex rv[3];
            bool allValid = true;
            for (int k = 0; k < 3; ++k)
            {
                const std::uint8_t* raw = base + static_cast<std::size_t>(i * 3 + k) * stride;
                if (!TransformGenericVertex(raw, stride, combined, vw, vh, params.vertexColorEnabled, rv[k]))
                {
                    allValid = false;
                    break;
                }
            }
            if (!allValid)
                continue;

            RasterizeTriangleShaded(fb, depthTestEnabled_, blendEnabled_, params.textureEnabled, texture,
                                    params.diffuseColor[0], params.diffuseColor[1], params.diffuseColor[2],
                                    params.diffuseColor[3], rv[0], rv[1], rv[2]);
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
        if (primitiveCount * 3 > ib.GetIndexCount())
            throw std::runtime_error(
                "SoftwareGraphicsBackend::DrawIndexedPrimitivesEx: primitiveCount needs more indices than the bound buffer has");

        const auto& swVb = static_cast<const SoftwareVertexBufferBackend&>(vb);
        const auto& swIb = static_cast<const SoftwareIndexBufferBackend&>(ib);
        const std::size_t stride = swVb.Stride();
        if (stride != 16 && stride != 20 && stride != 24)
            throw std::runtime_error(
                "SoftwareGraphicsBackend::DrawIndexedPrimitivesEx: unsupported vertex stride (only 16/20/24 supported in v1)");

        const auto* texture = dynamic_cast<const SoftwareTextureBackend*>(params.texture0);
        const std::uint8_t* vbBase = swVb.Data().data();
        const std::uint8_t* ibBase = swIb.Data().data();
        const bool thirtyTwoBit = swIb.IsThirtyTwoBit();

        const Matrix combined = world * view * projection;
        SoftwareFramebuffer& fb = CurrentFramebuffer();
        int vw = 0, vh = 0;
        GetViewportSize(vw, vh);

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
            RasterVertex rv[3];
            bool allValid = true;
            for (int k = 0; k < 3; ++k)
            {
                const std::uint32_t idx = readIndex(i * 3 + k);
                const std::uint8_t* raw = vbBase + static_cast<std::size_t>(idx) * stride;
                if (!TransformGenericVertex(raw, stride, combined, vw, vh, params.vertexColorEnabled, rv[k]))
                {
                    allValid = false;
                    break;
                }
            }
            if (!allValid)
                continue;

            RasterizeTriangleShaded(fb, depthTestEnabled_, blendEnabled_, params.textureEnabled, texture,
                                    params.diffuseColor[0], params.diffuseColor[1], params.diffuseColor[2],
                                    params.diffuseColor[3], rv[0], rv[1], rv[2]);
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
