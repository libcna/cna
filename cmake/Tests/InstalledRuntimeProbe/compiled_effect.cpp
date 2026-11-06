// SPDX-License-Identifier: MS-PL
#include "compiled_effect_policy.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include <cstdio>
#include <exception>

int main()
{
    using namespace Microsoft::Xna::Framework::Graphics;
    try
    {
        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              PresentationParameters::CreateWindowedEXT(64, 64));
        if (device.GetGraphicsRendererName() != PROBE_EXPECTED_RENDERER) return 2;
        if (!InstalledCompiledEffectPolicy(device)) return 3;
        std::puts("installed compiled-effect capability and pixel/refusal checks passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "installed compiled-effect probe: %s\n", error.what());
        return 1;
    }
}
