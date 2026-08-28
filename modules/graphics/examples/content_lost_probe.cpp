// SPDX-License-Identifier: MS-PL

// plans/plan_cabi.md CABI-15: does ContentLost actually reach the resources a reset destroys?
//
// The event fires only where a renderer reports a real device reset, and the three families that
// can (DirectX9, Direct2D, Skia) are not built here. So this drives the same entry point the
// renderer callback drives -- GraphicsDevice::NotifyContentLostResourcesEXT -- and checks the
// contract around it: false before, true and raised after, and cleared by writing content again.
//
// Exit codes: 0 pass; 2 wrong initial state; 3 the event did not reach a resource;
// 4 IsContentLost did not become true; 5 a write did not clear it; 6 no device.

#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <exception>
#include <memory>

using namespace Microsoft::Xna::Framework::Graphics;

int main()
{
    std::unique_ptr<GraphicsDevice> device;
    try {
        PresentationParameters parameters;
        device = std::make_unique<GraphicsDevice>(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);
    } catch (const std::exception& error) {
        std::printf("no device: %s\n", error.what());
        return 6;
    }

    const VertexDeclaration& declaration = VertexPositionColor::getVertexDeclarationStatic();
    DynamicVertexBuffer vertices(*device, declaration, 3, BufferUsage::WriteOnly);
    DynamicIndexBuffer indices(*device, IndexElementSize::SixteenBits, 3, BufferUsage::WriteOnly);
    RenderTarget2D target(*device, 4, 4);

    int vertexEvents = 0;
    int indexEvents = 0;
    int targetEvents = 0;
    vertices.ContentLost += [&](System::Object*, const System::EventArgs&) { ++vertexEvents; };
    indices.ContentLost += [&](System::Object*, const System::EventArgs&) { ++indexEvents; };
    target.ContentLost += [&](System::Object*, const System::EventArgs&) { ++targetEvents; };

    // Nothing has been lost, so nothing claims it has.
    if (vertices.getIsContentLostProperty() || indices.getIsContentLostProperty() ||
        target.getIsContentLostProperty()) {
        std::printf("a resource reported lost content before any reset\n");
        return 2;
    }

    device->NotifyContentLostResourcesEXT();

    if (vertexEvents != 1 || indexEvents != 1 || targetEvents != 1) {
        std::printf("events: vertex=%d index=%d target=%d (each should be 1)\n",
                    vertexEvents, indexEvents, targetEvents);
        return 3;
    }
    if (!vertices.getIsContentLostProperty() || !indices.getIsContentLostProperty() ||
        !target.getIsContentLostProperty()) {
        std::printf("the event fired but IsContentLost stayed false\n");
        return 4;
    }

    // Writing the content again is what makes it no longer lost -- XNA's own rule.
    const VertexPositionColor written[3] = {};
    vertices.SetData(written, 0, 3, SetDataOptions::Discard);
    const std::uint16_t writtenIndices[3] = {0U, 1U, 2U};
    indices.SetData(writtenIndices, 0, 3, SetDataOptions::Discard);
    if (vertices.getIsContentLostProperty() || indices.getIsContentLostProperty()) {
        std::printf("a write did not clear the lost flag\n");
        return 5;
    }

    std::printf("content-lost probe: events raised, flags set, writes cleared them\n");
    return 0;
}
