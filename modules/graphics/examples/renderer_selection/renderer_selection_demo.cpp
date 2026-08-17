// SPDX-License-Identifier: MS-PL
//
// plan_runtimerenderer.md RTR-P4-14: the reference for choosing a graphics renderer at runtime.
//
// This is the program a game author copies. It shows the three things that matter and nothing else:
// what this build actually contains, how to ask for one of those renderers before CNA starts, and
// what happens when you ask too late.
//
// Run it with CNA_GRAPHICS_RENDERER=<NAME> in the environment to see the precedence order: an
// explicit SetPreferred() call wins over the environment variable, which wins over the build
// default.

#include "CNA/GraphicsRendererFallbackRecord.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/InvalidOperationException.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    using CNA::GraphicsRendererSelection;

    // 1. What did this build actually link in?
    //
    // In the default, recommended mode this is exactly one renderer -- the one
    // -DCNA_GRAPHICS_RENDERER named. Asking for anything else is an error, not a silent downgrade.
    std::cout << "Renderers compiled into this build:";
    for (const CNA::GraphicsRendererType type : GraphicsRendererSelection::GetAvailable())
    {
        std::cout << ' ' << CNA::getGraphicsRendererName(type);
    }
    std::cout << '\n';

    std::cout << "Selected before any request: "
              << CNA::getGraphicsRendererName(GraphicsRendererSelection::GetSelected()) << '\n';

    // 2. Ask for a specific renderer -- BEFORE constructing any GraphicsDevice.
    //
    // Both an identity and its name are accepted; the name is the same spelling
    // CNA_GRAPHICS_RENDERER uses, matched case-insensitively.
    if (argc > 1)
    {
        const std::string requested = argv[1];
        try
        {
            GraphicsRendererSelection::SetPreferred(requested);
            std::cout << "Requested " << requested << " -- now selected: "
                      << CNA::getGraphicsRendererName(GraphicsRendererSelection::GetSelected())
                      << '\n';
        }
        catch (const System::Exception& e)
        {
            // The honest outcome for a renderer this build does not contain. A game that wants a
            // second chance opts into one explicitly:
            //   GraphicsRendererSelection::SetFallbackChain({CNA::GraphicsRendererType::Software});
            std::cout << "Requested " << requested << " -- refused: " << e.what() << '\n';
        }
    }

    // 3. Optionally configure a fallback chain (plan_runtimerenderer.md RTR-P5-21).
    //
    // Off by default: a renderer that cannot run is an ERROR, and CNA never substitutes another
    // one behind your back. Pass renderer names as further arguments to opt in.
    //
    //   cna_demo_renderer_selection LLGL OPENGLES3 SOFTWARE
    //
    // is a real example on a Wayland session, where LLGL genuinely cannot initialize -- it needs
    // the x11 SDL video driver -- so the chain is exercised without anything being simulated.
    if (argc > 2)
    {
        std::vector<CNA::GraphicsRendererType> chain;
        for (int i = 2; i < argc; ++i)
        {
            CNA::GraphicsRendererType parsed{};
            if (CNA::tryParseGraphicsRendererName(argv[i], parsed))
            {
                chain.push_back(parsed);
            }
            else
            {
                std::cout << "Ignoring unknown fallback renderer: " << argv[i] << '\n';
            }
        }
        if (!chain.empty())
        {
            CNA::GraphicsRendererSelection::SetFallbackChain(chain);
            std::cout << "Fallback chain enabled with " << chain.size() << " entr"
                      << (chain.size() == 1 ? "y" : "ies") << ".\n";
        }
    }

    // 4. Creating a device latches the choice.
    {
        Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
        std::cout << "Active renderer: "
                  << CNA::getGraphicsRendererName(GraphicsRendererSelection::GetActive()) << '\n';
    }

    // 5. What actually happened. The history is empty unless a fallback substituted something --
    // which is the point: a game that ends up on a different renderer than it asked for can say so.
    const auto history = CNA::GraphicsRendererSelection::GetFallbackHistory();
    if (history.empty())
    {
        std::cout << "No fallback occurred.\n";
    }
    else
    {
        std::cout << "Fallback history (" << history.size() << " renderer(s) passed over):\n";
        for (const auto& record : history)
        {
            std::cout << "  - " << CNA::getGraphicsRendererName(record.type) << " ("
                      << CNA::getGraphicsRendererFallbackReasonName(record.reason)
                      << "): " << record.message << '\n';
        }
    }

    // From here on the selection cannot change -- window flags, and in several renderers the whole
    // device, were fixed by the answer CNA already acted on.
    try
    {
        GraphicsRendererSelection::SetPreferred(GraphicsRendererSelection::GetActive());
        std::cout << "ERROR: selection was still changeable after a device existed\n";
        return 1;
    }
    catch (const System::InvalidOperationException&)
    {
        std::cout << "Selection is latched, as expected.\n";
    }

    return 0;
}
