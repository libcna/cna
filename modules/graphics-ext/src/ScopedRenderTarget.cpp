// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ScopedRenderTarget.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;

    ScopedRenderTarget::ScopedRenderTarget(GraphicsDevice& device, RenderTarget2D* destination)
        : device_(device)
    {
        try
        {
            previous_ = device_.GetRenderTargets();
            recorded_ = true;
        }
        catch (...)
        {
            // A renderer that cannot report its bindings is not a reason to refuse to run the pass;
            // it only means the destructor falls back to the back buffer, which is exactly what the
            // unscoped code this replaces did every time.
            recorded_ = false;
        }

        device_.SetRenderTarget(destination);
    }

    ScopedRenderTarget::~ScopedRenderTarget()
    {
        try
        {
            if (recorded_ && !previous_.empty())
                device_.SetRenderTargets(previous_);
            else
                device_.SetRenderTarget(nullptr);
        }
        catch (...)
        {
            // Deliberately swallowed. This runs on the unwind path of whatever went wrong first,
            // and a throwing destructor there turns a diagnosable exception into std::terminate.
        }
    }

    bool ScopedRenderTarget::hasRecordedPrevious() const { return recorded_; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
