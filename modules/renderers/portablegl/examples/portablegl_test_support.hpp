// SPDX-License-Identifier: MS-PL
// Shared harness for the PortableGL renderer's pixel-oracle tests.
//
// Every PortableGL test is a real Game running under CNA_GRAPHICS_RENDERER=PORTABLEGL that draws
// through the ordinary public XNA API and then reads the backbuffer back, so a check fails when
// the renderer is wrong rather than when a mock is wrong. This header only removes the
// copy-and-paste (the Game/GraphicsDeviceManager scaffolding, the pass counter and the readback
// predicates); every expectation lives in the test that states it.

#pragma once

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace portablegl_test
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
    using Microsoft::Xna::Framework::Graphics::VertexElement;
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
    using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

    /// The one vertex layout this renderer's 3D route accepts: Position0 @0 Vector3 +
    /// Color0 @12 Color, stride 16.
    inline VertexDeclaration PosColorDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
    }

    /// Six vertices tiling the whole NDC square at depth @p ndcZ, wound CLOCKWISE in XNA's
    /// top-left screen space (TL, TR, BR / TL, BR, BL). That is the front-facing winding under
    /// XNA's own default RasterizerState.CullCounterClockwise, so a quad built here survives
    /// culling and a quad built by ReversedFullQuad() below does not.
    inline std::array<VertexPositionColor, 6> FullQuad(const Color& color, float ndcZ = 0.0f)
    {
        return {{
            {Vector3(-1.0f,  1.0f, ndcZ), color},
            {Vector3( 1.0f,  1.0f, ndcZ), color},
            {Vector3( 1.0f, -1.0f, ndcZ), color},
            {Vector3(-1.0f,  1.0f, ndcZ), color},
            {Vector3( 1.0f, -1.0f, ndcZ), color},
            {Vector3(-1.0f, -1.0f, ndcZ), color},
        }};
    }

    /// The same quad wound COUNTER-CLOCKWISE on screen -- back-facing under
    /// RasterizerState.CullCounterClockwise and front-facing under CullClockwise.
    inline std::array<VertexPositionColor, 6> ReversedFullQuad(const Color& color, float ndcZ = 0.0f)
    {
        return {{
            {Vector3(-1.0f,  1.0f, ndcZ), color},
            {Vector3( 1.0f, -1.0f, ndcZ), color},
            {Vector3( 1.0f,  1.0f, ndcZ), color},
            {Vector3(-1.0f,  1.0f, ndcZ), color},
            {Vector3(-1.0f, -1.0f, ndcZ), color},
            {Vector3( 1.0f, -1.0f, ndcZ), color},
        }};
    }

    /// The four corners of the NDC square in clockwise-on-screen order (TL, TR, BR, BL), for the
    /// indexed routes. Index them with {0,1,2, 0,2,3}.
    inline std::array<VertexPositionColor, 4> FullQuadCorners(const Color& color, float ndcZ = 0.0f)
    {
        return {{
            {Vector3(-1.0f,  1.0f, ndcZ), color},
            {Vector3( 1.0f,  1.0f, ndcZ), color},
            {Vector3( 1.0f, -1.0f, ndcZ), color},
            {Vector3(-1.0f, -1.0f, ndcZ), color},
        }};
    }

    /// Base Game for a PortableGL pixel-oracle test: owns the GraphicsDeviceManager, the pass
    /// counter and the readback predicates. A derived test overrides Draw(), runs its checks and
    /// calls Finish().
    class PortableGLTestGame : public Microsoft::Xna::Framework::Game
    {
    public:
        /// @param size Square backbuffer edge length in pixels.
        explicit PortableGLTestGame(int size = 64) : size_(size)
        {
            gdm_ = std::make_unique<Microsoft::Xna::Framework::GraphicsDeviceManager>(this);
            gdm_->setPreferredBackBufferWidthProperty(size);
            gdm_->setPreferredBackBufferHeightProperty(size);
        }

        /// Process exit code: 0 when every check passed.
        [[nodiscard]] int getResult() const { return result_; }

    protected:
        /// Backbuffer edge length this test asked for.
        [[nodiscard]] int Size() const { return size_; }

        void check(bool ok, const char* label)
        {
            ++checkCount_;
            std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
            if (ok) ++passCount_;
        }

        /// Reads one pixel, including its alpha.
        Color PixelAt(int x, int y)
        {
            const Rectangle region(x, y, 1, 1);
            Color pixel(0, 0, 0, 0);
            getGraphicsDeviceProperty().GetBackBufferData(&region, &pixel, 0, 1);
            return pixel;
        }

        /// True when every pixel of @p region matches @p expected exactly on R, G and B.
        bool RegionIs(const Rectangle& region, const Color& expected)
        {
            return RegionWithin(region, expected, 0, false);
        }

        /// True when every pixel of @p region matches @p expected exactly on R, G, B and A.
        bool RegionIsRgba(const Rectangle& region, const Color& expected)
        {
            return RegionWithin(region, expected, 0, true);
        }

        /// True when every pixel of @p region is within @p tolerance per channel of @p expected.
        /// Used only where PortableGL's own documented truncation of a BLENDED float result can
        /// legitimately land one LSB below a round-to-nearest device; never to hide wrong maths.
        bool RegionNear(const Rectangle& region, const Color& expected, int tolerance,
                        bool includeAlpha = false)
        {
            return RegionWithin(region, expected, tolerance, includeAlpha);
        }

        /// Number of pixels in @p region that differ from @p background on R, G or B.
        int CountPixelsDifferentFrom(const Rectangle& region, const Color& background)
        {
            std::vector<Color> pixels(static_cast<std::size_t>(region.Width) *
                                          static_cast<std::size_t>(region.Height),
                                      Color(0, 0, 0, 0));
            getGraphicsDeviceProperty().GetBackBufferData(
                &region, pixels.data(), 0, static_cast<int>(pixels.size()));
            int count = 0;
            for (const Color& p : pixels)
            {
                if (p.getRProperty() != background.getRProperty() ||
                    p.getGProperty() != background.getGProperty() ||
                    p.getBProperty() != background.getBProperty())
                    ++count;
            }
            return count;
        }

        /// Prints the summary and ends the run.
        void Finish()
        {
            std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
            result_ = (passCount_ == checkCount_) ? 0 : 1;
            Exit();
        }

    private:
        bool RegionWithin(const Rectangle& region, const Color& expected, int tolerance,
                          bool includeAlpha)
        {
            std::vector<Color> pixels(static_cast<std::size_t>(region.Width) *
                                          static_cast<std::size_t>(region.Height),
                                      Color(0, 0, 0, 0));
            getGraphicsDeviceProperty().GetBackBufferData(
                &region, pixels.data(), 0, static_cast<int>(pixels.size()));
            const auto within = [tolerance](int actual, int wanted) {
                const int delta = actual - wanted;
                return delta <= tolerance && -delta <= tolerance;
            };
            for (const Color& p : pixels)
            {
                if (!within(p.getRProperty(), expected.getRProperty()) ||
                    !within(p.getGProperty(), expected.getGProperty()) ||
                    !within(p.getBProperty(), expected.getBProperty()))
                    return false;
                if (includeAlpha && !within(p.getAProperty(), expected.getAProperty()))
                    return false;
            }
            return true;
        }

        std::unique_ptr<Microsoft::Xna::Framework::GraphicsDeviceManager> gdm_;
        int size_;
        int passCount_ = 0;
        int checkCount_ = 0;
        int result_ = 1;
    };
}
