// SPDX-License-Identifier: MS-PL

// plans/plan_cabi.md CABI-15: does ContentLost actually reach the resources a reset destroys?
//
// The event fires only where a renderer reports a real device reset, and the families that can
// (DirectX9, for example) are not built here. So this drives the same entry point the
// renderer callback drives -- GraphicsDevice::NotifyContentLostResourcesEXT -- and checks the
// contract around it: false before, true and raised after, and cleared by writing content again.
//
// Exit codes: 0 pass; 2 wrong initial state; 3 the event did not reach a resource;
// 4 IsContentLost did not become true; 5 a write did not clear it; 6 no device.

#include "CNA/GraphicsCapability.hpp"
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
#include <optional>

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

    // plans/plan_apple_m4.md AM4-195: vertex and index buffers exist only where the renderer draws
    // 3D; a 2D-only one (SDL_RENDERER) refuses to create them, and the render-target half of the
    // contract is then the whole of it.
    const bool buffers = device->SupportsCapability(CNA::GraphicsCapability::ThreeD);
    const VertexDeclaration& declaration = VertexPositionColor::getVertexDeclarationStatic();
    std::optional<DynamicVertexBuffer> vertices;
    std::optional<DynamicIndexBuffer> indices;
    if (buffers) {
        vertices.emplace(*device, declaration, 3, BufferUsage::WriteOnly);
        indices.emplace(*device, IndexElementSize::SixteenBits, 3, BufferUsage::WriteOnly);
    }
    RenderTarget2D target(*device, 4, 4);

    int vertexEvents = 0;
    int indexEvents = 0;
    int targetEvents = 0;
    if (buffers) {
        vertices->ContentLost += [&](System::Object*, const System::EventArgs&) { ++vertexEvents; };
        indices->ContentLost += [&](System::Object*, const System::EventArgs&) { ++indexEvents; };
    }
    target.ContentLost += [&](System::Object*, const System::EventArgs&) { ++targetEvents; };

    // Nothing has been lost, so nothing claims it has.
    if ((buffers && (vertices->getIsContentLostProperty() || indices->getIsContentLostProperty())) ||
        target.getIsContentLostProperty()) {
        std::printf("a resource reported lost content before any reset\n");
        return 2;
    }

    device->NotifyContentLostResourcesEXT();

    const int expectedBufferEvents = buffers ? 1 : 0;
    if (vertexEvents != expectedBufferEvents || indexEvents != expectedBufferEvents ||
        targetEvents != 1) {
        std::printf("events: vertex=%d index=%d target=%d (expected %d, %d, 1)\n",
                    vertexEvents, indexEvents, targetEvents, expectedBufferEvents,
                    expectedBufferEvents);
        return 3;
    }
    if ((buffers && (!vertices->getIsContentLostProperty() || !indices->getIsContentLostProperty())) ||
        !target.getIsContentLostProperty()) {
        std::printf("the event fired but IsContentLost stayed false\n");
        return 4;
    }

    // Writing the content again is what makes it no longer lost -- XNA's own rule.
    if (buffers) {
        const VertexPositionColor written[3] = {};
        vertices->SetData(written, 0, 3, SetDataOptions::Discard);
        const std::uint16_t writtenIndices[3] = {0U, 1U, 2U};
        indices->SetData(writtenIndices, 0, 3, SetDataOptions::Discard);
        if (vertices->getIsContentLostProperty() || indices->getIsContentLostProperty()) {
            std::printf("a write did not clear the lost flag\n");
            return 5;
        }
    }

    std::printf("content-lost probe: events raised, flags set, writes cleared them\n");
    return 0;
}
