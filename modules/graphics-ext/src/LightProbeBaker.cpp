// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/LightProbeBaker.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/LightProbeVolumeEXT.hpp"
#include "CNA/Graphics/ScopedRenderTarget.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;

    namespace {

        constexpr float kHalfPi = 1.57079632679f;

        /// The six directions, and an up vector that is never parallel to any of them.
        struct FaceAxis { Vector3 Forward; Vector3 Up; };

        const std::array<FaceAxis, 6>& FaceAxes()
        {
            static const std::array<FaceAxis, 6> axes = {
                FaceAxis{Vector3( 1.0f,  0.0f,  0.0f), Vector3(0.0f, 1.0f, 0.0f)},
                FaceAxis{Vector3(-1.0f,  0.0f,  0.0f), Vector3(0.0f, 1.0f, 0.0f)},
                FaceAxis{Vector3( 0.0f,  1.0f,  0.0f), Vector3(0.0f, 0.0f, 1.0f)},
                FaceAxis{Vector3( 0.0f, -1.0f,  0.0f), Vector3(0.0f, 0.0f, 1.0f)},
                FaceAxis{Vector3( 0.0f,  0.0f,  1.0f), Vector3(0.0f, 1.0f, 0.0f)},
                FaceAxis{Vector3( 0.0f,  0.0f, -1.0f), Vector3(0.0f, 1.0f, 0.0f)},
            };
            return axes;
        }

        /// The world direction one pixel of a captured face looks along, taken from the camera
        /// basis the view matrix carries. Reconstructing it this way rather than from a cube-map
        /// face layout is what removes the whole class of handedness bugs a cube capture usually
        /// ships with: there is no convention to agree with, only the matrix that was used.
        Vector3 DirectionOf(const Matrix& view, const float ndcX, const float ndcY)
        {
            // For a row-vector view matrix, the columns of the upper 3x3 are the camera's own axes
            // expressed in world space.
            const Vector3 right(view.M11, view.M21, view.M31);
            const Vector3 up(view.M12, view.M22, view.M32);
            const Vector3 backward(view.M13, view.M23, view.M33);

            // A 90-degree square perspective puts tan(45) = 1 at the edge, so the NDC coordinates
            // are the tangent offsets directly.
            const Vector3 direction(ndcX * right.X + ndcY * up.X - backward.X,
                                    ndcX * right.Y + ndcY * up.Y - backward.Y,
                                    ndcX * right.Z + ndcY * up.Z - backward.Z);
            const float length = std::sqrt(direction.X * direction.X + direction.Y * direction.Y +
                                           direction.Z * direction.Z);
            if (!(length > 1e-8f)) return Vector3(0.0f, 0.0f, 1.0f);
            return Vector3(direction.X / length, direction.Y / length, direction.Z / length);
        }

        /// The solid angle one pixel of a 90-degree face subtends, in the same form the cube
        /// projection uses: texels away from a face's centre are foreshortened, and weighting them
        /// equally tilts every probe towards its own corners.
        float SolidAngleOf(const float ndcX, const float ndcY, const float size)
        {
            const float lengthSquared = ndcX * ndcX + ndcY * ndcY + 1.0f;
            return (4.0f / (size * size)) / (lengthSquared * std::sqrt(lengthSquared));
        }

    } // namespace

    LightProbeBaker::LightProbeBaker(GraphicsDevice& device, const int faceSize)
        : device_(device), faceSize_(faceSize)
    {
        if (faceSize <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::LightProbeBaker: the face size must be positive");

        // Probed rather than asked. Neither "can bind an offscreen target" nor "can read one back"
        // is a capability a renderer publishes -- the Headless renderer binds happily and refuses
        // only the read -- so the only answer that cannot drift from the truth is doing it once.
        try
        {
            // Probed on a one-pixel target rather than on the capture itself: GetData wants the
            // whole surface, so reading one texel of a large target is refused for a reason that
            // has nothing to do with whether the renderer can read targets at all.
            RenderTarget2D probeTarget(device, 1, 1);
            device.SetRenderTarget(&probeTarget);
            device.Clear(Color::Black);
            device.SetRenderTarget(nullptr);

            Color probe = Color::White;
            probeTarget.GetData(&probe, 1);

            capture_ = std::make_unique<RenderTarget2D>(device, faceSize, faceSize);
            supported_ = true;
        }
        catch (...)
        {
            try { device.SetRenderTarget(nullptr); }
            catch (...) { /* best effort; the answer is already decided */ }
            capture_.reset();
            supported_ = false;
        }
    }

    LightProbeBaker::~LightProbeBaker() = default;

    bool  LightProbeBaker::isSupported() const { return supported_; }
    int   LightProbeBaker::getFaceSize() const { return faceSize_; }
    int   LightProbeBaker::getFaceCount() { return 6; }
    float LightProbeBaker::getNearPlane() const { return nearPlane_; }
    float LightProbeBaker::getFarPlane()  const { return farPlane_; }

    void LightProbeBaker::setPlanes(const float nearPlane, const float farPlane)
    {
        if (!(nearPlane > 0.0f) || !(farPlane > nearPlane))
            throw std::invalid_argument(
                "CNA::Graphics::LightProbeBaker::setPlanes: the near distance must be positive and "
                "the far distance must exceed it");
        nearPlane_ = nearPlane;
        farPlane_ = farPlane;
    }

    Matrix LightProbeBaker::faceView(const int face, const Vector3& position)
    {
        if (face < 0 || face >= 6)
            throw std::out_of_range("CNA::Graphics::LightProbeBaker::faceView: a capture has six "
                                    "faces");
        const FaceAxis& axis = FaceAxes()[static_cast<std::size_t>(face)];
        return Matrix::CreateLookAt(position,
                                    Vector3(position.X + axis.Forward.X,
                                            position.Y + axis.Forward.Y,
                                            position.Z + axis.Forward.Z),
                                    axis.Up);
    }

    LightProbeEXT LightProbeBaker::bakeProbe(const Vector3& position, const SceneDraw& draw)
    {
        if (!supported_)
            throw std::runtime_error(
                "CNA::Graphics::LightProbeBaker::bakeProbe: this renderer cannot render to a target "
                "and read it back, so there is nothing to capture with");

        const Matrix projection =
            Matrix::CreatePerspectiveFieldOfView(kHalfPi, 1.0f, nearPlane_, farPlane_);
        const float size = static_cast<float>(faceSize_);

        std::array<Vector3, LightProbeEXT::kCoefficientCount> sums{};
        std::vector<Color> pixels(static_cast<std::size_t>(faceSize_) * faceSize_, Color::Black);

        for (int face = 0; face < 6; ++face)
        {
            const Matrix view = faceView(face, position);
            {
                ScopedRenderTarget bound(device_, capture_.get());
                device_.Clear(Color::Black);
                if (draw) draw(view, projection);
            }
            capture_->GetData(pixels.data(), static_cast<int>(pixels.size()));

            for (int y = 0; y < faceSize_; ++y)
                for (int x = 0; x < faceSize_; ++x)
                {
                    const float ndcX = 2.0f * (static_cast<float>(x) + 0.5f) / size - 1.0f;
                    const float ndcY = 1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / size;
                    const Vector3 direction = DirectionOf(view, ndcX, ndcY);
                    const float solidAngle = SolidAngleOf(ndcX, ndcY, size);

                    const Color& texel = pixels[static_cast<std::size_t>(y) * faceSize_ + x];
                    const Vector3 radiance(static_cast<float>(texel.getRProperty()) / 255.0f,
                                           static_cast<float>(texel.getGProperty()) / 255.0f,
                                           static_cast<float>(texel.getBProperty()) / 255.0f);

                    const float basis[LightProbeEXT::kCoefficientCount] = {
                        0.282095f,
                        0.488603f * direction.Y,
                        0.488603f * direction.Z,
                        0.488603f * direction.X,
                        1.092548f * direction.X * direction.Y,
                        1.092548f * direction.Y * direction.Z,
                        0.315392f * (3.0f * direction.Z * direction.Z - 1.0f),
                        1.092548f * direction.X * direction.Z,
                        0.546274f * (direction.X * direction.X - direction.Y * direction.Y),
                    };
                    for (int index = 0; index < LightProbeEXT::kCoefficientCount; ++index)
                    {
                        const float weight = basis[index] * solidAngle;
                        Vector3& sum = sums[static_cast<std::size_t>(index)];
                        sum = Vector3(sum.X + radiance.X * weight, sum.Y + radiance.Y * weight,
                                      sum.Z + radiance.Z * weight);
                    }
                }
        }

        LightProbeEXT probe(position);
        for (int index = 0; index < LightProbeEXT::kCoefficientCount; ++index)
            probe.setCoefficient(index, sums[static_cast<std::size_t>(index)]);
        return probe;
    }

    void LightProbeBaker::bakeLight(LightProbeVolumeEXT& volume, const SceneDraw& draw)
    {
        for (int z = 0; z < volume.getCountZ(); ++z)
            for (int y = 0; y < volume.getCountY(); ++y)
                for (int x = 0; x < volume.getCountX(); ++x)
                {
                    LightProbeEXT baked = bakeProbe(volume.getProbePosition(x, y, z), draw);
                    // Whatever visibility the probe already carried is kept: the two bakes are
                    // separate passes and either may be run without the other.
                    const LightProbeEXT& existing = volume.getProbe(x, y, z);
                    for (int direction = 0; direction < LightProbeEXT::kVisibilityDirections;
                         ++direction)
                        baked.setVisibility(direction, existing.getVisibilityMean(direction),
                                            existing.getVisibilityMeanSquared(direction));
                    volume.setProbe(x, y, z, baked);
                }
    }

    void LightProbeBaker::bakeVisibility(LightProbeVolumeEXT& volume, const SceneDraw& draw)
    {
        if (!supported_)
            throw std::runtime_error(
                "CNA::Graphics::LightProbeBaker::bakeVisibility: this renderer cannot render to a "
                "target and read it back");

        const Matrix projection =
            Matrix::CreatePerspectiveFieldOfView(kHalfPi, 1.0f, nearPlane_, farPlane_);
        const float size = static_cast<float>(faceSize_);
        std::vector<Color> pixels(static_cast<std::size_t>(faceSize_) * faceSize_, Color::Black);

        for (int z = 0; z < volume.getCountZ(); ++z)
            for (int y = 0; y < volume.getCountY(); ++y)
                for (int x = 0; x < volume.getCountX(); ++x)
                {
                    LightProbeEXT probe = volume.getProbe(x, y, z);
                    const Vector3 position = volume.getProbePosition(x, y, z);

                    for (int face = 0; face < 6; ++face)
                    {
                        const Matrix view = faceView(face, position);
                        {
                            ScopedRenderTarget bound(device_, capture_.get());
                            // White is "nothing here": an unwritten pixel has to read as the far
                            // plane, or every direction the scene does not cover would record a
                            // wall at the camera and reject every probe in the volume.
                            device_.Clear(Color::White);
                            if (draw) draw(view, projection);
                        }
                        capture_->GetData(pixels.data(), static_cast<int>(pixels.size()));

                        double total = 0.0;
                        double mean = 0.0;
                        double meanSquared = 0.0;
                        for (int py = 0; py < faceSize_; ++py)
                            for (int px = 0; px < faceSize_; ++px)
                            {
                                const float ndcX = 2.0f * (static_cast<float>(px) + 0.5f) / size
                                                 - 1.0f;
                                const float ndcY = 1.0f - 2.0f * (static_cast<float>(py) + 0.5f)
                                                 / size;
                                const float solidAngle = SolidAngleOf(ndcX, ndcY, size);
                                const Color& texel =
                                    pixels[static_cast<std::size_t>(py) * faceSize_ + px];
                                const double distance =
                                    static_cast<double>(texel.getRProperty()) / 255.0 * farPlane_;
                                total += solidAngle;
                                mean += distance * solidAngle;
                                meanSquared += distance * distance * solidAngle;
                            }

                        if (total > 0.0)
                            probe.setVisibility(face, static_cast<float>(mean / total),
                                                static_cast<float>(meanSquared / total));
                    }
                    volume.setProbe(x, y, z, probe);
                }
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
