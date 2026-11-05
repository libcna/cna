// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "../Internal/ProfileValidation.hpp"
#include "../Internal/DeferredResourceCreation.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/NullReferenceException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    OcclusionQuery::OcclusionQuery(GraphicsDevice& device)
        : GraphicsResource(CNA::Internal::Graphics::DeferResourceCreated(&device))
    {
        // Recovered Microsoft XNA 4.0 constructs a query only when the selected profile advertises
        // it. Reach's profile table does not; the renderer-wide capability is checked separately
        // so an incapable HiDef renderer cannot hand out a public object backed by a null factory.
        // XNA OcclusionQuery::.ctor IL_0019-0040 refuses the profile before the renderer.
        if (device.getGraphicsProfileProperty() == GraphicsProfile::Reach)
            throw System::NotSupportedException(CNA::Internal::Graphics::ProfileDiagnostic(
                device.getGraphicsProfileProperty(), "does not support OcclusionQuery."));
        if (!device.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery))
        {
            throw System::NotSupportedException(
                "OcclusionQuery is not supported by the active graphics profile and renderer.");
        }
        renderer_ = device.GetRenderer().CreateLogicalOcclusionQueryEXT();
        if (!renderer_)
        {
            throw System::NotSupportedException(
                "OcclusionQuery is advertised but the active renderer created no query resource.");
        }
        // XNA OcclusionQuery::.ctor checks the profile (IL_0019-0040) and creates the query
        // (IL_006c) before AddTrackedObject announces it (IL_009e).
        CNA::Internal::Graphics::PublishResourceCreated(*this);
    }

    bool OcclusionQuery::RaisesDisposingEventInternal() const noexcept
    {
        // OcclusionQuery::Dispose(bool) IL_0003-000d: '~OcclusionQuery' publishes isDisposed
        // before GraphicsResource.Dispose(true) runs, so XNA never raises Disposing here.
        return false;
    }

    OcclusionQuery::~OcclusionQuery() = default;

    void OcclusionQuery::Dispose(bool disposing)
    {
        renderer_.reset();
        GraphicsResource::Dispose(disposing);
    }

    bool OcclusionQuery::getIsCompleteProperty() const
    {
        // XNA records the observation even when the native result is still unavailable. A caller
        // must make this observation (directly or through PixelCount) before reusing the object.
        // OcclusionQuery::get_IsComplete IL_0000-0002 stores it before anything else, and has no
        // disposal check: after Dispose released the native query (ReleaseNativeObject
        // IL_0024-0032) IL_000e-0012 answer false (owner decision 2026-11-04, TQ-035).
        hasIsCompleteBeenQueried_ = true;
        if (!renderer_ || !hasCalledBegin_ || !renderer_->IsComplete())
            return false;
        pixelCount_ = renderer_->PixelCount();
        return true;
    }

    int OcclusionQuery::getPixelCountProperty() const
    {
        // get_PixelCount IL_0000-0012: any IsComplete == false, a disposed query's included, is
        // InvalidOperationException -- never ObjectDisposedException.
        // Refresh FrameworkResources.DataNotAvailable is the diagnostic selected at IL_0008.
        if (!getIsCompleteProperty())
        {
            throw System::InvalidOperationException(
                "The query data is not yet available. Use the IsComplete property to determine if the "
                "data is available before attempting to retrieve it.");
        }
        return pixelCount_;
    }

    bool OcclusionQuery::isPixelCountPreciseEXT() const
    {
        // Like IsComplete, no disposal check: a released query counts nothing, precisely or not.
        if (renderer_) return renderer_->PixelCountIsPreciseEXT();
        return false;
    }

    void OcclusionQuery::Begin()
    {
        // OcclusionQuery::Begin IL_0000-0025 refuses an open pair and an unobserved result before
        // it touches the native query; there is no disposal check. IL_0026-0035 then call through
        // the IDirect3DQuery9 pointer that Dispose cleared, which the CLR reports as a
        // NullReferenceException (owner decision 2026-11-04, TQ-035).
        // Refresh resources EndMustBeCalledBeforeBegin / IsCompleteMustBeCalled supply the text.
        if (isInBeginEndPair_)
            throw System::InvalidOperationException("Begin cannot be called again until End has been successfully called.");
        if (!hasIsCompleteBeenQueried_)
            throw System::InvalidOperationException(
                "Begin may not be called on this query object again before IsComplete has been checked.");
        if (!renderer_)
            throw System::NullReferenceException();

        renderer_->Begin();
        isInBeginEndPair_ = true;
        hasCalledBegin_ = true;
        hasIsCompleteBeenQueried_ = false;
    }

    void OcclusionQuery::End()
    {
        // OcclusionQuery::End IL_0000-0012 refuses a missing Begin first; IL_0013-0022 then call
        // through the cleared native pointer, as Begin does.
        // Refresh FrameworkResources.BeginMustBeCalledBeforeEnd supplies the text.
        if (!isInBeginEndPair_)
            throw System::InvalidOperationException("Begin must be called successfully before End can be called.");
        if (!renderer_)
            throw System::NullReferenceException();

        renderer_->End();
        isInBeginEndPair_ = false;
    }

    const std::string& OcclusionQuery::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.OcclusionQuery";
        return name;
    }
}
