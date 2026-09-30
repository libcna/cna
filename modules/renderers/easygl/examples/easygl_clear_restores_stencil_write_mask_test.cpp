// SPDX-License-Identifier: MS-PL
// Found while triaging cna-car-simulator's stencil finding: XNA's Clear ignores the bound
// DepthStencilState's StencilWriteMask, but glClear honours glStencilMask, so every EasyGL clear
// opens the mask for the clear and puts the state's mask back afterwards -- every clear except
// Target|Stencil, which opened it and left it open. The next draw under that same state then wrote
// stencil bits its StencilWriteMask excludes.
//
// A state with StencilWriteMask 0x0F is bound across Clear(Target|Stencil, 0), then a Replace draw
// with reference 0xFF must leave 0x0F in the buffer; an Equal 0x0F draw then shows red. With the
// mask left open the buffer holds 0xFF and the red draw fails the test. The other stencil clears
// (Stencil alone, Depth|Stencil, all three) are checked the same way.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class ClearRestoresStencilWriteMaskTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        RenderTarget2D target(device, 8, 8, false, SurfaceFormat::Color,
                              DepthFormat::Depth24Stencil8);
        BasicEffect effect(device);
        effect.setVertexColorEnabledProperty(true);

        const auto quad = [](const Color& colour) {
            return std::vector<VertexPositionColor>{
                {Vector3(-1, 1, 0), colour}, {Vector3(-1, -1, 0), colour},
                {Vector3(1, -1, 0), colour}, {Vector3(-1, 1, 0), colour},
                {Vector3(1, -1, 0), colour}, {Vector3(1, 1, 0), colour}};
        };
        const auto draw = [&](const std::vector<VertexPositionColor>& vertices) {
            effect.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 2,
                                      VertexPositionColor::getVertexDeclarationStatic());
        };

        DepthStencilState maskedWrite;
        maskedWrite.setDepthBufferEnableProperty(false);
        maskedWrite.setStencilEnableProperty(true);
        maskedWrite.setStencilFunctionProperty(CompareFunction::Always);
        maskedWrite.setStencilPassProperty(StencilOperation::Replace);
        maskedWrite.setReferenceStencilProperty(0xFF);
        maskedWrite.setStencilWriteMaskProperty(0x0F);
        DepthStencilState equalLow;
        equalLow.setDepthBufferEnableProperty(false);
        equalLow.setStencilEnableProperty(true);
        equalLow.setStencilFunctionProperty(CompareFunction::Equal);
        equalLow.setReferenceStencilProperty(0x0F);
        equalLow.setStencilWriteMaskProperty(0);

        struct Case
        {
            const char* name;
            ClearOptions options;
        };
        const Case cases[] = {
            {"Target|Stencil", ClearOptions::Target | ClearOptions::Stencil},
            {"Stencil", ClearOptions::Stencil},
            {"DepthBuffer|Stencil", ClearOptions::DepthBuffer | ClearOptions::Stencil},
            {"Target|DepthBuffer|Stencil",
             ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil},
        };
        for (const Case& testCase : cases)
        {
            device.SetRenderTarget(&target);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                         Color::Black, 1.0f, 0);
            device.setDepthStencilStateProperty(maskedWrite);
            device.Clear(testCase.options, Color::Black, 1.0f, 0);
            draw(quad(Color::Black));
            device.setDepthStencilStateProperty(equalLow);
            draw(quad(Color::Red));
            device.SetRenderTarget(nullptr);
            device.setDepthStencilStateProperty(DepthStencilState::None);

            std::vector<Color> pixels(64);
            target.GetData(pixels.data(), 64);
            const bool pass = pixels[36] == Color::Red;
            std::printf("[%s] Clear(%s) under StencilWriteMask 0x0F keeps the mask: centre "
                        "(%d,%d,%d)\n",
                        pass ? "PASS" : "FAIL", testCase.name, pixels[36].getRProperty(),
                        pixels[36].getGProperty(), pixels[36].getBProperty());
            if (!pass) { MarkFailedEXT(); }
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<ClearRestoresStencilWriteMaskTest>();
}
