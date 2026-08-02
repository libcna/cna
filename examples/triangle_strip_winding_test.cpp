// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-183 -- triangle-strip parity and culling, measured against the XNA/FNA oracle.
//
// GFX-160 established the source convention independently from FNA SpriteBatch: clockwise as
// displayed is XNA's front face, CullClockwiseFace removes it, and CullCounterClockwiseFace removes
// the opposite face. For a horizontal strip, the clockwise source order starts bottom/top in each
// successive column:
//
//     BL0, TL0, BL1, TL1, BL2, TL2, ...
//
// Fixed-function strip assembly reverses the first two vertices of every odd-numbered primitive,
// so every triangle above remains clockwise. Starting top/bottom instead produces the identical
// coverage with counter-clockwise winding. The matrix below draws both orders side by side and
// probes every odd and even primitive independently under all three public CullMode values.
//
// The original GFX-183 GTest used TL, BL, TR, BR, which is the counter-clockwise form, but labelled
// it front-facing. Its observed pixels were therefore the exact XNA answer, not a backend defect.
// This fixture keeps geometry and oracle separate so that inversion cannot recur unnoticed.
//
// Coverage:
//   * DrawUserPrimitives and DrawPrimitives triangle strips;
//   * DrawUserIndexedPrimitives and DrawIndexedPrimitives, Uint16 and Uint32;
//   * three- and four-triangle strips, probing every odd/even primitive;
//   * clockwise and counter-clockwise source order under all three cull modes;
//   * nonzero vertexStart, indexOffset/startIndex, and baseVertex;
//   * strip/list/strip/list/strip deferred ordering with repeated state changes;
//   * viewport and scissor changes after queueing, before replay;
//   * caller-array mutation, buffer disposal before replay, repeated target disposal, and device
//     teardown.
//
// Exit code 0 means every supported-path assertion passed. A backend that does not rasterize, or
// whose documented 3D boundary excludes strips, reports that boundary instead of inventing pixels.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#ifdef CNA_BACKEND_WEBGPU
#include "CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kW = 128;
    constexpr int kH = 80;

    const Color kClear(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kYellow(255, 255, 0, 255);
    const Color kWhite(255, 255, 255, 255);
    const Color kPoison(0xCD, 0xCD, 0xCD, 0xCD);

#if defined(CNA_BACKEND_HEADLESS)
    constexpr const char* kBackendName = "HEADLESS";
    constexpr bool kRasterizes = false;
    constexpr bool kKnownStripBoundary = true;
#elif defined(CNA_BACKEND_SOFTWARE)
    constexpr const char* kBackendName = "SOFTWARE";
    constexpr bool kRasterizes = true;
    constexpr bool kKnownStripBoundary = true; // Software v1 accepts TriangleList only.
#elif defined(CNA_BACKEND_EASYGL)
    constexpr const char* kBackendName = "EASYGL";
    constexpr bool kRasterizes = true;
    constexpr bool kKnownStripBoundary = false;
#elif defined(CNA_BACKEND_BGFX)
    constexpr const char* kBackendName = "BGFX";
    constexpr bool kRasterizes = true;
    constexpr bool kKnownStripBoundary = false;
#elif defined(CNA_BACKEND_VULKAN)
    constexpr const char* kBackendName = "VULKAN";
    constexpr bool kRasterizes = true;
    constexpr bool kKnownStripBoundary = false;
#elif defined(CNA_BACKEND_WEBGPU)
    constexpr const char* kBackendName = "WEBGPU";
    constexpr bool kRasterizes = true;
    constexpr bool kKnownStripBoundary = false;
#elif defined(CNA_BACKEND_SDL_GPU)
    constexpr const char* kBackendName = "SDL_GPU";
    constexpr bool kRasterizes = true;
    constexpr bool kKnownStripBoundary = false;
#else
#error "REMED-GFX-183: this backend has no declared triangle-strip control boundary."
#endif

    constexpr bool kEveryPathRequired =
#if defined(CNA_BACKEND_WEBGPU)
        true;
#else
        false;
#endif

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() &&
               a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() &&
               a.getAProperty() == b.getAProperty();
    }

    std::string ColorText(const Color& color)
    {
        return "(" + std::to_string(static_cast<int>(color.getRProperty())) + "," +
               std::to_string(static_cast<int>(color.getGProperty())) + "," +
               std::to_string(static_cast<int>(color.getBProperty())) + "," +
               std::to_string(static_cast<int>(color.getAProperty())) + ")";
    }

    enum class Wind
    {
        Clockwise,
        CounterClockwise
    };

    enum class Path
    {
        UserNonIndexed,
        BufferNonIndexed,
        UserIndexed16,
        UserIndexed32,
        BufferIndexed16,
        BufferIndexed32
    };

    const char* PathText(Path path)
    {
        switch (path)
        {
            case Path::UserNonIndexed: return "DrawUserPrimitives";
            case Path::BufferNonIndexed: return "DrawPrimitives/nonzero vertexStart";
            case Path::UserIndexed16: return "DrawUserIndexedPrimitives/Uint16";
            case Path::UserIndexed32: return "DrawUserIndexedPrimitives/Uint32";
            case Path::BufferIndexed16: return "DrawIndexedPrimitives/Uint16/nonzero start+base";
            case Path::BufferIndexed32: return "DrawIndexedPrimitives/Uint32/nonzero start+base";
        }
        return "unknown";
    }

    const char* CullText(CullMode mode)
    {
        if (mode == CullMode::None) return "CullNone";
        if (mode == CullMode::CullClockwiseFace) return "CullClockwiseFace";
        return "CullCounterClockwiseFace";
    }

    const RasterizerState& CullState(CullMode mode)
    {
        if (mode == CullMode::None) return RasterizerState::CullNone;
        if (mode == CullMode::CullClockwiseFace) return RasterizerState::CullClockwise;
        return RasterizerState::CullCounterClockwise;
    }

    bool ExpectedPresent(Wind wind, CullMode mode)
    {
        if (mode == CullMode::None) return true;
        if (mode == CullMode::CullClockwiseFace)
            return wind == Wind::CounterClockwise;
        return wind == Wind::Clockwise;
    }

    std::vector<VertexPositionColor> MakeStrip(
        float x0, float x1, float yBottom, float yTop,
        int primitiveCount, Wind wind, const Color& color)
    {
        const int vertexCount = primitiveCount + 2;
        const int columnCount = (vertexCount + 1) / 2;
        std::vector<VertexPositionColor> result;
        result.reserve(static_cast<std::size_t>(vertexCount));
        for (int i = 0; i < vertexCount; ++i)
        {
            const int column = i / 2;
            const float t = columnCount > 1
                ? static_cast<float>(column) / static_cast<float>(columnCount - 1)
                : 0.0f;
            const float x = x0 + (x1 - x0) * t;
            const bool firstInColumn = (i % 2) == 0;
            const bool bottom = wind == Wind::Clockwise ? firstInColumn : !firstInColumn;
            result.emplace_back(Vector3(x, bottom ? yBottom : yTop, 0.5f), color);
        }
        return result;
    }

    std::vector<VertexPositionColor> MakeListQuad(
        float x0, float x1, float yBottom, float yTop, const Color& color)
    {
        const Vector3 tl(x0, yTop, 0.5f);
        const Vector3 tr(x1, yTop, 0.5f);
        const Vector3 bl(x0, yBottom, 0.5f);
        const Vector3 br(x1, yBottom, 0.5f);
        return {
            VertexPositionColor(tl, color), VertexPositionColor(tr, color),
            VertexPositionColor(bl, color), VertexPositionColor(br, color),
            VertexPositionColor(bl, color), VertexPositionColor(tr, color),
        };
    }

    Vector3 TriangleCentroid(const std::vector<VertexPositionColor>& strip, int primitive)
    {
        const Vector3& a = strip[static_cast<std::size_t>(primitive)].Position;
        const Vector3& b = strip[static_cast<std::size_t>(primitive + 1)].Position;
        const Vector3& c = strip[static_cast<std::size_t>(primitive + 2)].Position;
        return Vector3((a.X + b.X + c.X) / 3.0f,
                       (a.Y + b.Y + c.Y) / 3.0f,
                       (a.Z + b.Z + c.Z) / 3.0f);
    }

#ifdef CNA_BACKEND_WEBGPU
    struct ErrorScopeResult
    {
        bool complete = false;
        WGPUPopErrorScopeStatus status = WGPUPopErrorScopeStatus_Error;
        WGPUErrorType type = WGPUErrorType_Unknown;
        std::string message;
    };

    void OnErrorScope(WGPUPopErrorScopeStatus status, WGPUErrorType type,
                      WGPUStringView message, void* userdata1, void*)
    {
        auto& result = *static_cast<ErrorScopeResult*>(userdata1);
        result.status = status;
        result.type = type;
        if (message.data != nullptr)
        {
            if (message.length == WGPU_STRLEN) result.message = message.data;
            else result.message.assign(message.data, message.length);
        }
        result.complete = true;
    }

    ErrorScopeResult PopErrorScope(
        CNA::Internal::Backends::WebGPU::WebGPUGraphicsBackend& backend)
    {
        ErrorScopeResult result;
        WGPUPopErrorScopeCallbackInfo callback{};
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.callback = OnErrorScope;
        callback.userdata1 = &result;
        wgpuDevicePopErrorScope(backend.Device(), callback);
        for (int attempt = 0; attempt < 10000 && !result.complete; ++attempt)
            wgpuInstanceProcessEvents(backend.Instance());
        return result;
    }
#endif
}

class TriangleStripWindingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<BasicEffect> effect_;
    bool done_ = false;
    int result_ = 1;
    int passCount_ = 0;
    int totalCount_ = 0;
    int boundaryCount_ = 0;
    int matrixCasesRun_ = 0;

    struct Snapshot
    {
        std::vector<Color> pixels;

        [[nodiscard]] Color AtNdc(float x, float y) const
        {
            const int px = std::clamp(
                static_cast<int>((x * 0.5f + 0.5f) * static_cast<float>(kW - 1)),
                0, kW - 1);
            const int py = std::clamp(
                static_cast<int>((-y * 0.5f + 0.5f) * static_cast<float>(kH - 1)),
                0, kH - 1);
            return pixels[static_cast<std::size_t>(py) * kW + px];
        }
    };

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void boundary(const std::string& label)
    {
        ++boundaryCount_;
        std::printf("[INFO] %s\n", label.c_str());
        std::fflush(stdout);
    }

    static void ResetState(GraphicsDevice& device)
    {
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
    }

    Snapshot Read(RenderTarget2D& target)
    {
        Snapshot result;
        result.pixels.assign(static_cast<std::size_t>(kW) * kH, kPoison);
        target.GetData(result.pixels.data(), 0, static_cast<int>(result.pixels.size()));
        return result;
    }

    void ApplyEffect()
    {
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(Matrix::getIdentityProperty());
        effect_->setProjectionProperty(Matrix::getIdentityProperty());
        effect_->setTextureEnabledProperty(false);
        effect_->VertexColorEnabled = true;
        effect_->setLightingEnabledProperty(false);
        effect_->Apply();
    }

    static void PoisonVertices(std::vector<VertexPositionColor>& vertices)
    {
        std::fill(vertices.begin(), vertices.end(),
                  VertexPositionColor(Vector3(4.0f, 4.0f, 0.5f), kPoison));
    }

    void DrawUserNonIndexed(
        GraphicsDevice& device,
        std::vector<VertexPositionColor> clockwise,
        std::vector<VertexPositionColor> counterClockwise,
        int primitiveCount)
    {
        const VertexPositionColor decoy(Vector3(4.0f, 4.0f, 0.5f), kBlue);
        clockwise.insert(clockwise.begin(), decoy);
        counterClockwise.insert(counterClockwise.begin(), decoy);
        device.DrawUserPrimitives(
            PrimitiveType::TriangleStrip, clockwise.data(), 1, primitiveCount);
        device.DrawUserPrimitives(
            PrimitiveType::TriangleStrip, counterClockwise.data(), 1, primitiveCount);
        PoisonVertices(clockwise);
        PoisonVertices(counterClockwise);
    }

    void DrawBufferNonIndexed(
        GraphicsDevice& device,
        const std::vector<VertexPositionColor>& clockwise,
        const std::vector<VertexPositionColor>& counterClockwise,
        int primitiveCount)
    {
        std::vector<VertexPositionColor> vertices(
            4, VertexPositionColor(Vector3(4.0f, 4.0f, 0.5f), kBlue));
        const int clockwiseStart = static_cast<int>(vertices.size());
        vertices.insert(vertices.end(), clockwise.begin(), clockwise.end());
        vertices.emplace_back(Vector3(4.0f, 4.0f, 0.5f), kBlue);
        const int counterClockwiseStart = static_cast<int>(vertices.size());
        vertices.insert(vertices.end(), counterClockwise.begin(), counterClockwise.end());

        VertexBuffer buffer(
            device, VertexPositionColor::getVertexDeclarationStatic(),
            static_cast<int>(vertices.size()), BufferUsage::None);
        buffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
        device.SetVertexBuffer(&buffer);
        device.DrawPrimitives(
            PrimitiveType::TriangleStrip, clockwiseStart, primitiveCount);
        device.DrawPrimitives(
            PrimitiveType::TriangleStrip, counterClockwiseStart, primitiveCount);
        device.SetVertexBuffer(nullptr);
        buffer.Dispose();
        PoisonVertices(vertices);
    }

    template<typename TIndex>
    void DrawUserIndexed(
        GraphicsDevice& device,
        std::vector<VertexPositionColor> clockwise,
        std::vector<VertexPositionColor> counterClockwise,
        int primitiveCount)
    {
        std::vector<TIndex> indices{static_cast<TIndex>(77), static_cast<TIndex>(77)};
        const int vertexCount = primitiveCount + 2;
        for (int i = 0; i < vertexCount; ++i)
            indices.push_back(static_cast<TIndex>(i));

        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleStrip,
            clockwise.data(), 0, vertexCount,
            indices.data(), 2, primitiveCount);
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleStrip,
            counterClockwise.data(), 0, vertexCount,
            indices.data(), 2, primitiveCount);

        PoisonVertices(clockwise);
        PoisonVertices(counterClockwise);
        std::fill(indices.begin(), indices.end(), static_cast<TIndex>(0));
    }

    template<typename TIndex>
    void DrawBufferIndexed(
        GraphicsDevice& device,
        const std::vector<VertexPositionColor>& clockwise,
        const std::vector<VertexPositionColor>& counterClockwise,
        int primitiveCount)
    {
        const int vertexCount = primitiveCount + 2;
        std::vector<VertexPositionColor> vertices(
            5, VertexPositionColor(Vector3(4.0f, 4.0f, 0.5f), kBlue));
        const int clockwiseBase = static_cast<int>(vertices.size());
        vertices.insert(vertices.end(), clockwise.begin(), clockwise.end());
        vertices.insert(vertices.end(), 3,
                        VertexPositionColor(Vector3(4.0f, 4.0f, 0.5f), kBlue));
        const int counterClockwiseBase = static_cast<int>(vertices.size());
        vertices.insert(vertices.end(), counterClockwise.begin(), counterClockwise.end());

        std::vector<TIndex> indices{
            static_cast<TIndex>(91), static_cast<TIndex>(91), static_cast<TIndex>(91)};
        const int clockwiseStart = static_cast<int>(indices.size());
        for (int i = 0; i < vertexCount; ++i)
            indices.push_back(static_cast<TIndex>(i));
        indices.push_back(static_cast<TIndex>(92));
        indices.push_back(static_cast<TIndex>(92));
        const int counterClockwiseStart = static_cast<int>(indices.size());
        for (int i = 0; i < vertexCount; ++i)
            indices.push_back(static_cast<TIndex>(i));

        VertexBuffer vertexBuffer(
            device, VertexPositionColor::getVertexDeclarationStatic(),
            static_cast<int>(vertices.size()), BufferUsage::None);
        IndexBuffer indexBuffer(
            device,
            std::is_same_v<TIndex, std::uint32_t>
                ? IndexElementSize::ThirtyTwoBits
                : IndexElementSize::SixteenBits,
            static_cast<int>(indices.size()), BufferUsage::None);
        vertexBuffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
        indexBuffer.SetData(indices.data(), static_cast<int>(indices.size()));
        device.SetVertexBuffer(&vertexBuffer);
        device.SetIndexBuffer(&indexBuffer);

        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleStrip,
            clockwiseBase, 0, vertexCount, clockwiseStart, primitiveCount);
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleStrip,
            counterClockwiseBase, 0, vertexCount,
            counterClockwiseStart, primitiveCount);

        device.SetIndexBuffer(nullptr);
        device.SetVertexBuffer(nullptr);
        indexBuffer.Dispose();
        vertexBuffer.Dispose();
        std::fill(indices.begin(), indices.end(), static_cast<TIndex>(0));
        PoisonVertices(vertices);
    }

    void DrawPair(
        GraphicsDevice& device, Path path,
        const std::vector<VertexPositionColor>& clockwise,
        const std::vector<VertexPositionColor>& counterClockwise,
        int primitiveCount)
    {
        switch (path)
        {
            case Path::UserNonIndexed:
                DrawUserNonIndexed(device, clockwise, counterClockwise, primitiveCount);
                return;
            case Path::BufferNonIndexed:
                DrawBufferNonIndexed(device, clockwise, counterClockwise, primitiveCount);
                return;
            case Path::UserIndexed16:
                DrawUserIndexed<std::uint16_t>(
                    device, clockwise, counterClockwise, primitiveCount);
                return;
            case Path::UserIndexed32:
                DrawUserIndexed<std::uint32_t>(
                    device, clockwise, counterClockwise, primitiveCount);
                return;
            case Path::BufferIndexed16:
                DrawBufferIndexed<std::uint16_t>(
                    device, clockwise, counterClockwise, primitiveCount);
                return;
            case Path::BufferIndexed32:
                DrawBufferIndexed<std::uint32_t>(
                    device, clockwise, counterClockwise, primitiveCount);
                return;
        }
    }

    bool RunMatrixCase(
        GraphicsDevice& device, RenderTarget2D& target,
        Path path, int primitiveCount, CullMode cull,
        std::string& failure)
    {
        const auto clockwise = MakeStrip(
            -0.92f, -0.08f, -0.76f, 0.76f,
            primitiveCount, Wind::Clockwise, kRed);
        const auto counterClockwise = MakeStrip(
            0.08f, 0.92f, -0.76f, 0.76f,
            primitiveCount, Wind::CounterClockwise, kGreen);

        device.SetRenderTarget(&target);
        ResetState(device);
        device.Clear(kClear);
        device.setRasterizerStateProperty(CullState(cull));
        ApplyEffect();
        try
        {
            DrawPair(device, path, clockwise, counterClockwise, primitiveCount);
        }
        catch (const std::exception& e)
        {
            failure = e.what();
            device.SetIndexBuffer(nullptr);
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(device);
            return false;
        }
        catch (...)
        {
            failure = "non-std exception";
            device.SetIndexBuffer(nullptr);
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(device);
            return false;
        }

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(device);

        Snapshot snapshot;
        try
        {
            snapshot = Read(target);
        }
        catch (const std::exception& e)
        {
            failure = std::string("readback: ") + e.what();
            return false;
        }

        const std::string prefix = std::string("M ") + PathText(path) + " / " +
            std::to_string(primitiveCount) + " triangles / " + CullText(cull);
        const bool clockwisePresent = ExpectedPresent(Wind::Clockwise, cull);
        const bool counterClockwisePresent = ExpectedPresent(Wind::CounterClockwise, cull);
        for (int primitive = 0; primitive < primitiveCount; ++primitive)
        {
            const Vector3 cwProbe = TriangleCentroid(clockwise, primitive);
            const Vector3 ccwProbe = TriangleCentroid(counterClockwise, primitive);
            const Color cwActual = snapshot.AtNdc(cwProbe.X, cwProbe.Y);
            const Color ccwActual = snapshot.AtNdc(ccwProbe.X, ccwProbe.Y);
            const Color& cwExpected = clockwisePresent ? kRed : kClear;
            const Color& ccwExpected = counterClockwisePresent ? kGreen : kClear;
            check(Same(cwActual, cwExpected),
                  prefix + " clockwise primitive " + std::to_string(primitive) +
                  ((primitive % 2) == 0 ? " (even)" : " (odd)") +
                  " expected " + ColorText(cwExpected) + " got " + ColorText(cwActual));
            check(Same(ccwActual, ccwExpected),
                  prefix + " counter-clockwise primitive " + std::to_string(primitive) +
                  ((primitive % 2) == 0 ? " (even)" : " (odd)") +
                  " expected " + ColorText(ccwExpected) + " got " + ColorText(ccwActual));
        }
        const Color gap = snapshot.AtNdc(0.0f, 0.0f);
        check(Same(gap, kClear), prefix + " leaves the separating gap clear (got " +
              ColorText(gap) + ")");
        ++matrixCasesRun_;
        return true;
    }

    void RunMatrix(GraphicsDevice& device, RenderTarget2D& target)
    {
        constexpr std::array<Path, 6> paths{
            Path::UserNonIndexed,
            Path::BufferNonIndexed,
            Path::UserIndexed16,
            Path::UserIndexed32,
            Path::BufferIndexed16,
            Path::BufferIndexed32,
        };
        constexpr std::array<int, 2> primitiveCounts{3, 4};
        constexpr std::array<CullMode, 3> culls{
            CullMode::None,
            CullMode::CullClockwiseFace,
            CullMode::CullCounterClockwiseFace,
        };

        for (Path path : paths)
        {
            bool supported = true;
            for (int primitiveCount : primitiveCounts)
            {
                for (CullMode cull : culls)
                {
                    if (!supported) continue;
                    std::string failure;
                    supported = RunMatrixCase(
                        device, target, path, primitiveCount, cull, failure);
                    if (!supported)
                    {
                        const std::string label = std::string(PathText(path)) +
                            " is unavailable on " + kBackendName + " (" + failure + ")";
                        if (kEveryPathRequired) check(false, label);
                        else boundary(label + " -- control boundary recorded");
                    }
                }
            }
        }

        if (kEveryPathRequired)
            check(matrixCasesRun_ == 36,
                  "M0 WebGPU executed the complete 6-path x 2-count x 3-cull matrix (" +
                  std::to_string(matrixCasesRun_) + "/36 cases)");
        else if (!kKnownStripBoundary)
            check(matrixCasesRun_ > 0,
                  std::string("M0 ") + kBackendName +
                  " executed at least one supported triangle-strip control path");
    }

    void DrawBufferedIndexedRedStrip(GraphicsDevice& device)
    {
        const auto red = MakeStrip(
            -0.90f, -0.10f, -0.80f, 0.80f, 2, Wind::Clockwise, kRed);
        std::vector<VertexPositionColor> vertices(
            4, VertexPositionColor(Vector3(4.0f, 4.0f, 0.5f), kPoison));
        vertices.insert(vertices.end(), red.begin(), red.end());
        const std::array<std::uint16_t, 7> indices{88, 88, 88, 0, 1, 2, 3};
        VertexBuffer vb(
            device, VertexPositionColor::getVertexDeclarationStatic(),
            static_cast<int>(vertices.size()), BufferUsage::None);
        IndexBuffer ib(
            device, IndexElementSize::SixteenBits,
            static_cast<int>(indices.size()), BufferUsage::None);
        vb.SetData(vertices.data(), static_cast<int>(vertices.size()));
        ib.SetData(indices.data(), static_cast<int>(indices.size()));
        device.SetVertexBuffer(&vb);
        device.SetIndexBuffer(&ib);
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleStrip, 4, 0, 4, 3, 2);
        device.SetIndexBuffer(nullptr);
        device.SetVertexBuffer(nullptr);
        ib.Dispose();
        vb.Dispose();
        PoisonVertices(vertices);
    }

    void RunDeferredOrderAndLifetime(GraphicsDevice& device, RenderTarget2D& target)
    {
        device.SetRenderTarget(&target);
        ResetState(device);
        device.setViewportProperty(Viewport(0, 0, kW, kH));
        device.setScissorRectangleProperty(Rectangle(0, 0, kW, kH));
        device.Clear(kClear);
        ApplyEffect();

        try
        {
            // strip: queued from persistent Uint16 buffers, then both buffers are disposed.
            device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
            DrawBufferedIndexedRedStrip(device);

            // list: overlays the upper-left half.
            auto blue = MakeListQuad(-0.90f, -0.10f, 0.0f, 0.80f, kBlue);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.DrawUserPrimitives(
                PrimitiveType::TriangleList, blue.data(), 0, 2);
            PoisonVertices(blue);

            // strip: counter-clockwise is kept by CullClockwise and overlays lower-right red.
            auto green = MakeStrip(
                -0.50f, -0.10f, -0.80f, 0.0f, 2,
                Wind::CounterClockwise, kGreen);
            device.setRasterizerStateProperty(RasterizerState::CullClockwise);
            device.DrawUserPrimitives(
                PrimitiveType::TriangleStrip, green.data(), 0, 2);
            PoisonVertices(green);

            // list: front-facing and white across the upper-right half.
            auto white = MakeListQuad(0.10f, 0.90f, 0.0f, 0.80f, kWhite);
            device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
            device.DrawUserPrimitives(
                PrimitiveType::TriangleList, white.data(), 0, 2);
            PoisonVertices(white);

            // strip: Uint32 DrawUserIndexed path overlays only the right part of that list.
            auto yellow = MakeStrip(
                0.50f, 0.90f, 0.0f, 0.80f, 2,
                Wind::Clockwise, kYellow);
            std::array<std::uint32_t, 6> yellowIndices{99, 99, 0, 1, 2, 3};
            device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
            device.DrawUserIndexedPrimitives(
                PrimitiveType::TriangleStrip,
                yellow.data(), 0, 4,
                yellowIndices.data(), 2, 2);
            PoisonVertices(yellow);
            yellowIndices.fill(0);

            // No later draw may retroactively observe these live values. If viewport, scissor, or
            // CullMode is replayed from global state, every probe below disappears.
            RasterizerState finalState;
            finalState.setCullModeProperty(CullMode::CullClockwiseFace);
            finalState.setScissorTestEnableProperty(true);
            device.setRasterizerStateProperty(finalState);
            device.setViewportProperty(Viewport(0, 0, 1, 1));
            device.setScissorRectangleProperty(Rectangle(0, 0, 1, 1));
        }
        catch (const std::exception& e)
        {
            device.SetIndexBuffer(nullptr);
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(device);
            if (kEveryPathRequired)
                check(false, std::string("O0 deferred order/lifetime sequence threw: ") + e.what());
            else
                boundary(std::string("O0 deferred order/lifetime control unavailable on ") +
                         kBackendName + " (" + e.what() + ")");
            return;
        }

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(device);
        Snapshot snapshot;
        try
        {
            snapshot = Read(target);
        }
        catch (const std::exception& e)
        {
            check(false, std::string("O0 deferred order/lifetime readback threw: ") + e.what());
            return;
        }

        struct Probe
        {
            float x;
            float y;
            const Color* expected;
            const char* label;
        };
        const std::array<Probe, 6> probes{
            Probe{-0.70f, -0.40f, &kRed, "red strip remains in its unoverlapped region"},
            Probe{-0.70f,  0.40f, &kBlue, "later TriangleList overlays the strip"},
            Probe{-0.30f, -0.40f, &kGreen, "later back-facing strip overlays red"},
            Probe{ 0.30f,  0.40f, &kWhite, "second TriangleList remains left of final strip"},
            Probe{ 0.70f,  0.40f, &kYellow, "final Uint32 strip overlays the list"},
            Probe{ 0.50f, -0.50f, &kClear, "never-covered control remains clear"},
        };
        for (const Probe& probe : probes)
        {
            const Color actual = snapshot.AtNdc(probe.x, probe.y);
            check(Same(actual, *probe.expected),
                  std::string("O1 ") + probe.label + " expected " +
                  ColorText(*probe.expected) + " got " + ColorText(actual));
        }
    }

    void RunDisposalCycles(GraphicsDevice& device)
    {
        for (int cycle = 0; cycle < 4; ++cycle)
        {
            RenderTarget2D target(
                device, kW, kH, false, SurfaceFormat::Color,
                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(&target);
            ResetState(device);
            device.Clear(kClear);
            const bool clockwise = (cycle % 2) == 0;
            device.setRasterizerStateProperty(
                clockwise ? RasterizerState::CullCounterClockwise
                          : RasterizerState::CullClockwise);
            ApplyEffect();
            auto strip = MakeStrip(
                -0.80f, 0.80f, -0.70f, 0.70f,
                3 + (cycle % 2),
                clockwise ? Wind::Clockwise : Wind::CounterClockwise,
                kYellow);
            try
            {
                device.DrawUserPrimitives(
                    PrimitiveType::TriangleStrip,
                    strip.data(), 0, 3 + (cycle % 2));
                PoisonVertices(strip);
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(device);
                const Snapshot snapshot = Read(target);
                const Color actual = snapshot.AtNdc(-0.55f, 0.0f);
                check(Same(actual, kYellow),
                      "D cycle " + std::to_string(cycle) +
                      " renders after source mutation and before explicit target disposal (got " +
                      ColorText(actual) + ")");
            }
            catch (const std::exception& e)
            {
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(device);
                if (kEveryPathRequired)
                    check(false, "D cycle " + std::to_string(cycle) + " threw: " + e.what());
                else
                    boundary("D cycle unavailable on " + std::string(kBackendName) +
                             " (" + e.what() + ")");
                target.Dispose();
                return;
            }
            target.Dispose();
        }
    }

    void Initialize() override
    {
        auto& device = getGraphicsDeviceProperty();
        if (kRasterizes && device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        {
            effect_ = std::make_unique<BasicEffect>(device);
        }
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();

        std::printf("[INFO] REMED-GFX-183 triangle-strip winding on %s\n", kBackendName);
        std::fflush(stdout);

        if (!kRasterizes ||
            !device.SupportsCapability(CNA::GraphicsCapability::ThreeD) ||
            kKnownStripBoundary)
        {
            check(kKnownStripBoundary || !kRasterizes,
                  std::string("B0 ") + kBackendName +
                  " declares its non-rasterizing/TriangleList-only boundary");
            result_ = passCount_ == totalCount_ ? 0 : 1;
            Exit();
            return;
        }

#ifdef CNA_BACKEND_WEBGPU
        auto& backend = static_cast<
            CNA::Internal::Backends::WebGPU::WebGPUGraphicsBackend&>(device.GetBackend());
        const std::size_t uncapturedBefore = backend.GetUncapturedErrorCountEXT();
        wgpuDevicePushErrorScope(backend.Device(), WGPUErrorFilter_OutOfMemory);
        wgpuDevicePushErrorScope(backend.Device(), WGPUErrorFilter_Validation);
#endif

        try
        {
            RenderTarget2D target(
                device, kW, kH, false, SurfaceFormat::Color,
                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            RunMatrix(device, target);
            RunDeferredOrderAndLifetime(device, target);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            target.Dispose();
            RunDisposalCycles(device);
        }
        catch (const std::exception& e)
        {
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            check(false, std::string("T0 unhandled fixture exception: ") + e.what());
        }

#ifdef CNA_BACKEND_WEBGPU
        const ErrorScopeResult validation = PopErrorScope(backend);
        const ErrorScopeResult outOfMemory = PopErrorScope(backend);
        check(validation.complete &&
              validation.status == WGPUPopErrorScopeStatus_Success &&
              validation.type == WGPUErrorType_NoError && validation.message.empty(),
              "V1 WebGPU validation error scope is clean" +
              (validation.message.empty() ? std::string() : ": " + validation.message));
        check(outOfMemory.complete &&
              outOfMemory.status == WGPUPopErrorScopeStatus_Success &&
              outOfMemory.type == WGPUErrorType_NoError && outOfMemory.message.empty(),
              "V2 WebGPU out-of-memory error scope is clean" +
              (outOfMemory.message.empty() ? std::string() : ": " + outOfMemory.message));
        check(backend.GetUncapturedErrorCountEXT() == uncapturedBefore,
              "V3 WebGPU emitted no uncaptured validation/device errors");
#endif

        device.SetIndexBuffer(nullptr);
        device.SetVertexBuffer(nullptr);
        ResetState(device);
        if (effect_)
        {
            effect_->Dispose();
            effect_.reset();
        }

        std::printf("[INFO] %s: %d/%d checks passed (%d boundaries, %d matrix cases)\n",
                    kBackendName, passCount_, totalCount_, boundaryCount_, matrixCasesRun_);
        std::fflush(stdout);
        result_ = passCount_ == totalCount_ ? 0 : 1;
        Exit();
    }

public:
    TriangleStripWindingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kW);
        gdm_->setPreferredBackBufferHeightProperty(kH);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    TriangleStripWindingTest test;
    test.Run();
    return test.Result();
}
