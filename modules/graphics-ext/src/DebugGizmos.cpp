// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/DebugGizmos.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/CascadedShadowMap.hpp"
#include "CNA/Graphics/ClusteredLightGrid.hpp"
#include "CNA/Graphics/DebugDraw.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/LightProbeVolumeEXT.hpp"
#include "CNA/Graphics/PointLightEXT.hpp"
#include "CNA/Graphics/SpotLightEXT.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::BoundingBox;
    using Microsoft::Xna::Framework::BoundingFrustum;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::MathHelper;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;

    namespace {

        constexpr int kEdges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7},
        };

        Vector3 Normalized(const Vector3& value, const Vector3& fallback)
        {
            const float length = std::sqrt(value.X * value.X + value.Y * value.Y
                                         + value.Z * value.Z);
            if (length < 1e-6f) return fallback;
            return Vector3(value.X / length, value.Y / length, value.Z / length);
        }

        /// A basis whose Z is the given axis, for placing a ring around it.
        void BasisFor(const Vector3& axis, Vector3& right, Vector3& up)
        {
            // Any vector not parallel to the axis will do; picking the one furthest from it keeps
            // the cross product away from zero, which is where a degenerate basis comes from.
            const Vector3 helper = std::abs(axis.Y) < 0.9f ? Vector3(0.0f, 1.0f, 0.0f)
                                                           : Vector3(1.0f, 0.0f, 0.0f);
            right = Normalized(Vector3(axis.Y * helper.Z - axis.Z * helper.Y,
                                       axis.Z * helper.X - axis.X * helper.Z,
                                       axis.X * helper.Y - axis.Y * helper.X),
                               Vector3(1.0f, 0.0f, 0.0f));
            up = Vector3(axis.Y * right.Z - axis.Z * right.Y,
                         axis.Z * right.X - axis.X * right.Z,
                         axis.X * right.Y - axis.Y * right.X);
        }

        void AddCone(DebugDraw& debug, const Vector3& apex, const Vector3& axis, const float range,
                     const float halfAngle, const Color& colour, const int segments)
        {
            const int steps = std::clamp(segments, 4, 128);
            const float radius = range * std::tan(std::clamp(halfAngle, 0.0f, 1.5533f));
            const Vector3 centre(apex.X + axis.X * range, apex.Y + axis.Y * range,
                                 apex.Z + axis.Z * range);

            Vector3 right, up;
            BasisFor(axis, right, up);

            const float step = MathHelper::TwoPi / static_cast<float>(steps);
            Vector3 previous;
            for (int i = 0; i <= steps; ++i)
            {
                const float angle = step * static_cast<float>(i);
                const float c = std::cos(angle) * radius;
                const float s = std::sin(angle) * radius;
                const Vector3 point(centre.X + right.X * c + up.X * s,
                                    centre.Y + right.Y * c + up.Y * s,
                                    centre.Z + right.Z * c + up.Z * s);
                if (i > 0) debug.addLine(previous, point, colour);
                // Four ribs and no more: a cone drawn with one rib per ring segment is a filled
                // triangle on screen and shows nothing.
                if (i % std::max(steps / 4, 1) == 0 && i < steps)
                    debug.addLine(apex, point, colour);
                previous = point;
            }
        }

        Vector3 TransformCoordinate(const Vector3& point, const Matrix& matrix)
        {
            const float x = point.X * matrix.M11 + point.Y * matrix.M21 + point.Z * matrix.M31
                          + matrix.M41;
            const float y = point.X * matrix.M12 + point.Y * matrix.M22 + point.Z * matrix.M32
                          + matrix.M42;
            const float z = point.X * matrix.M13 + point.Y * matrix.M23 + point.Z * matrix.M33
                          + matrix.M43;
            return Vector3(x, y, z);
        }

        void AddTransformedBox(DebugDraw& debug, const BoundingBox& box, const Matrix& matrix,
                               const Color& colour)
        {
            const std::vector<Vector3> local = box.GetCorners();
            std::vector<Vector3> world;
            world.reserve(local.size());
            for (const Vector3& corner : local) world.push_back(TransformCoordinate(corner, matrix));
            for (const auto& edge : kEdges)
                debug.addLine(world[edge[0]], world[edge[1]], colour);
        }

    } // namespace

    void addPointLightGizmo(DebugDraw& debug, const PointLightEXT& light, const Color& colour)
    {
        debug.addSphere(light.Position, light.Range, colour);
        debug.addCross(light.Position, light.Range * 0.05f, colour);
    }

    void addSpotLightGizmo(DebugDraw& debug, const SpotLightEXT& light, const Color& colour,
                           const int segments)
    {
        const Vector3 axis = Normalized(light.Direction, Vector3(0.0f, -1.0f, 0.0f));
        AddCone(debug, light.Position, axis, light.Range, light.OuterAngle, colour, segments);
        AddCone(debug, light.Position, axis, light.Range, light.InnerAngle, colour, segments);
    }

    void addDirectionalLightGizmo(DebugDraw& debug, const DirectionalLightEXT& light,
                                  const Vector3& at, const float length, const Color& colour)
    {
        const Vector3 axis = Normalized(light.Direction, Vector3(0.0f, -1.0f, 0.0f));
        const Vector3 tail(at.X - axis.X * length, at.Y - axis.Y * length, at.Z - axis.Z * length);
        debug.addLine(tail, at, colour);

        // A head, so the arrow reads as travelling rather than as a bare segment.
        Vector3 right, up;
        BasisFor(axis, right, up);
        const float head = length * 0.15f;
        const Vector3 base(at.X - axis.X * head, at.Y - axis.Y * head, at.Z - axis.Z * head);
        for (int i = 0; i < 4; ++i)
        {
            const float angle = MathHelper::TwoPi * static_cast<float>(i) / 4.0f;
            const float c = std::cos(angle) * head * 0.5f;
            const float s = std::sin(angle) * head * 0.5f;
            debug.addLine(at, Vector3(base.X + right.X * c + up.X * s,
                                      base.Y + right.Y * c + up.Y * s,
                                      base.Z + right.Z * c + up.Z * s), colour);
        }
    }

    void addProbeVolumeGizmo(DebugDraw& debug, const LightProbeVolumeEXT& volume,
                             const Color& colour, const float crossSize)
    {
        debug.addBox(volume.getBounds(), colour);
        for (int z = 0; z < volume.getCountZ(); ++z)
            for (int y = 0; y < volume.getCountY(); ++y)
                for (int x = 0; x < volume.getCountX(); ++x)
                    debug.addCross(volume.getProbePosition(x, y, z), crossSize, colour);
    }

    void addClusterSliceGizmo(DebugDraw& debug, const ClusteredLightGrid& grid,
                              const Matrix& inverseView, const Color& colour)
    {
        if (!grid.hasProjection()) return;

        for (int slice = 0; slice < grid.getSliceCount(); ++slice)
        {
            // The whole grid at this depth, from the two opposite corner tiles. Drawing every tile
            // would be tilesX * tilesY * slices boxes, which is a thicket rather than a picture.
            const BoundingBox first = grid.clusterBounds(0, 0, slice);
            const BoundingBox last  = grid.clusterBounds(grid.getTilesX() - 1,
                                                         grid.getTilesY() - 1, slice);
            AddTransformedBox(debug, BoundingBox::CreateMerged(first, last), inverseView, colour);
        }
    }

    void addCascadeGizmo(DebugDraw& debug, const CascadedShadowMap& cascades, const Color& colour)
    {
        for (int i = 0; i < cascades.getCascadeCount(); ++i)
            debug.addFrustum(BoundingFrustum(cascades.getCascadeMatrix(i)), colour);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
