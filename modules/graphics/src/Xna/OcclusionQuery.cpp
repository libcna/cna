// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    OcclusionQuery::OcclusionQuery(GraphicsDevice& device)
        : GraphicsResource(&device)
        , renderer_(device.GetRenderer().CreateOcclusionQuery())
    {
    }

    OcclusionQuery::~OcclusionQuery() = default;

    void OcclusionQuery::Dispose(bool disposing)
    {
        renderer_.reset();
        GraphicsResource::Dispose(disposing);
    }

    bool OcclusionQuery::getIsCompleteProperty() const
    {
        System::ObjectDisposedException::ThrowIf(
            getIsDisposedProperty(), getNameProperty());
        if (renderer_) return renderer_->IsComplete();
        return false;
    }

    int OcclusionQuery::getPixelCountProperty() const
    {
        System::ObjectDisposedException::ThrowIf(
            getIsDisposedProperty(), getNameProperty());
        if (renderer_) return renderer_->PixelCount();
        return 0;
    }

    bool OcclusionQuery::isPixelCountPreciseEXT() const
    {
        System::ObjectDisposedException::ThrowIf(
            getIsDisposedProperty(), getNameProperty());
        if (renderer_) return renderer_->PixelCountIsPreciseEXT();
        return false;
    }

    void OcclusionQuery::Begin()
    {
        System::ObjectDisposedException::ThrowIf(
            getIsDisposedProperty(), getNameProperty());
        if (renderer_) renderer_->Begin();
    }

    void OcclusionQuery::End()
    {
        System::ObjectDisposedException::ThrowIf(
            getIsDisposedProperty(), getNameProperty());
        if (renderer_) renderer_->End();
    }

    const std::string& OcclusionQuery::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.OcclusionQuery";
        return name;
    }
}
