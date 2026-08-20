// SPDX-License-Identifier: MS-PL
// plans/plan_fna3d.md FNA3D-28/29: the two buffer claims FNA3D-9 made without a behaviour test behind
// them.
//
// FNA3D-28 -- `MultiStreamVertexInput` is reported **true** by this renderer, and until now the
// only thing standing behind that answer was `SupportsCapability(...) == true` in the capabilities
// test. FNA3D genuinely takes an array of per-stream bindings, each with its own
// `FNA3D_VertexDeclaration`, so the claim is testable: put POSITION in one vertex buffer and COLOR
// in a second, bind both, and check the pixels. A renderer that silently used only stream 0 would
// render the quad in whatever colour uninitialised stream-1 data happened to give -- most likely
// black -- and every capability check would still pass.
//
// FNA3D-29 -- `SetDataOptions` is forwarded to FNA3D per upload, with `NoOverwrite` downgraded to
// `None` on a driver that reports no support for it (FNA3D-22). XNA's semantics are that all three
// options must leave the caller's data readable afterwards; `Discard` and `NoOverwrite` are
// promises about *aliasing*, not permission to lose bytes. These checks pin exactly that.
//
// Note on what is deliberately NOT here: CNA's buffer surface has no destination-offset `SetData`
// overload at all -- `VertexBuffer`'s own header states the destination write always begins at the
// buffer's start, and `startIndex` only selects where reading from the source begins. A mid-buffer
// partial update is therefore not a route any renderer can be tested against, so Check D pins the
// source-window selection that does exist rather than an offset that does not.
//
// Check A -- POSITION from stream 0 and COLOR from stream 1 produce the right pixels.
// Check B -- swapping which buffer holds which attribute still works, so the streams are really
//   independent rather than one layout being hard-coded.
// Check C -- every SetDataOptions value round-trips the data it wrote.
// Check D -- `startIndex` selects which SOURCE elements are uploaded.
// Check E -- consecutive updates to one buffer are all observable; the last one renders.
// Check F -- index buffers round-trip through both the 16-bit and 32-bit routes.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // Deliberately plain PODs: `Color` has a polymorphic IPackedVector base, so a struct holding
    // one carries a vtable pointer that SetDataRaw would copy verbatim into the GPU buffer.
    struct PositionOnly
    {
        float x, y, z;
    };

    struct ColorOnly
    {
        std::uint32_t rgba;   // XNA's own AABBGGRR packing
    };

    constexpr std::uint32_t Pack(const Color& c)
    {
        return static_cast<std::uint32_t>(c.getRProperty())
             | (static_cast<std::uint32_t>(c.getGProperty()) << 8)
             | (static_cast<std::uint32_t>(c.getBProperty()) << 16)
             | (static_cast<std::uint32_t>(c.getAProperty()) << 24);
    }

    std::string Describe(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    /// A full-viewport quad, positions only.
    std::vector<PositionOnly> QuadPositions()
    {
        return {
            { -1.0f,  1.0f, 0.0f }, { -1.0f, -1.0f, 0.0f }, {  1.0f, -1.0f, 0.0f },
            { -1.0f,  1.0f, 0.0f }, {  1.0f, -1.0f, 0.0f }, {  1.0f,  1.0f, 0.0f },
        };
    }

    std::vector<ColorOnly> QuadColors(const Color& color)
    {
        return std::vector<ColorOnly>(6, ColorOnly{ Pack(color) });
    }

    /// A full-viewport quad in the typed XNA vertex struct, for the single-stream option checks.
    std::vector<VertexPositionColor> TypedQuad(const Color& color)
    {
        return {
            { Vector3(-1.0f,  1.0f, 0.0f), color }, { Vector3(-1.0f, -1.0f, 0.0f), color },
            { Vector3( 1.0f, -1.0f, 0.0f), color }, { Vector3(-1.0f,  1.0f, 0.0f), color },
            { Vector3( 1.0f, -1.0f, 0.0f), color }, { Vector3( 1.0f,  1.0f, 0.0f), color },
        };
    }
}

class Fna3dBuffersTest : public CNA::Examples::PixelTestGame
{
public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        effect_ = std::make_unique<BasicEffect>(device);

        MultiStreamChecks(device);
        SetDataOptionChecks(device);
        IndexBufferChecks(device);
    }

private:
    std::unique_ptr<BasicEffect> effect_;

    // ---- FNA3D-28 -------------------------------------------------------------------------
    void MultiStreamChecks(GraphicsDevice& device)
    {
        // One declaration per stream, each describing only what that stream carries. This is the
        // shape FNA3D binds natively: a declaration per binding, not one interleaved layout.
        VertexDeclaration positionDecl(
            static_cast<int>(sizeof(PositionOnly)),
            { VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0) });
        VertexDeclaration colorDecl(
            static_cast<int>(sizeof(ColorOnly)),
            { VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0) });

        const std::vector<PositionOnly> positions = QuadPositions();

        // Check A -- POSITION in stream 0, COLOR in stream 1.
        {
            const std::vector<ColorOnly> colors = QuadColors(Color(220, 60, 40, 255));
            VertexBuffer positionVb(device, positionDecl, 6, BufferUsage::WriteOnly);
            VertexBuffer colorVb(device, colorDecl, 6, BufferUsage::WriteOnly);
            positionVb.SetDataRaw(positions.data(), 6, static_cast<int>(sizeof(PositionOnly)));
            colorVb.SetDataRaw(colors.data(), 6, static_cast<int>(sizeof(ColorOnly)));

            DrawTwoStreamQuad(device, positionVb, colorVb);
            ExpectPixel("POSITION from stream 0 and COLOR from stream 1 reach the shader",
                        CentrePixel(device), Color(220, 60, 40, 255), 4);
        }

        // Check B -- the same two streams in the other binding order. If a layout were hard-coded
        // to "stream 0 carries everything", one of these two orders would have to fail.
        {
            const std::vector<ColorOnly> colors = QuadColors(Color(40, 200, 120, 255));
            VertexBuffer positionVb(device, positionDecl, 6, BufferUsage::WriteOnly);
            VertexBuffer colorVb(device, colorDecl, 6, BufferUsage::WriteOnly);
            positionVb.SetDataRaw(positions.data(), 6, static_cast<int>(sizeof(PositionOnly)));
            colorVb.SetDataRaw(colors.data(), 6, static_cast<int>(sizeof(ColorOnly)));

            device.Clear(Color(0, 0, 0, 255));
            std::vector<VertexBufferBinding> bindings = {
                VertexBufferBinding(&colorVb, 0, 0),
                VertexBufferBinding(&positionVb, 0, 0),
            };
            device.SetVertexBuffers(bindings);
            DrawBoundStreams(device);
            ExpectPixel("the two streams work in the reverse binding order too",
                        CentrePixel(device), Color(40, 200, 120, 255), 4);
        }
    }

    // ---- FNA3D-29 -------------------------------------------------------------------------
    void SetDataOptionChecks(GraphicsDevice& device)
    {
        // SetDataOptions reaches a buffer through XNA's DynamicVertexBuffer, which is the public
        // route the option exists for; VertexBuffer's own SetDataWithOptions is protected.
        const VertexDeclaration& decl = VertexPositionColor::getVertexDeclarationStatic();

        // Check C -- every option must leave the written data readable. Discard and NoOverwrite
        // are promises about aliasing with in-flight GPU reads, never licence to drop bytes; and
        // on a driver without FNA3D_SupportsNoOverwrite this renderer downgrades NoOverwrite to
        // None (FNA3D-22), which must be invisible here.
        struct OptionCase { SetDataOptions option; const char* name; Color color; };
        const OptionCase cases[] = {
            { SetDataOptions::None,        "None",        Color(255, 40, 40, 255) },
            { SetDataOptions::Discard,     "Discard",     Color(40, 255, 40, 255) },
            { SetDataOptions::NoOverwrite, "NoOverwrite", Color(40, 40, 255, 255) },
        };

        for (const OptionCase& entry : cases)
        {
            DynamicVertexBuffer vb(device, decl, 6, BufferUsage::WriteOnly);
            const std::vector<VertexPositionColor> quad = TypedQuad(entry.color);
            vb.SetData(quad.data(), 0, 6, entry.option);

            DrawSingleStreamQuad(device, vb);
            ExpectPixel((std::string("SetDataOptions::") + entry.name +
                         " leaves the uploaded data readable").c_str(),
                        CentrePixel(device), entry.color, 4);
        }

        // Check D -- `startIndex` selects the SOURCE window. CNA's buffer surface has no
        // destination-offset overload at all (VertexBuffer's own header states the destination
        // write always begins at the buffer's start), so that -- not a mid-buffer patch -- is what
        // this API offers and what is worth pinning: a renderer that ignored startIndex would
        // upload the first three vertices instead of the ones named.
        {
            DynamicVertexBuffer vb(device, decl, 3, BufferUsage::WriteOnly);
            std::vector<VertexPositionColor> source = TypedQuad(Color(200, 200, 0, 255));
            // Elements 3..5 are a different colour, and are also a complete triangle on their own.
            for (int i = 3; i < 6; ++i)
            {
                source[static_cast<std::size_t>(i)] =
                    VertexPositionColor(source[static_cast<std::size_t>(i)].Position,
                                        Color(0, 120, 220, 255));
            }
            vb.SetData(source.data(), 3, 3, SetDataOptions::None);

            device.Clear(Color(0, 0, 0, 255));
            device.SetVertexBuffer(&vb);
            effect_->VertexColorEnabled = true;
            effect_->setTextureEnabledProperty(false);
            effect_->Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

            const int width = device.getViewportProperty().getWidthProperty();
            const int height = device.getViewportProperty().getHeightProperty();
            // Vertices 3..5 are the upper-right triangle of the quad.
            ExpectPixel("startIndex selects which source elements are uploaded",
                        Rectangle((width * 4) / 5, height / 5, 1, 1), Color(0, 120, 220, 255), 6);
        }

        // Check E -- consecutive uploads to one buffer are each accepted; the last one renders.
        {
            DynamicVertexBuffer vb(device, decl, 6, BufferUsage::WriteOnly);
            for (const Color& step : { Color(10, 10, 10, 255), Color(90, 90, 90, 255),
                                       Color(230, 120, 30, 255) })
            {
                const std::vector<VertexPositionColor> quad = TypedQuad(step);
                vb.SetData(quad.data(), 0, 6, SetDataOptions::Discard);
            }
            DrawSingleStreamQuad(device, vb);
            ExpectPixel("the last of several consecutive uploads is the one that renders",
                        CentrePixel(device), Color(230, 120, 30, 255), 4);
        }
    }

    // ---- FNA3D-29, index side -------------------------------------------------------------
    void IndexBufferChecks(GraphicsDevice& device)
    {
        VertexDeclaration positionDecl(
            static_cast<int>(sizeof(PositionOnly)),
            { VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0) });
        VertexDeclaration colorDecl(
            static_cast<int>(sizeof(ColorOnly)),
            { VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0) });

        const std::vector<PositionOnly> positions = {
            { -1.0f,  1.0f, 0.0f }, { -1.0f, -1.0f, 0.0f },
            {  1.0f, -1.0f, 0.0f }, {  1.0f,  1.0f, 0.0f },
        };
        const std::vector<ColorOnly> colors(4, ColorOnly{ Pack(Color(120, 60, 200, 255)) });

        VertexBuffer positionVb(device, positionDecl, 4, BufferUsage::WriteOnly);
        VertexBuffer colorVb(device, colorDecl, 4, BufferUsage::WriteOnly);
        positionVb.SetDataRaw(positions.data(), 4, static_cast<int>(sizeof(PositionOnly)));
        colorVb.SetDataRaw(colors.data(), 4, static_cast<int>(sizeof(ColorOnly)));

        // Check F -- both index widths, each written through SetData with an explicit option.
        {
            const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
            DynamicIndexBuffer ib(device, IndexElementSize::SixteenBits, 6,
                                  BufferUsage::WriteOnly);
            ib.SetData(indices, 0, 6, SetDataOptions::Discard);
            DrawIndexedTwoStreamQuad(device, positionVb, colorVb, ib);
            ExpectPixel("a 16-bit index buffer round-trips and draws",
                        CentrePixel(device), Color(120, 60, 200, 255), 4);
        }
        {
            const std::uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
            DynamicIndexBuffer ib(device, IndexElementSize::ThirtyTwoBits, 6,
                                  BufferUsage::WriteOnly);
            ib.SetData(indices, 0, 6, SetDataOptions::NoOverwrite);
            DrawIndexedTwoStreamQuad(device, positionVb, colorVb, ib);
            ExpectPixel("a 32-bit index buffer round-trips and draws",
                        CentrePixel(device), Color(120, 60, 200, 255), 4);
        }
    }

    // ---- helpers --------------------------------------------------------------------------
    Rectangle CentrePixel(GraphicsDevice& device) const
    {
        return Rectangle(device.getViewportProperty().getWidthProperty() / 2,
                         device.getViewportProperty().getHeightProperty() / 2, 1, 1);
    }

    void DrawTwoStreamQuad(GraphicsDevice& device, VertexBuffer& positions, VertexBuffer& colors)
    {
        device.Clear(Color(0, 0, 0, 255));
        std::vector<VertexBufferBinding> bindings = {
            VertexBufferBinding(&positions, 0, 0),
            VertexBufferBinding(&colors, 0, 0),
        };
        device.SetVertexBuffers(bindings);
        DrawBoundStreams(device);
    }

    void DrawSingleStreamQuad(GraphicsDevice& device, VertexBuffer& vb)
    {
        device.Clear(Color(0, 0, 0, 255));
        device.SetVertexBuffer(&vb);
        DrawBoundStreams(device);
    }

    void DrawBoundStreams(GraphicsDevice& device)
    {
        effect_->VertexColorEnabled = true;
        effect_->setTextureEnabledProperty(false);
        effect_->Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    }

    void DrawIndexedTwoStreamQuad(GraphicsDevice& device, VertexBuffer& positions,
                                  VertexBuffer& colors, IndexBuffer& indices)
    {
        device.Clear(Color(0, 0, 0, 255));
        std::vector<VertexBufferBinding> bindings = {
            VertexBufferBinding(&positions, 0, 0),
            VertexBufferBinding(&colors, 0, 0),
        };
        device.SetVertexBuffers(bindings);
        device.setIndicesProperty(&indices);
        effect_->VertexColorEnabled = true;
        effect_->setTextureEnabledProperty(false);
        effect_->Apply();
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Fna3dBuffersTest>();
}
