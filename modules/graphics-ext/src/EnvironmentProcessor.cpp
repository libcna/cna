// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/EnvironmentProcessor.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::MathHelper;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::CubeMapFace;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::TextureCube;

    EnvironmentProcessor::EnvironmentProcessor(GraphicsDevice& device) : device_(device) {}

    EnvironmentProcessor::~EnvironmentProcessor() = default;

    Vector3 EnvironmentProcessor::faceDirection(const int face, const float u, const float v)
    {
        // Face-local coordinates in -1..1, with v running *down* the face -- which is what the
        // cube-map convention wants and the opposite of what a texture coordinate usually means.
        const float a = u * 2.0f - 1.0f;
        const float b = 1.0f - v * 2.0f;

        Vector3 direction(0.0f, 0.0f, 0.0f);
        switch (face)
        {
        case 0: direction = Vector3( 1.0f,    b,   -a); break;   // +X
        case 1: direction = Vector3(-1.0f,    b,    a); break;   // -X
        case 2: direction = Vector3(    a, 1.0f,   -b); break;   // +Y
        case 3: direction = Vector3(    a, -1.0f,   b); break;   // -Y
        case 4: direction = Vector3(    a,    b, 1.0f); break;   // +Z
        default: direction = Vector3(  -a,    b, -1.0f); break;  // -Z
        }

        const float length = std::sqrt(direction.X * direction.X + direction.Y * direction.Y +
                                       direction.Z * direction.Z);
        return Vector3(direction.X / length, direction.Y / length, direction.Z / length);
    }

    void EnvironmentProcessor::directionToEquirectangular(const Vector3& direction, float& u,
                                                           float& v)
    {
        const float length = std::sqrt(direction.X * direction.X + direction.Y * direction.Y +
                                       direction.Z * direction.Z);
        const float inverse = length > 1e-9f ? 1.0f / length : 1.0f;
        const float x = direction.X * inverse;
        const float y = direction.Y * inverse;
        const float z = direction.Z * inverse;

        // atan2(x, -z) puts -Z at the centre of the image, which is where a panorama's "front" is
        // and where a viewer looking straight ahead at load time expects to be looking.
        const float longitude = std::atan2(x, -z);
        const float latitude  = std::asin(std::clamp(y, -1.0f, 1.0f));

        u = longitude / (2.0f * MathHelper::Pi) + 0.5f;
        v = 0.5f - latitude / MathHelper::Pi;
    }

    std::unique_ptr<TextureCube> EnvironmentProcessor::convertEquirectangular(Texture2D* panorama,
                                                                              const int faceSize)
    {
        if (panorama == nullptr)
            throw std::invalid_argument(
                "CNA::Graphics::EnvironmentProcessor::convertEquirectangular: the panorama must "
                "not be null");
        if (faceSize <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::EnvironmentProcessor::convertEquirectangular: faceSize must be "
                "positive");

        const int width  = panorama->getWidthProperty();
        const int height = panorama->getHeightProperty();
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::EnvironmentProcessor::convertEquirectangular: the panorama has no "
                "pixels");

        std::vector<Color> source(static_cast<std::size_t>(width) * height, Color::Black);
        panorama->GetData(source.data(), static_cast<int>(source.size()));

        auto cube = std::make_unique<TextureCube>(device_, faceSize, false, SurfaceFormat::Color);
        std::vector<Color> face(static_cast<std::size_t>(faceSize) * faceSize, Color::Black);

        for (int faceIndex = 0; faceIndex < 6; ++faceIndex)
        {
            for (int y = 0; y < faceSize; ++y)
            {
                for (int x = 0; x < faceSize; ++x)
                {
                    // Texel centres, not corners: sampling at the corner shifts every face by half
                    // a texel and leaves a visible seam where two faces meet.
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(faceSize);
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(faceSize);

                    float su = 0.0f;
                    float sv = 0.0f;
                    directionToEquirectangular(faceDirection(faceIndex, u, v), su, sv);

                    // Nearest-neighbour, and wrapping in longitude rather than clamping: the
                    // panorama's left and right edges are the same meridian, so clamping there
                    // would smear a stripe down the seam of the sky.
                    int sx = static_cast<int>(su * static_cast<float>(width));
                    sx = ((sx % width) + width) % width;
                    int sy = static_cast<int>(sv * static_cast<float>(height));
                    sy = std::clamp(sy, 0, height - 1);

                    face[static_cast<std::size_t>(y) * faceSize + x] =
                        source[static_cast<std::size_t>(sy) * width + sx];
                }
            }
            cube->SetData(static_cast<CubeMapFace>(faceIndex), face.data(),
                          static_cast<int>(face.size()));
        }

        return cube;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
