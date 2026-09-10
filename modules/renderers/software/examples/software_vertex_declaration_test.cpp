// SPDX-License-Identifier: MS-PL
// SOFTWARE-108 / SOFTWARE-130 / SOFTWARE-320: renderer-independent public pixel contract for
// declaration-driven vertex input, compiled against both Software and EasyGL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr std::array<std::array<float, 3>, 6> kQuad{{
        {-1.0f,  1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},
        {-1.0f,  1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f},
    }};

    int FormatSize(VertexElementFormat format)
    {
        switch (format)
        {
            case VertexElementFormat::Single: return 4;
            case VertexElementFormat::Vector2: return 8;
            case VertexElementFormat::Vector3: return 12;
            case VertexElementFormat::Vector4: return 16;
            case VertexElementFormat::Color: return 4;
            case VertexElementFormat::Byte4: return 4;
            case VertexElementFormat::Short2: return 4;
            case VertexElementFormat::Short4: return 8;
            case VertexElementFormat::NormalizedShort2: return 4;
            case VertexElementFormat::NormalizedShort4: return 8;
            case VertexElementFormat::HalfVector2: return 4;
            case VertexElementFormat::HalfVector4: return 8;
        }
        return 0;
    }

    void StoreRed(std::uint8_t* destination, VertexElementFormat format)
    {
        const float floats[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        const std::int16_t shorts[4] = {1, 0, 0, 1};
        const std::int16_t normalized[4] = {32767, 0, 0, 32767};
        const std::uint16_t halves[4] = {
            PackedVector::HalfTypeHelper::Convert(1.0f),
            PackedVector::HalfTypeHelper::Convert(0.0f),
            PackedVector::HalfTypeHelper::Convert(0.0f),
            PackedVector::HalfTypeHelper::Convert(1.0f),
        };
        switch (format)
        {
            case VertexElementFormat::Single:
                std::memcpy(destination, floats, 4); break;
            case VertexElementFormat::Vector2:
                std::memcpy(destination, floats, 8); break;
            case VertexElementFormat::Vector3:
                std::memcpy(destination, floats, 12); break;
            case VertexElementFormat::Vector4:
                std::memcpy(destination, floats, 16); break;
            case VertexElementFormat::Color:
                destination[0] = 255; destination[1] = 0;
                destination[2] = 0; destination[3] = 255; break;
            case VertexElementFormat::Byte4:
                destination[0] = 1; destination[1] = 0;
                destination[2] = 0; destination[3] = 1; break;
            case VertexElementFormat::Short2:
                std::memcpy(destination, shorts, 4); break;
            case VertexElementFormat::Short4:
                std::memcpy(destination, shorts, 8); break;
            case VertexElementFormat::NormalizedShort2:
                std::memcpy(destination, normalized, 4); break;
            case VertexElementFormat::NormalizedShort4:
                std::memcpy(destination, normalized, 8); break;
            case VertexElementFormat::HalfVector2:
                std::memcpy(destination, halves, 4); break;
            case VertexElementFormat::HalfVector4:
                std::memcpy(destination, halves, 8); break;
        }
    }

    const char* FormatName(VertexElementFormat format)
    {
        static constexpr const char* names[] = {
            "Single", "Vector2", "Vector3", "Vector4", "Color", "Byte4",
            "Short2", "Short4", "NormalizedShort2", "NormalizedShort4",
            "HalfVector2", "HalfVector4",
        };
        return names[static_cast<int>(format)];
    }
}

class VertexDeclarationFormatContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    bool done_ = false;
    int passed_ = 0;
    int failed_ = 0;

    Color ReadCenter(GraphicsDevice& device)
    {
        const Viewport viewport = device.getViewportProperty();
        const Rectangle rectangle(viewport.getWidthProperty() / 2,
                                  viewport.getHeightProperty() / 2, 1, 1);
        Color pixel;
        device.GetBackBufferData(&rectangle, &pixel, 0, 1);
        return pixel;
    }

    void CheckRed(GraphicsDevice& device, const char* label)
    {
        const Color pixel = ReadCenter(device);
        const bool red = pixel.getRProperty() >= 250 && pixel.getGProperty() <= 2 &&
                         pixel.getBProperty() <= 2;
        std::printf("[%s] %s: centre=(%d,%d,%d,%d)\n", red ? "PASS" : "FAIL", label,
                    pixel.getRProperty(), pixel.getGProperty(), pixel.getBProperty(),
                    pixel.getAProperty());
        red ? ++passed_ : ++failed_;
    }

    void Prepare(GraphicsDevice& device, BasicEffect& effect)
    {
        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        effect.VertexColorEnabled = true;
        effect.Apply();
    }

    void TestEveryFormat(GraphicsDevice& device, BasicEffect& effect)
    {
        for (int ordinal = static_cast<int>(VertexElementFormat::Single);
             ordinal <= static_cast<int>(VertexElementFormat::HalfVector4); ++ordinal)
        {
            const auto format = static_cast<VertexElementFormat>(ordinal);
            const int colorSize = FormatSize(format);
            const int stride = colorSize + 12;
            std::vector<std::uint8_t> vertices(static_cast<std::size_t>(stride) * kQuad.size());
            for (std::size_t i = 0; i < kQuad.size(); ++i)
            {
                std::uint8_t* record = vertices.data() + i * static_cast<std::size_t>(stride);
                StoreRed(record, format);
                std::memcpy(record + colorSize, kQuad[i].data(), 12);
            }
            const VertexDeclaration declaration(stride, {
                VertexElement(colorSize, VertexElementFormat::Vector3,
                              VertexElementUsage::Position, 0),
                VertexElement(0, format, VertexElementUsage::Color, 0),
            });
            Prepare(device, effect);
            device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 2,
                                      declaration);
            CheckRed(device, FormatName(format));
        }
    }

    struct ReorderedVertex
    {
        std::uint8_t color[4];
        float position[3];
    };

    void TestBoundOffset(GraphicsDevice& device, BasicEffect& effect)
    {
        std::array<ReorderedVertex, 7> vertices{};
        for (std::size_t i = 0; i < kQuad.size(); ++i)
        {
            vertices[i + 1].color[0] = 255;
            vertices[i + 1].color[3] = 255;
            std::memcpy(vertices[i + 1].position, kQuad[i].data(), 12);
        }
        const VertexDeclaration declaration(sizeof(ReorderedVertex), {
            VertexElement(4, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
        VertexBuffer buffer(device, declaration, 7, BufferUsage::None);
        buffer.SetData(vertices.data(), static_cast<int>(vertices.size()));
        device.SetVertexBuffers({VertexBufferBinding(&buffer, 1)});
        Prepare(device, effect);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        CheckRed(device, "bound reordered declaration + VertexBufferBinding offset");
        device.SetVertexBuffers({});
    }

    template<typename TIndex>
    void TestUserIndexed(GraphicsDevice& device, BasicEffect& effect, const char* label)
    {
        const ReorderedVertex vertices[5] = {
            {{0, 255, 0, 255}, {-1.0f,  1.0f, 0.0f}},
            {{255, 0, 0, 255}, {-1.0f,  1.0f, 0.0f}},
            {{255, 0, 0, 255}, {-1.0f, -1.0f, 0.0f}},
            {{255, 0, 0, 255}, { 1.0f, -1.0f, 0.0f}},
            {{255, 0, 0, 255}, { 1.0f,  1.0f, 0.0f}},
        };
        const TIndex indices[7] = {0, 0, 1, 2, 0, 2, 3};
        const VertexDeclaration declaration(sizeof(ReorderedVertex), {
            VertexElement(4, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
        Prepare(device, effect);
        device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList,
                                         vertices, 1, 4, indices, 1, 2, declaration);
        CheckRed(device, label);
    }

    void TestMultipleStreams(GraphicsDevice& device, BasicEffect& effect)
    {
        struct Position { float xyz[3]; };
        struct ColorBytes { std::uint8_t rgba[4]; };
        std::array<Position, 6> positions{};
        std::array<ColorBytes, 6> colors{};
        for (std::size_t i = 0; i < kQuad.size(); ++i)
        {
            std::memcpy(positions[i].xyz, kQuad[i].data(), 12);
            colors[i].rgba[0] = 255;
            colors[i].rgba[3] = 255;
        }
        const VertexDeclaration positionDeclaration(sizeof(Position), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        const VertexDeclaration colorDeclaration(sizeof(ColorBytes), {
            VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
        VertexBuffer positionBuffer(device, positionDeclaration, 6, BufferUsage::None);
        VertexBuffer colorBuffer(device, colorDeclaration, 6, BufferUsage::None);
        positionBuffer.SetData(positions.data(), 6);
        colorBuffer.SetData(colors.data(), 6);
        device.SetVertexBuffers({VertexBufferBinding(&positionBuffer),
                                 VertexBufferBinding(&colorBuffer)});
        Prepare(device, effect);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        CheckRed(device, "Position0 and Color0 in independent streams");
        device.SetVertexBuffers({});
    }

    struct PositionColorVertex
    {
        float position[3];
        std::uint8_t color[4];
    };

    struct PositionTextureVertex
    {
        float position[3];
        float textureCoordinate[2];
    };

    void TestPartialCrossStreamUsageCollision(GraphicsDevice& device, BasicEffect& effect)
    {
        std::array<PositionColorVertex, 6> primary{};
        std::array<PositionTextureVertex, 6> secondary{};
        for (std::size_t i = 0; i < kQuad.size(); ++i)
        {
            std::memcpy(primary[i].position, kQuad[i].data(), 12);
            std::fill(std::begin(primary[i].color), std::end(primary[i].color), 255);
            secondary[i].textureCoordinate[0] = 0.75f;
            secondary[i].textureCoordinate[1] = 0.75f;
        }

        const VertexDeclaration primaryDeclaration(sizeof(PositionColorVertex), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
        const VertexDeclaration secondaryDeclaration(sizeof(PositionTextureVertex), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        VertexBuffer primaryBuffer(device, primaryDeclaration, 6, BufferUsage::None);
        VertexBuffer secondaryBuffer(device, secondaryDeclaration, 6, BufferUsage::None);
        primaryBuffer.SetData(primary.data(), 6);
        secondaryBuffer.SetData(secondary.data(), 6);

        Texture2D texture(device, 2, 2);
        const Color pixels[4] = {Color::Green, Color::Green, Color::Green, Color::Red};
        texture.SetData(pixels, 4);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        effect.setTextureEnabledProperty(true);
        effect.setTextureProperty(&texture);
        device.SetVertexBuffers({VertexBufferBinding(&primaryBuffer),
                                 VertexBufferBinding(&secondaryBuffer)});
        Prepare(device, effect);
        try
        {
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            CheckRed(device, "partial cross-stream usage collision retains unique TexCoord0");
        }
        catch (const std::exception& exception)
        {
            std::printf("[FAIL] partial cross-stream usage collision threw: %s\n",
                        exception.what());
            ++failed_;
        }
        device.SetVertexBuffers({});
        effect.setTextureProperty(nullptr);
        effect.setTextureEnabledProperty(false);
    }

    struct PositionTexture0Vertex
    {
        float position[3];
        float textureCoordinate[2];
    };

    struct CollidingTextureVertex
    {
        float textureCoordinate[2];
    };

    void TestCompleteCrossStreamUsageCollision(GraphicsDevice& device)
    {
        std::array<PositionTexture0Vertex, 6> primary{};
        std::array<CollidingTextureVertex, 6> secondary{};
        for (std::size_t i = 0; i < kQuad.size(); ++i)
        {
            std::memcpy(primary[i].position, kQuad[i].data(), 12);
            secondary[i].textureCoordinate[0] = 0.75f;
            secondary[i].textureCoordinate[1] = 0.75f;
        }

        const VertexDeclaration primaryDeclaration(sizeof(PositionTexture0Vertex), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const VertexDeclaration secondaryDeclaration(sizeof(CollidingTextureVertex), {
            VertexElement(0, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        VertexBuffer primaryBuffer(device, primaryDeclaration, 6, BufferUsage::None);
        VertexBuffer secondaryBuffer(device, secondaryDeclaration, 6, BufferUsage::None);
        primaryBuffer.SetData(primary.data(), 6);
        secondaryBuffer.SetData(secondary.data(), 6);

        Texture2D white(device, 1, 1);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        Texture2D pattern(device, 2, 2);
        const Color patternPixels[4] = {
            Color::Green, Color::Green, Color::Green, Color::Red,
        };
        pattern.SetData(patternPixels, 4);

        DualTextureEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setTextureProperty(&white);
        effect.setTexture2Property(&pattern);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
        device.SetVertexBuffers({VertexBufferBinding(&primaryBuffer),
                                 VertexBufferBinding(&secondaryBuffer)});
        device.Clear(Color::Green);
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        effect.Apply();
        try
        {
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            CheckRed(device, "complete cross-stream collision remaps TexCoord0 to TexCoord1");
        }
        catch (const std::exception& exception)
        {
            std::printf("[FAIL] complete cross-stream usage collision threw: %s\n",
                        exception.what());
            ++failed_;
        }
        device.SetVertexBuffers({});
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        BasicEffect effect(device);
        TestEveryFormat(device, effect);
        TestBoundOffset(device, effect);
        TestUserIndexed<std::uint16_t>(device, effect,
                                       "reordered user-indexed declaration (16-bit)");
        TestUserIndexed<std::uint32_t>(device, effect,
                                       "reordered user-indexed declaration (32-bit)");
        TestMultipleStreams(device, effect);
        TestPartialCrossStreamUsageCollision(device, effect);
        TestCompleteCrossStreamUsageCollision(device);
        std::printf("=== %d/%d PASS ===\n", passed_, passed_ + failed_);
        Exit();
    }

public:
    VertexDeclarationFormatContractTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        // HalfVector2/HalfVector4 vertex inputs are XNA HiDef formats. This exhaustive format
        // contract intentionally exercises them, so request the profile that owns them.
        manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
    }

    int Result() const { return failed_ == 0 ? 0 : 1; }
};

int main()
{
    VertexDeclarationFormatContractTest game;
    game.Run();
    return game.Result();
}
