// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    OcclusionQuery::OcclusionQuery(GraphicsDevice& device)
        : GraphicsResource(&device)
    {
        // Recovered Microsoft XNA 4.0 constructs a query only when the selected profile advertises
        // it. Reach's profile table does not; the renderer-wide capability is checked separately
        // so an incapable HiDef renderer cannot hand out a public object backed by a null factory.
        if (device.getGraphicsProfileProperty() == GraphicsProfile::Reach ||
            !device.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery))
        {
            throw System::NotSupportedException(
                "OcclusionQuery is not supported by the active graphics profile and renderer.");
        }
        renderer_ = device.GetRenderer().CreateOcclusionQuery();
        if (!renderer_)
        {
            throw System::NotSupportedException(
                "OcclusionQuery is advertised but the active renderer created no query resource.");
        }
    }

    OcclusionQuery::~OcclusionQuery() = default;

    void OcclusionQuery::Dispose(bool disposing)
    {
        renderer_.reset();
        GraphicsResource::Dispose(disposing);
    }

    bool OcclusionQuery::getIsCompleteProperty() const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("OcclusionQuery");

        // XNA records the observation even when the native result is still unavailable. A caller
        // must make this observation (directly or through PixelCount) before reusing the object.
        hasIsCompleteBeenQueried_ = true;
        if (!hasCalledBegin_ || !renderer_ || !renderer_->IsComplete())
            return false;
        pixelCount_ = renderer_->PixelCount();
        return true;
    }

    int OcclusionQuery::getPixelCountProperty() const
    {
        if (!getIsCompleteProperty())
        {
            throw System::InvalidOperationException(
                "The occlusion query has not completed; query IsComplete before reading PixelCount.");
        }
        return pixelCount_;
    }

    bool OcclusionQuery::isPixelCountPreciseEXT() const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("OcclusionQuery");
        if (renderer_) return renderer_->PixelCountIsPreciseEXT();
        return false;
    }

    void OcclusionQuery::Begin()
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("OcclusionQuery");
        if (isInBeginEndPair_)
            throw System::InvalidOperationException("End must be called before Begin is called again.");
        if (!hasIsCompleteBeenQueried_)
            throw System::InvalidOperationException(
                "IsComplete must be queried before beginning this occlusion query again.");

        renderer_->Begin();
        isInBeginEndPair_ = true;
        hasCalledBegin_ = true;
        hasIsCompleteBeenQueried_ = false;
    }

    void OcclusionQuery::End()
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("OcclusionQuery");
        if (!isInBeginEndPair_)
            throw System::InvalidOperationException("Begin must be called before End.");

        renderer_->End();
        isInBeginEndPair_ = false;
    }

    const std::string& OcclusionQuery::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.OcclusionQuery";
        return name;
    }
}
