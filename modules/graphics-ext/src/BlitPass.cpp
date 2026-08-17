// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/BlitPass.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace CNA::Graphics {

    BlitPass::BlitPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
    }

    BlitPass::~BlitPass() = default;

    void BlitPass::apply(const PostProcessContext& context)
    {
        fullscreen_->draw(context.source, context.destination, nullptr,
                          context.width, context.height);
    }

    const std::string& BlitPass::getName() const
    {
        static const std::string name = "Blit";
        return name;
    }

    bool BlitPass::isSupported(Microsoft::Xna::Framework::Graphics::GraphicsDevice&) const
    {
        return true;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
