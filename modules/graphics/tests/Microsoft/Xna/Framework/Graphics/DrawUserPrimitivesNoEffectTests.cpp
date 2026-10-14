// SPDX-License-Identifier: MS-PL
// RRC-047: a user-array draw with no applied Effect is refused -- on every overload, with exactly
// InvalidOperationException and exactly CNA's message, and the device stays usable afterwards.
//
// The retired SDL_RENDERER suite held this for all five DrawUserPrimitives shapes and all ten
// DrawUserIndexedPrimitives shapes (Tasks 721/722); after its retirement only one overload and type
// was checked anywhere. XNA refuses a draw without a vertex and pixel shader with
// InvalidOperationException; each overload reaches that refusal on its own path, so each is
// asserted on its own. Valid arguments are passed throughout so the effect check, which follows
// argument validation, is the one that answers.

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <string>
#include <typeinfo>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/InvalidOperationException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;

namespace
{
    const std::string kUserMessage =
        "GraphicsDevice::DrawUserPrimitives: no effect has been applied.";
    const std::string kUserIndexedMessage =
        "GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.";

    /// Empty when @p draw threw exactly InvalidOperationException (not a subclass) with exactly
    /// @p message; otherwise what it did instead.
    std::string RefusalMismatch(const std::function<void()>& draw, const std::string& message)
    {
        try
        {
            draw();
            return "the draw was accepted";
        }
        catch (const System::InvalidOperationException& e)
        {
            if (typeid(e) != typeid(System::InvalidOperationException))
                return std::string("threw a subclass, ") + typeid(e).name();
            if (message != e.what())
                return std::string("threw the message \"") + e.what() + "\"";
            return {};
        }
        catch (const std::exception& e)
        {
            return std::string("threw ") + typeid(e).name() + ": " + e.what();
        }
    }

    class DrawUserPrimitivesNoEffectTest : public ::testing::Test
    {
    protected:
        GraphicsDevice gd;

        const VertexPositionColor vpc[3] = {
            {Vector3(0.f, 0.f, 0.f), Color::White},
            {Vector3(1.f, 0.f, 0.f), Color::White},
            {Vector3(0.f, 1.f, 0.f), Color::White}};
        const VertexPositionColorTexture vpct[3] = {
            {Vector3(0.f, 0.f, 0.f), Color::White, Vector2(0.f, 0.f)},
            {Vector3(1.f, 0.f, 0.f), Color::White, Vector2(1.f, 0.f)},
            {Vector3(0.f, 1.f, 0.f), Color::White, Vector2(0.f, 1.f)}};
        const VertexPositionTexture vpt[3] = {
            {Vector3(0.f, 0.f, 0.f), Vector2(0.f, 0.f)},
            {Vector3(1.f, 0.f, 0.f), Vector2(1.f, 0.f)},
            {Vector3(0.f, 1.f, 0.f), Vector2(0.f, 1.f)}};
        const VertexPositionNormalTexture vpnt[3] = {
            {Vector3(0.f, 0.f, 0.f), Vector3(0.f, 0.f, 1.f), Vector2(0.f, 0.f)},
            {Vector3(1.f, 0.f, 0.f), Vector3(0.f, 0.f, 1.f), Vector2(1.f, 0.f)},
            {Vector3(0.f, 1.f, 0.f), Vector3(0.f, 0.f, 1.f), Vector2(0.f, 1.f)}};
        const std::uint16_t indices16[3] = {0, 1, 2};
        const std::uint32_t indices32[3] = {0, 1, 2};

        void SetUp() override
        {
            // A renderer with no 3D pipeline refuses every user-array draw before the effect check.
            if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
                GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
            // Reach refuses 32-bit user indices first (NotSupportedException), as XNA does.
            gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
        }

        /// After the refusals, an applied effect makes the very same draw succeed.
        void ExpectUsableAfterwards()
        {
            BasicEffect effect(gd);
            effect.setVertexColorEnabledProperty(true);
            effect.Apply();
            EXPECT_NO_THROW(gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpc, 0, 1));
        }
    };
}

TEST_F(DrawUserPrimitivesNoEffectTest, EveryDrawUserPrimitivesOverloadRefusesWithTheExactMessage)
{
    const auto& declaration = VertexPositionColor::getVertexDeclarationStatic();
    EXPECT_EQ(RefusalMismatch([&] { gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpc, 0, 1); },
                              kUserMessage), "") << "VertexPositionColor";
    EXPECT_EQ(RefusalMismatch([&] { gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpct, 0, 1); },
                              kUserMessage), "") << "VertexPositionColorTexture";
    EXPECT_EQ(RefusalMismatch([&] { gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpt, 0, 1); },
                              kUserMessage), "") << "VertexPositionTexture";
    EXPECT_EQ(RefusalMismatch([&] { gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpnt, 0, 1); },
                              kUserMessage), "") << "VertexPositionNormalTexture";
    EXPECT_EQ(RefusalMismatch(
                  [&] {
                      gd.DrawUserPrimitives(PrimitiveType::TriangleList,
                                            static_cast<const void*>(vpc), 0, 1, declaration);
                  },
                  kUserMessage), "") << "raw pointer + VertexDeclaration";
    ExpectUsableAfterwards();
}

TEST_F(DrawUserPrimitivesNoEffectTest, EveryDrawUserIndexedPrimitivesOverloadRefusesWithTheExactMessage)
{
    const auto& declaration = VertexPositionColor::getVertexDeclarationStatic();
    const auto refuses = [&](const std::function<void()>& draw, const char* label) {
        EXPECT_EQ(RefusalMismatch(draw, kUserIndexedMessage), "") << label;
    };
    refuses([&] { gd.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpc, 0, 3, indices16, 0, 1); },
            "VertexPositionColor, 16-bit");
    refuses([&] { gd.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpct, 0, 3, indices16, 0, 1); },
            "VertexPositionColorTexture, 16-bit");
    refuses([&] { gd.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpt, 0, 3, indices16, 0, 1); },
            "VertexPositionTexture, 16-bit");
    refuses([&] { gd.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpnt, 0, 3, indices16, 0, 1); },
            "VertexPositionNormalTexture, 16-bit");
    refuses([&] { gd.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpc, 0, 3, indices32, 0, 1); },
            "VertexPositionColor, 32-bit");
    refuses([&] { gd.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpct, 0, 3, indices32, 0, 1); },
            "VertexPositionColorTexture, 32-bit");
    refuses([&] { gd.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpt, 0, 3, indices32, 0, 1); },
            "VertexPositionTexture, 32-bit");
    refuses([&] { gd.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpnt, 0, 3, indices32, 0, 1); },
            "VertexPositionNormalTexture, 32-bit");
    refuses([&] {
                gd.DrawUserIndexedPrimitives(PrimitiveType::TriangleList,
                                             static_cast<const void*>(vpc), 0, 3, indices16, 0, 1,
                                             declaration);
            },
            "raw pointer + VertexDeclaration, 16-bit");
    refuses([&] {
                gd.DrawUserIndexedPrimitives(PrimitiveType::TriangleList,
                                             static_cast<const void*>(vpc), 0, 3, indices32, 0, 1,
                                             declaration);
            },
            "raw pointer + VertexDeclaration, 32-bit");
    ExpectUsableAfterwards();
}
