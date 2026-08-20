// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/AsciiPass.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/AsciiPostProcessEffect.hpp"
#include "CNA/Graphics/ScopedRenderTarget.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    AsciiPass::AsciiPass(GraphicsDevice& device)
        : device_(device), effect_(std::make_unique<AsciiPostProcessEffect>(device))
    {
    }

    AsciiPass::~AsciiPass() = default;

    void AsciiPass::apply(const PostProcessContext& context)
    {
        if (context.source == nullptr)
            throw std::invalid_argument("CNA::Graphics::AsciiPass::apply: source must not be null");
        if (context.width <= 0 || context.height <= 0)
            throw std::invalid_argument("CNA::Graphics::AsciiPass::apply: the size must be positive");

        // The effect draws wherever the device currently points, so the pass's whole job is to
        // point it at the destination -- and to put the previous target back afterwards, including
        // when the readback inside Draw throws (plans/plan_modern.md MOD-203).
        const ScopedRenderTarget bound(device_, context.destination);
        effect_->Draw(*context.source, Rectangle(0, 0, context.width, context.height));
    }

    const std::string& AsciiPass::getName() const
    {
        static const std::string name = "Ascii";
        return name;
    }

    bool AsciiPass::isSupported(GraphicsDevice& device) const
    {
        // No capability describes "GetData works on an ordinary texture", so this asks by doing.
        // One texel, once per call -- and callers ask this at setup, not per frame.
        try
        {
            Texture2D probe(device, 1, 1);
            const Color white = Color::White;
            probe.SetData(&white, 1);
            Color read = Color::Black;
            probe.GetData(&read, 1);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    AsciiPostProcessEffect& AsciiPass::getEffect() const { return *effect_; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
