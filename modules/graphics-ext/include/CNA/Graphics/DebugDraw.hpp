// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <memory>
#include <vector>

namespace Microsoft::Xna::Framework {
    struct BoundingBox;
    struct BoundingFrustum;
    struct BoundingSphere;
}

namespace Microsoft::Xna::Framework::Graphics {
    class BasicEffect;
    class GraphicsDevice;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Draws wireframe shapes for looking at what the engine layer is doing.
     *
     * plan_modern.md `MOD-2160`. Nothing in this layer could draw a debug shape before, and the gap
     * was felt directly: every frustum, probe grid, cluster slice and light bound built in Phase 20
     * was verified by arithmetic, because there was no way to look at one.
     *
     * Everything submitted between @ref begin and @ref end is **accumulated into one vertex list and
     * drawn in a single call** — two, when both depth modes are used. That is not an optimisation
     * detail; a debug helper that costs a draw call per line is one nobody leaves switched on, and a
     * helper only used when someone remembers to switch it on is not there when it is needed.
     *
     * ```cpp
     * debug.begin(view, projection);
     * debug.addBox(bounds, Color::Yellow);
     * debug.setDepthTested(false);          // from here on, drawn through geometry
     * debug.addFrustum(lightFrustum, Color::Cyan);
     * debug.end();
     * ```
     *
     * It draws lines and nothing else. A solid shape would need a fill rule, a winding convention
     * and a lighting decision, and none of those help answer "is this box where I think it is".
     */
    class DebugDraw final
    {
    public:
        /**
         * @brief Creates the helper and its effect.
         *
         * @param device The device to draw with.
         */
        explicit DebugDraw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the helper and its resources. */
        ~DebugDraw();

        /** @brief Not copyable: it owns device resources. */
        DebugDraw(const DebugDraw&) = delete;
        /** @brief Not copy-assignable: it owns device resources. */
        DebugDraw& operator=(const DebugDraw&) = delete;

        /**
         * @brief Starts a batch and forgets whatever the last one held.
         *
         * @param view       The camera's view matrix.
         * @param projection The camera's projection matrix.
         */
        void begin(const Microsoft::Xna::Framework::Matrix& view,
                   const Microsoft::Xna::Framework::Matrix& projection);

        /**
         * @brief Draws everything submitted since @ref begin and closes the batch.
         *
         * One draw call per depth mode actually used, so a batch with no overlay shapes in it costs
         * exactly one. Restores the depth state it found.
         */
        void end();

        /** @brief Forgets every submitted shape without drawing, leaving the batch open. */
        void clear();

        /**
         * @brief Adds one line segment.
         *
         * @param from   The first endpoint, in world space.
         * @param to     The second endpoint, in world space.
         * @param colour The line's colour.
         */
        void addLine(const Microsoft::Xna::Framework::Vector3& from,
                     const Microsoft::Xna::Framework::Vector3& to,
                     const Microsoft::Xna::Framework::Color& colour);

        /**
         * @brief Adds the twelve edges of an axis-aligned box.
         *
         * @param bounds The box, in world space.
         * @param colour The colour to draw it in.
         */
        void addBox(const Microsoft::Xna::Framework::BoundingBox& bounds,
                    const Microsoft::Xna::Framework::Color& colour);

        /**
         * @brief Adds three great circles of a sphere, one per axis plane.
         *
         * Three rings rather than a mesh: a wireframe ball is unreadable at any useful line count,
         * while three rings read immediately as a sphere and cost 3 × `segments` lines.
         *
         * @param centre   The sphere's centre, in world space.
         * @param radius   The sphere's radius.
         * @param colour   The colour to draw it in.
         * @param segments Lines per ring; clamped to 4..128.
         */
        void addSphere(const Microsoft::Xna::Framework::Vector3& centre, float radius,
                       const Microsoft::Xna::Framework::Color& colour, int segments = 24);

        /**
         * @brief Adds three great circles of a bounding sphere.
         *
         * @param sphere   The sphere, in world space.
         * @param colour   The colour to draw it in.
         * @param segments Lines per ring; clamped to 4..128.
         */
        void addSphere(const Microsoft::Xna::Framework::BoundingSphere& sphere,
                       const Microsoft::Xna::Framework::Color& colour, int segments = 24);

        /**
         * @brief Adds the twelve edges of a frustum.
         *
         * The shape this helper exists for. A shadow map's fitted volume, a cascade's slice and a
         * culler's test are all frusta, and all three were previously checkable only by arithmetic.
         *
         * @param frustum The frustum, in world space.
         * @param colour  The colour to draw it in.
         */
        void addFrustum(const Microsoft::Xna::Framework::BoundingFrustum& frustum,
                        const Microsoft::Xna::Framework::Color& colour);

        /**
         * @brief Adds three axis-aligned lines through a point.
         *
         * @param position The point, in world space.
         * @param size     Half the length of each arm.
         * @param colour   The colour to draw it in.
         */
        void addCross(const Microsoft::Xna::Framework::Vector3& position, float size,
                      const Microsoft::Xna::Framework::Color& colour);

        /**
         * @brief Whether shapes submitted from now on are hidden behind geometry.
         *
         * @return True when subsequent submissions are depth-tested.
         */
        [[nodiscard]] bool isDepthTested() const;

        /**
         * @brief Sets whether shapes submitted from now on are hidden behind geometry.
         *
         * plan_modern.md `MOD-2162`, and it is set **per submission** rather than per batch because
         * the two modes answer different questions in the same frame. Depth-tested tells you where
         * a shape is *relative to the scene* — whether the light's volume really does contain the
         * pillar. Overlay tells you where a shape is *at all*, which is the only way to find one
         * that turned out to be behind the camera or inside the floor.
         *
         * Overlay shapes are drawn after the depth-tested ones, so an overlay line crossing a
         * depth-tested one wins.
         *
         * @param value True to depth-test, false to draw through.
         */
        void setDepthTested(bool value);

        /** @brief Returns how many line segments are waiting in the current batch. */
        [[nodiscard]] int getLineCount() const;

        /**
         * @brief Returns the accumulated vertices for one depth mode.
         *
         * Exposed so a test can assert what a shape *is* — that a box really is twelve edges joining
         * the eight corners `BoundingBox::GetCorners` returns — without a renderer that can read a
         * frame back. Two consecutive entries are one line.
         *
         * @param depthTested Which of the two lists to return.
         * @return The vertices, in submission order.
         */
        [[nodiscard]] const
        std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColor>&
        getVertices(bool depthTested) const;

    private:
        void addLineTo(std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColor>& list,
                       const Microsoft::Xna::Framework::Vector3& from,
                       const Microsoft::Xna::Framework::Vector3& to,
                       const Microsoft::Xna::Framework::Color& colour);
        void drawList(
            const std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColor>& list,
            bool depthTested);

        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> effect_;

        std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColor> depthTestedLines_;
        std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColor> overlayLines_;
        bool depthTested_ = true;
        bool open_        = false;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
