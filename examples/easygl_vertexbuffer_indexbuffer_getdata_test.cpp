// SPDX-License-Identifier: MS-PL
// Task 930: Verify that VertexBuffer/IndexBuffer::GetData() round-trips exactly what was
// previously uploaded via SetData(), for every typed vertex overload (VertexPositionColor,
// VertexPositionColorTexture, VertexPositionNormalTexture, VertexPositionTexture, NOXNA
// VertexPositionNormalTextureSkinned) and both 16-bit/32-bit IndexBuffer widths. Also verifies
// the WriteOnly-buffer NotSupportedException guard and the out-of-range ArgumentOutOfRangeException
// guard on both buffer kinds.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class VertexIndexGetDataTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    template<class Ex, class Fn>
    bool throwsType(Fn&& fn)
    {
        try { fn(); return false; }
        catch (const Ex&) { return true; }
        catch (...) { return false; }
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        // --- VertexPositionColor ---
        {
            VertexPositionColor src[3] = {
                VertexPositionColor(Vector3(1, 2, 3), Color(10, 20, 30, 40)),
                VertexPositionColor(Vector3(4, 5, 6), Color(50, 60, 70, 80)),
                VertexPositionColor(Vector3(7, 8, 9), Color(90, 100, 110, 120)),
            };
            VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vb.SetData(src, 3);

            VertexPositionColor dst[3]{};
            vb.GetData(dst, 3);
            check(dst[0] == src[0] && dst[1] == src[1] && dst[2] == src[2],
                  "VertexPositionColor: GetData(count) round-trips SetData");

            VertexPositionColor dstSlice[2]{};
            vb.GetData(dstSlice, 1, 2);
            check(dstSlice[0] == src[1] && dstSlice[1] == src[2],
                  "VertexPositionColor: GetData(startIndex, count) round-trips slice");
        }

        // --- VertexPositionColorTexture ---
        {
            VertexPositionColorTexture src[3] = {
                VertexPositionColorTexture(Vector3(1, 2, 3), Color(10, 20, 30, 40), Vector2(0.1f, 0.2f)),
                VertexPositionColorTexture(Vector3(4, 5, 6), Color(50, 60, 70, 80), Vector2(0.3f, 0.4f)),
                VertexPositionColorTexture(Vector3(7, 8, 9), Color(90, 100, 110, 120), Vector2(0.5f, 0.6f)),
            };
            VertexBuffer vb(dev, VertexPositionColorTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vb.SetData(src, 3);

            const VertexPositionColorTexture zero(Vector3(), Color(0, 0, 0, 0), Vector2());
            VertexPositionColorTexture dst[3] = {zero, zero, zero};
            vb.GetData(dst, 3);
            check(dst[0] == src[0] && dst[1] == src[1] && dst[2] == src[2],
                  "VertexPositionColorTexture: GetData(count) round-trips SetData");

            VertexPositionColorTexture dstSlice[2] = {zero, zero};
            vb.GetData(dstSlice, 1, 2);
            check(dstSlice[0] == src[1] && dstSlice[1] == src[2],
                  "VertexPositionColorTexture: GetData(startIndex, count) round-trips slice");
        }

        // --- VertexPositionNormalTexture ---
        {
            VertexPositionNormalTexture src[3] = {
                VertexPositionNormalTexture(Vector3(1, 2, 3), Vector3(0, 1, 0), Vector2(0.1f, 0.2f)),
                VertexPositionNormalTexture(Vector3(4, 5, 6), Vector3(0, 0, 1), Vector2(0.3f, 0.4f)),
                VertexPositionNormalTexture(Vector3(7, 8, 9), Vector3(1, 0, 0), Vector2(0.5f, 0.6f)),
            };
            VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vb.SetData(src, 3);

            VertexPositionNormalTexture dst[3]{};
            vb.GetData(dst, 3);
            check(dst[0] == src[0] && dst[1] == src[1] && dst[2] == src[2],
                  "VertexPositionNormalTexture: GetData(count) round-trips SetData");

            VertexPositionNormalTexture dstSlice[2]{};
            vb.GetData(dstSlice, 1, 2);
            check(dstSlice[0] == src[1] && dstSlice[1] == src[2],
                  "VertexPositionNormalTexture: GetData(startIndex, count) round-trips slice");
        }

        // --- VertexPositionTexture ---
        {
            VertexPositionTexture src[3] = {
                VertexPositionTexture(Vector3(1, 2, 3), Vector2(0.1f, 0.2f)),
                VertexPositionTexture(Vector3(4, 5, 6), Vector2(0.3f, 0.4f)),
                VertexPositionTexture(Vector3(7, 8, 9), Vector2(0.5f, 0.6f)),
            };
            VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vb.SetData(src, 3);

            VertexPositionTexture dst[3]{};
            vb.GetData(dst, 3);
            check(dst[0] == src[0] && dst[1] == src[1] && dst[2] == src[2],
                  "VertexPositionTexture: GetData(count) round-trips SetData");

            VertexPositionTexture dstSlice[2]{};
            vb.GetData(dstSlice, 1, 2);
            check(dstSlice[0] == src[1] && dstSlice[1] == src[2],
                  "VertexPositionTexture: GetData(startIndex, count) round-trips slice");
        }

        // --- VertexPositionNormalTextureSkinned (NOXNA) ---
        {
            VertexPositionNormalTextureSkinned src[3] = {
                VertexPositionNormalTextureSkinned(Vector3(1, 2, 3), Vector3(0, 1, 0), Vector2(0.1f, 0.2f),
                                                    Vector4(1, 0, 0, 0), std::array<std::uint8_t, 4>{0, 1, 2, 3}),
                VertexPositionNormalTextureSkinned(Vector3(4, 5, 6), Vector3(0, 0, 1), Vector2(0.3f, 0.4f),
                                                    Vector4(0.5f, 0.5f, 0, 0), std::array<std::uint8_t, 4>{1, 2, 3, 4}),
                VertexPositionNormalTextureSkinned(Vector3(7, 8, 9), Vector3(1, 0, 0), Vector2(0.5f, 0.6f),
                                                    Vector4(0.25f, 0.25f, 0.25f, 0.25f), std::array<std::uint8_t, 4>{2, 3, 4, 5}),
            };
            VertexBuffer vb(dev, VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vb.SetData(src, 3);

            VertexPositionNormalTextureSkinned dst[3]{};
            vb.GetData(dst, 3);
            check(dst[0] == src[0] && dst[1] == src[1] && dst[2] == src[2],
                  "VertexPositionNormalTextureSkinned: GetData(count) round-trips SetData");

            VertexPositionNormalTextureSkinned dstSlice[2]{};
            vb.GetData(dstSlice, 1, 2);
            check(dstSlice[0] == src[1] && dstSlice[1] == src[2],
                  "VertexPositionNormalTextureSkinned: GetData(startIndex, count) round-trips slice");
        }

        // --- VertexBuffer guard checks ---
        {
            VertexPositionColor src[3] = {
                VertexPositionColor(Vector3(1, 2, 3), Color(10, 20, 30, 40)),
                VertexPositionColor(Vector3(4, 5, 6), Color(50, 60, 70, 80)),
                VertexPositionColor(Vector3(7, 8, 9), Color(90, 100, 110, 120)),
            };

            VertexBuffer vbWriteOnly(dev, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::WriteOnly);
            vbWriteOnly.SetData(src, 3);
            VertexPositionColor dst[3]{};
            check(throwsType<System::NotSupportedException>([&]{ vbWriteOnly.GetData(dst, 3); }),
                  "VertexBuffer: GetData on WriteOnly buffer throws NotSupportedException");

            VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vb.SetData(src, 3);
            VertexPositionColor dstOver[5]{};
            check(throwsType<System::ArgumentOutOfRangeException>([&]{ vb.GetData(dstOver, 0, 5); }),
                  "VertexBuffer: out-of-range GetData throws ArgumentOutOfRangeException");
        }

        // --- IndexBuffer: 16-bit ---
        {
            std::uint16_t src[4] = {5, 10, 15, 20};
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 4, BufferUsage::None);
            ib.SetData(src, 4);

            std::uint16_t dst[4]{};
            ib.GetData(dst, 4);
            check(dst[0] == 5 && dst[1] == 10 && dst[2] == 15 && dst[3] == 20,
                  "IndexBuffer u16: GetData(count) round-trips SetData");

            std::uint16_t dstSlice[2]{};
            ib.GetData(dstSlice, 1, 2);
            check(dstSlice[0] == 10 && dstSlice[1] == 15,
                  "IndexBuffer u16: GetData(startIndex, count) round-trips slice");

            IndexBuffer ibWriteOnly(dev, IndexElementSize::SixteenBits, 4, BufferUsage::WriteOnly);
            ibWriteOnly.SetData(src, 4);
            std::uint16_t dstW[4]{};
            check(throwsType<System::NotSupportedException>([&]{ ibWriteOnly.GetData(dstW, 4); }),
                  "IndexBuffer u16: GetData on WriteOnly buffer throws NotSupportedException");

            std::uint16_t dstOver[6]{};
            check(throwsType<System::ArgumentOutOfRangeException>([&]{ ib.GetData(dstOver, 0, 6); }),
                  "IndexBuffer u16: out-of-range GetData throws ArgumentOutOfRangeException");
        }

        // --- IndexBuffer: 32-bit ---
        {
            std::uint32_t src[4] = {500, 1000, 1500, 2000};
            IndexBuffer ib(dev, IndexElementSize::ThirtyTwoBits, 4, BufferUsage::None);
            ib.SetData(src, 4);

            std::uint32_t dst[4]{};
            ib.GetData(dst, 4);
            check(dst[0] == 500 && dst[1] == 1000 && dst[2] == 1500 && dst[3] == 2000,
                  "IndexBuffer u32: GetData(count) round-trips SetData");

            std::uint32_t dstSlice[2]{};
            ib.GetData(dstSlice, 1, 2);
            check(dstSlice[0] == 1000 && dstSlice[1] == 1500,
                  "IndexBuffer u32: GetData(startIndex, count) round-trips slice");

            IndexBuffer ibWriteOnly(dev, IndexElementSize::ThirtyTwoBits, 4, BufferUsage::WriteOnly);
            ibWriteOnly.SetData(src, 4);
            std::uint32_t dstW[4]{};
            check(throwsType<System::NotSupportedException>([&]{ ibWriteOnly.GetData(dstW, 4); }),
                  "IndexBuffer u32: GetData on WriteOnly buffer throws NotSupportedException");

            std::uint32_t dstOver[6]{};
            check(throwsType<System::ArgumentOutOfRangeException>([&]{ ib.GetData(dstOver, 0, 6); }),
                  "IndexBuffer u32: out-of-range GetData throws ArgumentOutOfRangeException");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    VertexIndexGetDataTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VertexIndexGetDataTest g;
    g.Run();
    return g.getResult();
}
