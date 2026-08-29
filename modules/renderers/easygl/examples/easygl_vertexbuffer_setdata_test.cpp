// SPDX-License-Identifier: MS-PL
// Task 232: VertexBuffer::SetData with startIndex and elementCount < capacity.
//
// Tests:
//   1. SetData(data, startIndex=3, elementCount=6) — uploads a slice of a 9-element array.
//   2. SetData(data, startIndex=0, elementCount=6) — partial fill of a 12-vertex buffer.
//   3. SetDataRaw with explicit stride — verifies CNAEXT raw upload path.
//   4. DynamicVertexBuffer::SetData with SetDataOptions — options accepted without crash.
//   5. getBufferUsageProperty / getVertexCountProperty / getVertexDeclarationProperty.
//   6. IndexBuffer getBufferUsageProperty / getIndexElementSizeProperty / getIndexCountProperty.
//   7. IndexBuffer::SetData(data, startIndex, elementCount) — 16-bit and 32-bit variants.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static VertexDeclaration makePosColorDecl()
{
    return VertexDeclaration(16, {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
    });
}

static std::vector<VertexPositionColor> makeVerts(int count)
{
    std::vector<VertexPositionColor> v(count);
    for (int i = 0; i < count; ++i) {
        v[i].Position = Vector3(static_cast<float>(i), 0.f, 0.f);
        v[i].Color    = Color(i * 20, 0, 0, 255);
    }
    return v;
}

class VbSetDataTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    template<class T>
    void checkEq(T actual, T expected, const char* label)
    {
        bool ok = (actual == expected);
        if (!ok)
            std::printf("[FAIL] %s (expected %d, got %d)\n", label,
                        static_cast<int>(expected), static_cast<int>(actual));
        else
            std::printf("[PASS] %s\n", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Initialize() override
    {
        auto& dev = getGraphicsDeviceProperty();

        // 1. SetData(data, startIndex=3, elementCount=6) from 9-element source array
        {
            auto decl = makePosColorDecl();
            VertexBuffer vb(dev, decl, 9, BufferUsage::None);
            auto verts = makeVerts(9);
            vb.SetData(verts.data(), 3, 6);  // uploads verts[3..8] to GPU offset 0
            checkEq(vb.getVertexCountProperty(), 9,
                    "SetData(startIndex=3, count=6): capacity stays 9");
        }

        // 2. SetData(data, startIndex=0, elementCount=6) — partial fill of 12-vertex buffer
        {
            auto decl = makePosColorDecl();
            VertexBuffer vb(dev, decl, 12, BufferUsage::None);
            auto verts = makeVerts(12);
            vb.SetData(verts.data(), 0, 6);  // upload only first 6 of 12 slots
            checkEq(vb.getVertexCountProperty(), 12,
                    "SetData(startIndex=0, count=6): capacity stays 12");
        }

        // 3. CNAEXT ctor + SetDataRaw with explicit stride
        {
            VertexBuffer vb(dev, 6);
            struct Compact { float x, y, z; std::uint8_t r, g, b, a; };
            static_assert(sizeof(Compact) == 16, "Compact must be 16 bytes");
            Compact raw[6] = {};
            for (int i = 0; i < 6; ++i) {
                raw[i] = { static_cast<float>(i), 0.f, 0.f,
                           255, static_cast<std::uint8_t>(i * 40), 0, 255 };
            }
            vb.SetDataRaw(raw, 6, sizeof(Compact));
            checkEq(vb.getVertexCountProperty(), 6,
                    "SetDataRaw: capacity unchanged at 6");
        }

        // 4. DynamicVertexBuffer::SetData with SetDataOptions
        {
            auto decl = makePosColorDecl();
            DynamicVertexBuffer dvb(dev, decl, 6, BufferUsage::None);
            auto verts = makeVerts(9);
            dvb.SetData(verts.data(), 3, 6, SetDataOptions::Discard);
            checkEq(dvb.getVertexCountProperty(), 6,
                    "DynamicVertexBuffer SetData+Discard: capacity stays 6");
            dvb.SetData(verts.data(), 0, 6, SetDataOptions::NoOverwrite);
            checkEq(dvb.getVertexCountProperty(), 6,
                    "DynamicVertexBuffer SetData+NoOverwrite: capacity stays 6");
        }

        // 5. VertexBuffer property getters from full constructor
        {
            auto decl = makePosColorDecl();
            VertexBuffer vb(dev, decl, 4, BufferUsage::WriteOnly);
            checkEq(static_cast<int>(vb.getBufferUsageProperty()),
                    static_cast<int>(BufferUsage::WriteOnly),
                    "getBufferUsageProperty() == WriteOnly");
            checkEq(vb.getVertexCountProperty(), 4,
                    "getVertexCountProperty() == 4");
            checkEq(vb.getVertexDeclarationProperty().getVertexStrideProperty(), 16,
                    "getVertexDeclarationProperty().getVertexStrideProperty() == 16");
        }

        // 6. IndexBuffer property getters
        {
            IndexBuffer ib(dev, IndexElementSize::ThirtyTwoBits, 100, BufferUsage::WriteOnly);
            checkEq(static_cast<int>(ib.getBufferUsageProperty()),
                    static_cast<int>(BufferUsage::WriteOnly),
                    "IndexBuffer: getBufferUsageProperty() == WriteOnly");
            checkEq(static_cast<int>(ib.getIndexElementSizeProperty()),
                    static_cast<int>(IndexElementSize::ThirtyTwoBits),
                    "IndexBuffer: getIndexElementSizeProperty() == ThirtyTwoBits");
            checkEq(ib.getIndexCountProperty(), 100,
                    "IndexBuffer: getIndexCountProperty() == 100");
        }

        // 7a. IndexBuffer::SetData(uint16_t*, startIndex, elementCount)
        {
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 9, BufferUsage::None);
            std::uint16_t idx[9] = {0,1,2, 3,4,5, 6,7,8};
            ib.SetData(idx, 3, 6);  // upload idx[3..8]
            checkEq(ib.getIndexCountProperty(), 9,
                    "IndexBuffer SetData16(startIndex=3, count=6): capacity stays 9");
        }

        // 7b. IndexBuffer::SetData(uint32_t*, startIndex, elementCount)
        {
            IndexBuffer ib(dev, IndexElementSize::ThirtyTwoBits, 9, BufferUsage::None);
            std::uint32_t idx[9] = {0,1,2, 3,4,5, 6,7,8};
            ib.SetData(idx, 3, 6);  // upload idx[3..8]
            checkEq(ib.getIndexCountProperty(), 9,
                    "IndexBuffer SetData32(startIndex=3, count=6): capacity stays 9");
        }

        // 8. DynamicIndexBuffer::SetData with SetDataOptions
        {
            DynamicIndexBuffer dib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
            std::uint16_t idx[9] = {0,1,2, 3,4,5, 6,7,8};
            dib.SetData(idx, 3, 6, SetDataOptions::Discard);
            checkEq(dib.getIndexCountProperty(), 6,
                    "DynamicIndexBuffer SetData+Discard: capacity stays 6");
        }

        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VbSetDataTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VbSetDataTest g;
    g.Run();
    return g.getResult();
}
