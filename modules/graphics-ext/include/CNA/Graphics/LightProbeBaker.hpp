// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/LightProbeEXT.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <functional>
#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class RenderTarget2D;
}

namespace CNA::Graphics {

    class LightProbeVolumeEXT;

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Captures light probes by rendering the scene from where they stand.
     *
     * **The application draws the scene; the layer owns the capture and the projection.** That is
     * the same division shadow maps and the depth prepass already use, and for the same reason: the
     * layer has no idea what a scene *is*. It hands out a view and a projection, the game draws
     * whatever it wants seen, and the six results become nine coefficients.
     *
     * ```cpp
     * CNA::Graphics::LightProbeBaker baker(device);
     * baker.bakeLight(volume, [&](const Matrix& view, const Matrix& projection) {
     *     drawEverythingStatic(view, projection);
     * });
     * ```
     *
     * **The directions come from the view matrices themselves**, not from a cube-map convention.
     * Each pixel's world direction is reconstructed from the camera basis the baker built, so there
     * is no face layout to agree with and no handedness to get wrong -- the one class of bug a
     * cube capture usually ships with.
     *
     * Baking is an *offline* operation in the sense that matters: it costs six full scene draws per
     * probe, so a modest 8×4×8 grid is 1536 draws. It is not something to do in a frame.
     */
    class LightProbeBaker
    {
    public:
        /** @brief Edge length of each captured face by default. */
        static constexpr int kDefaultFaceSize = 32;

        /**
         * @brief What the baker asks the application to draw.
         *
         * @param view       The camera looking along one face direction from the probe.
         * @param projection A 90-degree square perspective, so the six faces tile the sphere.
         */
        using SceneDraw = std::function<void(const Microsoft::Xna::Framework::Matrix& view,
                                             const Microsoft::Xna::Framework::Matrix& projection)>;

        /**
         * @brief Creates a baker.
         *
         * @param device   The device to capture on.
         * @param faceSize Edge length of each captured face; must be positive. Irradiance is a very
         *                 low-frequency signal, so this is small on purpose -- a bigger capture
         *                 buys almost nothing and costs six times its area per probe.
         * @throws std::invalid_argument When the face size is not positive.
         */
        explicit LightProbeBaker(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                                 int faceSize = kDefaultFaceSize);

        /** @brief Destroys the baker and its capture target. */
        ~LightProbeBaker();

        LightProbeBaker(const LightProbeBaker&)            = delete;
        LightProbeBaker& operator=(const LightProbeBaker&) = delete;

        /** @brief Returns whether this renderer can capture at all. */
        [[nodiscard]] bool isSupported() const;

        /** @brief Returns the edge length of each captured face. */
        [[nodiscard]] int getFaceSize() const;

        /** @brief Returns how many faces a single probe costs; six, one per direction. */
        [[nodiscard]] static int getFaceCount();

        /**
         * @brief Returns the near plane the capture projection uses.
         *
         * Exposed because a scene drawn with a different near plane than the capture assumes clips
         * differently, and a probe that captured a clipped scene is wrong in a way nothing else
         * reports.
         */
        [[nodiscard]] float getNearPlane() const;

        /** @brief Returns the far plane the capture projection uses. */
        [[nodiscard]] float getFarPlane() const;

        /**
         * @brief Sets the capture projection's near and far planes.
         *
         * @param nearPlane Must be positive.
         * @param farPlane  Must exceed @p nearPlane.
         * @throws std::invalid_argument When the pair is not positive and increasing.
         */
        void setPlanes(float nearPlane, float farPlane);

        /**
         * @brief Captures one probe by drawing the scene six times.
         *
         * @param position The world-space point to capture from.
         * @param draw     What to render; called once per face.
         * @return The probe, with its position set and its coefficients projected.
         * @throws std::runtime_error When this renderer cannot capture.
         */
        [[nodiscard]] LightProbeEXT bakeProbe(
            const Microsoft::Xna::Framework::Vector3& position, const SceneDraw& draw);

        /**
         * @brief Captures every probe in a volume, in place.
         *
         * Each probe keeps the grid position the volume gave it; only its light changes.
         *
         * @param volume The volume to fill.
         * @param draw   What to render; called six times per probe.
         * @throws std::runtime_error When this renderer cannot capture.
         */
        void bakeLight(LightProbeVolumeEXT& volume, const SceneDraw& draw);

        /**
         * @brief Records how far the geometry is around every probe, for `MOD-2083`'s leak test.
         *
         * A second pass, and separate on purpose: it needs the scene drawn as **distance from the
         * probe**, which is a different shader from the one that draws it lit, and only the
         * application knows how to produce either. The layer reduces the six captures to the two
         * moments per direction the visibility test wants.
         *
         * The captured value is read from the red channel and taken as a fraction of
         * @ref getFarPlane, which is the convention `DepthEffect` already writes distance in.
         *
         * @param volume The volume whose probes gain visibility.
         * @param draw   What to render; it must write normalised distance from the camera.
         * @throws std::runtime_error When this renderer cannot capture.
         */
        void bakeVisibility(LightProbeVolumeEXT& volume, const SceneDraw& draw);

        /**
         * @brief Returns the view matrix the baker uses for one face.
         *
         * Exposed so a test -- or an application doing its own capture -- can reproduce exactly
         * what the baker asked for.
         *
         * @param face     0 to 5.
         * @param position The probe's world-space position.
         * @return The view matrix.
         * @throws std::out_of_range When the face is outside 0 to 5.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Matrix faceView(
            int face, const Microsoft::Xna::Framework::Vector3& position);

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> capture_;
        int   faceSize_;
        float nearPlane_ = 0.05f;
        float farPlane_  = 500.0f;
        bool  supported_ = false;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
