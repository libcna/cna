// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/DebugDraw.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::BoundingBox;
    using Microsoft::Xna::Framework::BoundingFrustum;
    using Microsoft::Xna::Framework::BoundingSphere;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::MathHelper;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::DepthStencilState;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

    namespace {

        constexpr int kMinSegments = 4;
        constexpr int kMaxSegments = 128;

        // XNA numbers the corners of a box and of a frustum the same way: 0-3 go round one face and
        // 4-7 round the opposite one, in the same rotational order. So one edge table draws both,
        // which is the reason `addFrustum` is four lines of code rather than its own geometry.
        constexpr int kEdges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7},
        };

    } // namespace

    DebugDraw::DebugDraw(GraphicsDevice& device)
        : device_(device), effect_(std::make_unique<BasicEffect>(device))
    {
        effect_->VertexColorEnabled = true;
    }

    DebugDraw::~DebugDraw() = default;

    void DebugDraw::begin(const Matrix& view, const Matrix& projection)
    {
        depthTestedLines_.clear();
        overlayLines_.clear();
        depthTested_ = true;
        open_ = true;
        effect_->World      = Matrix::getIdentityProperty();
        effect_->View       = view;
        effect_->Projection = projection;
    }

    void DebugDraw::clear()
    {
        depthTestedLines_.clear();
        overlayLines_.clear();
    }

    void DebugDraw::addLineTo(std::vector<VertexPositionColor>& list, const Vector3& from,
                              const Vector3& to, const Color& colour)
    {
        list.emplace_back(from, colour);
        list.emplace_back(to, colour);
    }

    void DebugDraw::addLine(const Vector3& from, const Vector3& to, const Color& colour)
    {
        addLineTo(depthTested_ ? depthTestedLines_ : overlayLines_, from, to, colour);
    }

    void DebugDraw::addBox(const BoundingBox& bounds, const Color& colour)
    {
        const std::vector<Vector3> corners = bounds.GetCorners();
        for (const auto& edge : kEdges)
            addLine(corners[edge[0]], corners[edge[1]], colour);
    }

    void DebugDraw::addFrustum(const BoundingFrustum& frustum, const Color& colour)
    {
        const std::vector<Vector3> corners = frustum.GetCorners();
        for (const auto& edge : kEdges)
            addLine(corners[edge[0]], corners[edge[1]], colour);
    }

    void DebugDraw::addSphere(const Vector3& centre, const float radius, const Color& colour,
                              const int segments)
    {
        const int steps = std::clamp(segments, kMinSegments, kMaxSegments);
        const float step = MathHelper::TwoPi / static_cast<float>(steps);

        for (int ring = 0; ring < 3; ++ring)
        {
            Vector3 previous;
            for (int i = 0; i <= steps; ++i)
            {
                const float angle = step * static_cast<float>(i);
                const float c = std::cos(angle) * radius;
                const float s = std::sin(angle) * radius;

                Vector3 point = centre;
                if (ring == 0)      { point.X += c; point.Y += s; }
                else if (ring == 1) { point.Y += c; point.Z += s; }
                else                { point.Z += c; point.X += s; }

                if (i > 0) addLine(previous, point, colour);
                previous = point;
            }
        }
    }

    void DebugDraw::addSphere(const BoundingSphere& sphere, const Color& colour, const int segments)
    {
        addSphere(sphere.Center, sphere.Radius, colour, segments);
    }

    void DebugDraw::addCross(const Vector3& position, const float size, const Color& colour)
    {
        addLine(Vector3(position.X - size, position.Y, position.Z),
                Vector3(position.X + size, position.Y, position.Z), colour);
        addLine(Vector3(position.X, position.Y - size, position.Z),
                Vector3(position.X, position.Y + size, position.Z), colour);
        addLine(Vector3(position.X, position.Y, position.Z - size),
                Vector3(position.X, position.Y, position.Z + size), colour);
    }

    bool DebugDraw::isDepthTested() const { return depthTested_; }

    void DebugDraw::setDepthTested(const bool value) { depthTested_ = value; }

    int DebugDraw::getLineCount() const
    {
        return static_cast<int>((depthTestedLines_.size() + overlayLines_.size()) / 2);
    }

    const std::vector<VertexPositionColor>& DebugDraw::getVertices(const bool depthTested) const
    {
        return depthTested ? depthTestedLines_ : overlayLines_;
    }

    void DebugDraw::drawList(const std::vector<VertexPositionColor>& list, const bool depthTested)
    {
        if (list.empty()) return;

        device_.setDepthStencilStateProperty(depthTested ? DepthStencilState::Default
                                                         : DepthStencilState::None);
        effect_->Apply();
        // No vertex buffer: a debug batch changes every frame and rebuilding one would cost more
        // than the draw it saves.
        device_.SetVertexBuffer(nullptr);
        device_.DrawUserPrimitives(PrimitiveType::LineList, list.data(), 0,
                                   static_cast<int>(list.size() / 2));
    }

    void DebugDraw::end()
    {
        if (!open_) return;
        open_ = false;

        const DepthStencilState previous = device_.getDepthStencilStateProperty();

        drawList(depthTestedLines_, true);
        // The overlay pass goes second so an overlay line crossing a depth-tested one wins, which
        // is the point of asking for it.
        drawList(overlayLines_, false);

        device_.setDepthStencilStateProperty(previous);

        depthTestedLines_.clear();
        overlayLines_.clear();
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
