// SPDX-License-Identifier: MS-PL

// plans/plan_cabi.md CABI-10: can two independently owned GraphicsDevices exist at once?
//
// fixcnats.md Phase 5 asks whether CNA can create a GraphicsDevice outside a Game, and forbids
// answering with a half-working constructor. XNA's own constructor shape already exists here --
// GraphicsDevice(GraphicsAdapter&, GraphicsProfile, const PresentationParameters&) -- and single
// standalone devices are already exercised by the graphics tests. What nothing covered is two of
// them alive together, which is exactly what a downstream cross-device validation test needs.
//
// Exit codes: 0 both devices constructed and disposed; 2 the second construction failed;
// 3 the first construction failed; 4 an unexpected failure afterwards.

#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include <cstdio>
#include <exception>
#include <memory>

using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;

int main()
{
    std::unique_ptr<GraphicsDevice> first;
    try {
        PresentationParameters parameters;
        first = std::make_unique<GraphicsDevice>(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);
    } catch (const std::exception& error) {
        std::printf("first device failed: %s\n", error.what());
        return 3;
    }
    std::printf("first device constructed\n");

    std::unique_ptr<GraphicsDevice> second;
    try {
        PresentationParameters parameters;
        second = std::make_unique<GraphicsDevice>(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);
    } catch (const std::exception& error) {
        std::printf("second device REFUSED: %s\n", error.what());
        return 2;
    }
    std::printf("second device constructed -- two devices coexist\n");

    try {
        // Destruction order is the other half of the question: releasing the first while the
        // second is live must not take the shared video subsystem out from under it.
        first.reset();
        std::printf("first device disposed while second still live\n");
        second.reset();
        std::printf("second device disposed\n");
    } catch (const std::exception& error) {
        std::printf("teardown failed: %s\n", error.what());
        return 4;
    }
    return 0;
}
