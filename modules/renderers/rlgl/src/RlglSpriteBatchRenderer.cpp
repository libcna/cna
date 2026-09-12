// SPDX-License-Identifier: MS-PL

#include "RlglResources.hpp"

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include "RlglBridge.hpp"

#if defined(CNA_RLGL_COMPILED_EFFECTS)
#include "CNA/Internal/Renderers/Rlgl/RlglCompiledEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "Fna3dStockEffectBlobs.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Renderers::Rlgl
{
    namespace Detail
    {
        using Microsoft::Xna::Framework::Color;
        using Microsoft::Xna::Framework::Matrix;
        using Microsoft::Xna::Framework::Rectangle;
        using Microsoft::Xna::Framework::Vector2;
        using Microsoft::Xna::Framework::Graphics::Effect;
        using Microsoft::Xna::Framework::Graphics::SpriteEffects;

        constexpr int kFloatsPerVertex = 8;
        constexpr int kMaximumSpritesPerGroup = 16383;
        constexpr int kVertexCapacity = kMaximumSpritesPerGroup * 4;
        constexpr int kIndexCapacity = kMaximumSpritesPerGroup * 6;

        class RlglSpriteBatchRenderer final : public ISpriteBatchRenderer
        {
        public:
            explicit RlglSpriteBatchRenderer(RlglRenderer& renderer)
                : renderer_(renderer)
                , pipeline_(Bridge::CreateSpritePipeline(
                      kVertexCapacity, kIndexCapacity))
            {
                try
                {
                    vertices_.reserve(
                        static_cast<std::size_t>(kVertexCapacity) * kFloatsPerVertex);
                    indices_.reserve(kIndexCapacity);
#if defined(CNA_RLGL_COMPILED_EFFECTS)
                    const auto& bytes =
                        CNA::Internal::Renderers::Fna3d::StockEffectBlobs::kSpriteEffectFxb;
                    spriteCompiledEffect_ = std::make_unique<RlglCompiledEffect>(
                        renderer_, bytes, sizeof(bytes));
                    const auto& parameters = spriteCompiledEffect_->GetDescription().parameters;
                    const auto matrix = std::find_if(
                        parameters.begin(), parameters.end(),
                        [](const CompiledEffectParameterDescription& parameter)
                        {
                            return parameter.name == "MatrixTransform";
                        });
                    if (matrix == parameters.end())
                    {
                        throw std::runtime_error(
                            "RLGL: embedded XNA SpriteEffect has no MatrixTransform parameter");
                    }
                    spriteMatrixParameterIndex_ = matrix->runtimeIndex;
#endif
                }
                catch (...)
                {
                    Bridge::DestroySpritePipeline(pipeline_);
                    throw;
                }
            }

            ~RlglSpriteBatchRenderer() override
            {
                Bridge::DestroySpritePipeline(pipeline_);
            }

            RlglSpriteBatchRenderer(const RlglSpriteBatchRenderer&) = delete;
            RlglSpriteBatchRenderer& operator=(const RlglSpriteBatchRenderer&) = delete;

            void Begin() override
            {
                if (begun_)
                    throw std::runtime_error("RLGL: SpriteBatch.Begin called twice");
                begun_ = true;
            }

            void End() override
            {
                if (!begun_)
                    throw std::runtime_error("RLGL: SpriteBatch.End called before Begin");
                Flush();
                begun_ = false;
            }

            void SetTransformMatrix(const Matrix& matrix) override
            {
                transform_ = matrix;
            }

            void SetCustomEffect(Effect* effect) override
            {
                if (customEffect_ == effect) return;
                Flush();
                if (effect != nullptr)
                {
#if defined(CNA_RLGL_COMPILED_EFFECTS)
                    if (dynamic_cast<RlglCompiledEffect*>(effect->GetCompiledRuntimePtr()) == nullptr)
                    {
                        throw System::NotSupportedException(
                            "RLGL: SpriteBatch source/custom effects are deferred to "
                            "plans/plan_rlgl.md RLGL-050");
                    }
#else
                    throw System::NotSupportedException(
                        "RLGL: compiled SpriteBatch effects require CNA_RLGL_COMPILED_EFFECTS=ON "
                        "(plans/plan_rlgl.md RLGL-051)");
#endif
                }
                customEffect_ = effect;
            }

            void SetSamplerState(
                const int textureFilter, const int addressU, const int addressV,
                const int addressW, const int maxAnisotropy,
                const int maxMipLevel, const float lodBias) override
            {
                filter_ = textureFilter;
                addressU_ = addressU;
                addressV_ = addressV;
                addressW_ = addressW;
                maxAnisotropy_ = maxAnisotropy;
                maxMipLevel_ = maxMipLevel;
                lodBias_ = lodBias;
            }

            void SetImmediateMode(const bool immediate) override
            {
                immediate_ = immediate;
            }

            void Draw(const ITextureRenderer& texture, const float x, const float y) override
            {
                const int width = texture.GetWidth();
                const int height = texture.GetHeight();
                Draw(texture, x, y, static_cast<float>(width), static_cast<float>(height),
                     Rectangle(0, 0, width, height), Color::White, 0.0f,
                     Vector2::Zero, SpriteEffects::None, 0.0f);
            }

            void Draw(
                const ITextureRenderer& texture,
                const Rectangle& destinationRectangle,
                const Rectangle& sourceRectangle,
                const Color& color) override
            {
                Draw(texture,
                     static_cast<float>(destinationRectangle.X),
                     static_cast<float>(destinationRectangle.Y),
                     static_cast<float>(destinationRectangle.Width),
                     static_cast<float>(destinationRectangle.Height),
                     sourceRectangle, color, 0.0f, Vector2::Zero,
                     SpriteEffects::None, 0.0f);
            }

            void Draw(
                const ITextureRenderer& texture,
                const Rectangle& destinationRectangle,
                const Rectangle& sourceRectangle,
                const Color& color, const float rotation,
                const Vector2& origin, const SpriteEffects effects,
                const float layerDepth) override
            {
                Draw(texture,
                     static_cast<float>(destinationRectangle.X),
                     static_cast<float>(destinationRectangle.Y),
                     static_cast<float>(destinationRectangle.Width),
                     static_cast<float>(destinationRectangle.Height),
                     sourceRectangle, color, rotation, origin, effects, layerDepth);
            }

            void Draw(
                const ITextureRenderer& texture,
                const float destinationX, const float destinationY,
                const float destinationWidth, const float destinationHeight,
                const Rectangle& sourceRectangle,
                const Color& color, const float rotation,
                const Vector2& origin, const SpriteEffects effects,
                const float layerDepth) override
            {
                (void)layerDepth;
                if (!begun_)
                    throw std::runtime_error("RLGL: SpriteBatch.Draw called before Begin");

                if (currentTexture_ != &texture || VertexCount() + 4 > kVertexCapacity)
                {
                    Flush();
                    currentTexture_ = &texture;
                }

                const float textureWidth = static_cast<float>(texture.GetWidth());
                const float textureHeight = static_cast<float>(texture.GetHeight());
                float u1 = static_cast<float>(sourceRectangle.X) / textureWidth;
                float v1 = static_cast<float>(sourceRectangle.Y) / textureHeight;
                float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) /
                    textureWidth;
                float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) /
                    textureHeight;
                if ((static_cast<int>(effects) &
                     static_cast<int>(SpriteEffects::FlipHorizontally)) != 0)
                {
                    std::swap(u1, u2);
                }
                if ((static_cast<int>(effects) &
                     static_cast<int>(SpriteEffects::FlipVertically)) != 0)
                {
                    std::swap(v1, v2);
                }
                if (SampledRowsAreBottomUp(texture) &&
                    !BatchFlushesThroughCompiledEffect())
                {
                    v1 = 1.0f - v1;
                    v2 = 1.0f - v2;
                }

                const float sourceWidth = static_cast<float>(sourceRectangle.Width);
                const float sourceHeight = static_cast<float>(sourceRectangle.Height);
                const float scaleX = destinationWidth / sourceWidth;
                const float scaleY = destinationHeight / sourceHeight;
                const float left = -origin.X * scaleX;
                const float top = -origin.Y * scaleY;
                const float right = (sourceWidth - origin.X) * scaleX;
                const float bottom = (sourceHeight - origin.Y) * scaleY;
                const float cosine = std::cos(rotation);
                const float sine = std::sin(rotation);

                const auto position = [&](const float x, const float y)
                {
                    return Vector2(
                        destinationX + x * cosine - y * sine,
                        destinationY + x * sine + y * cosine);
                };
                const Vector2 p0 = position(left, top);
                const Vector2 p1 = position(right, top);
                const Vector2 p2 = position(right, bottom);
                const Vector2 p3 = position(left, bottom);

                const float red = static_cast<float>(color.getRProperty()) / 255.0f;
                const float green = static_cast<float>(color.getGProperty()) / 255.0f;
                const float blue = static_cast<float>(color.getBProperty()) / 255.0f;
                const float alpha = static_cast<float>(color.getAProperty()) / 255.0f;
                const auto appendVertex = [&](const Vector2& p, const float u, const float v)
                {
                    vertices_.insert(vertices_.end(),
                        {p.X, p.Y, u, v, red, green, blue, alpha});
                };

                const auto base = static_cast<std::uint16_t>(VertexCount());
                appendVertex(p0, u1, v1);
                appendVertex(p1, u2, v1);
                appendVertex(p2, u2, v2);
                appendVertex(p3, u1, v2);
                indices_.insert(indices_.end(), {
                    static_cast<std::uint16_t>(base + 0),
                    static_cast<std::uint16_t>(base + 1),
                    static_cast<std::uint16_t>(base + 2),
                    static_cast<std::uint16_t>(base + 2),
                    static_cast<std::uint16_t>(base + 3),
                    static_cast<std::uint16_t>(base + 0)});

                if (immediate_) Flush();
            }

        private:
            [[nodiscard]] int VertexCount() const
            {
                return static_cast<int>(vertices_.size() / kFloatsPerVertex);
            }

            [[nodiscard]] bool BatchFlushesThroughCompiledEffect() const
            {
#if defined(CNA_RLGL_COMPILED_EFFECTS)
                return customEffect_ != nullptr &&
                    customEffect_->GetCompiledRuntimePtr() != nullptr;
#else
                return false;
#endif
            }

            void ClearBatch() noexcept
            {
                vertices_.clear();
                indices_.clear();
                currentTexture_ = nullptr;
            }

            void Flush()
            {
                if (vertices_.empty()) return;
                if (currentTexture_ == nullptr)
                    throw std::runtime_error("RLGL: SpriteBatch lost its texture group");

                int viewportWidth = 0;
                int viewportHeight = 0;
                renderer_.GetSpriteBatchViewportSize(viewportWidth, viewportHeight);
                if (viewportWidth <= 0 || viewportHeight <= 0)
                    throw std::runtime_error("RLGL: SpriteBatch viewport is empty");

#if defined(CNA_RLGL_COMPILED_EFFECTS)
                if (BatchFlushesThroughCompiledEffect())
                {
                    FlushBatchWithCompiledEffect(viewportWidth, viewportHeight);
                    return;
                }
#endif

                const Matrix orthographic = Matrix::CreateOrthographicOffCenter(
                    0.0f, static_cast<float>(viewportWidth),
                    static_cast<float>(viewportHeight), 0.0f, -1.0f, 1.0f);
                const Matrix combined = transform_ * orthographic;
                float projection[16] = {};
                combined.ToColumnMajor(projection);

                // The production path never records into rlgl's global immediate batch. Drain
                // any diagnostic work before establishing CNA's texture/sampler bindings.
                Bridge::FlushImmediateBatch();
                currentTexture_->BindGL(0);
                renderer_.ApplySamplerState(
                    0, filter_, addressU_, addressV_, maxAnisotropy_);
                renderer_.ApplySamplerMipState(0, maxMipLevel_, lodBias_);
                renderer_.ApplySamplerAddressW(0, addressW_);
                Bridge::DrawSpriteGeometry(
                    pipeline_, vertices_.data(), VertexCount(),
                    indices_.data(), static_cast<int>(indices_.size()), projection);

                ClearBatch();
            }

#if defined(CNA_RLGL_COMPILED_EFFECTS)
            void ApplyCompiledSpriteVertexShader(
                const int viewportWidth, const int viewportHeight)
            {
                if (spriteCompiledEffect_ == nullptr || customEffect_ == nullptr ||
                    viewportWidth <= 0 || viewportHeight <= 0)
                {
                    throw std::runtime_error(
                        "RLGL: the XNA SpriteBatch vertex effect is unavailable");
                }

                const Matrix projection = Matrix::CreateOrthographicOffCenter(
                    0.0f, static_cast<float>(viewportWidth),
                    static_cast<float>(viewportHeight), 0.0f, 0.0f, -1.0f);
                const Matrix combined = transform_ * projection;
                float values[16] = {};
                combined.ToColumnMajor(values);
                spriteCompiledEffect_->SetParameterValue(
                    spriteMatrixParameterIndex_, values, sizeof(values));
                spriteCompiledEffect_->SetTechnique(0);

                auto& graphicsDevice = customEffect_->getGraphicsDeviceInternal();
                CompiledEffectDeviceState deviceState;
                deviceState.blend = &graphicsDevice.getBlendStateProperty();
                deviceState.depthStencil = &graphicsDevice.getDepthStencilStateProperty();
                deviceState.rasterizer = &graphicsDevice.getRasterizerStateProperty();
                deviceState.samplerStates = &graphicsDevice.getSamplerStatesProperty();
                deviceState.vertexSamplerStates =
                    &graphicsDevice.getVertexSamplerStatesProperty();
                CompiledEffectPassStateChanges ignoredChanges;
                spriteCompiledEffect_->ApplyPass(0, deviceState, ignoredChanges);
            }

            void FlushBatchWithCompiledEffect(
                const int viewportWidth, const int viewportHeight)
            {
                auto* const runtime = dynamic_cast<RlglCompiledEffect*>(
                    customEffect_ != nullptr
                        ? customEffect_->GetCompiledRuntimePtr() : nullptr);
                if (runtime == nullptr || currentTexture_ == nullptr)
                {
                    ClearBatch();
                    throw std::runtime_error(
                        "RLGL: compiled SpriteBatch lost its effect or texture group");
                }

                using Microsoft::Xna::Framework::Graphics::PrimitiveType;
                using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
                using Microsoft::Xna::Framework::Graphics::VertexElement;
                using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
                using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
                static const VertexDeclaration declaration(
                    kFloatsPerVertex * static_cast<int>(sizeof(float)), {
                        VertexElement(
                            0, VertexElementFormat::Vector2,
                            VertexElementUsage::Position, 0),
                        VertexElement(
                            2 * static_cast<int>(sizeof(float)),
                            VertexElementFormat::Vector2,
                            VertexElementUsage::TextureCoordinate, 0),
                        VertexElement(
                            4 * static_cast<int>(sizeof(float)),
                            VertexElementFormat::Vector4,
                            VertexElementUsage::Color, 0),
                    });

                try
                {
                    if (compiledSpriteVertexBuffer_ == nullptr)
                    {
                        compiledSpriteVertexBuffer_ =
                            renderer_.CreateVertexBuffer(kVertexCapacity);
                        compiledSpriteVertexBuffer_->SetVertexDeclaration(declaration);
                    }
                    if (compiledSpriteIndexBuffer_ == nullptr)
                    {
                        compiledSpriteIndexBuffer_ =
                            renderer_.CreateIndexBuffer16(kIndexCapacity);
                    }
                    compiledSpriteVertexBuffer_->SetData(
                        vertices_.data(), VertexCount(),
                        kFloatsPerVertex * sizeof(float));
                    compiledSpriteIndexBuffer_->SetData16(
                        indices_.data(), static_cast<int>(indices_.size()));

                    renderer_.ApplySamplerState(
                        0, filter_, addressU_, addressV_, maxAnisotropy_);
                    renderer_.ApplySamplerMipState(0, maxMipLevel_, lodBias_);
                    renderer_.ApplySamplerAddressW(0, addressW_);

                    auto* const technique =
                        customEffect_->getCurrentTechniqueProperty();
                    const int passCount = technique != nullptr
                        ? technique->getPassesProperty().getCountProperty() : 0;
                    if (passCount <= 0)
                    {
                        throw System::InvalidOperationException(
                            "RLGL: a compiled Effect used with SpriteBatch must have a "
                            "current technique with at least one pass");
                    }

                    ApplyCompiledSpriteVertexShader(viewportWidth, viewportHeight);
                    const auto& deviceTextures =
                        customEffect_->getGraphicsDeviceInternal().getTexturesProperty();
                    GpuDrawParams params;
                    params.compiledEffectRuntime = runtime;
                    for (int pass = 0; pass < passCount; ++pass)
                    {
                        technique->getPassesProperty()[pass].Apply();
                        renderer_.DrawCompiledEffectGeometry(
                            *compiledSpriteVertexBuffer_,
                            compiledSpriteIndexBuffer_.get(),
                            PrimitiveType::TriangleList,
                            static_cast<int>(indices_.size()),
                            0, 0, 0, params, currentTexture_, &deviceTextures);
                    }
                }
                catch (...)
                {
                    ClearBatch();
                    throw;
                }
                ClearBatch();
            }
#endif

            RlglRenderer& renderer_;
            Bridge::SpritePipeline pipeline_{};
            std::vector<float> vertices_;
            std::vector<std::uint16_t> indices_;
            const ITextureRenderer* currentTexture_ = nullptr;
            Matrix transform_ = Matrix::getIdentityProperty();
            Effect* customEffect_ = nullptr;
#if defined(CNA_RLGL_COMPILED_EFFECTS)
            std::unique_ptr<RlglCompiledEffect> spriteCompiledEffect_;
            std::uint32_t spriteMatrixParameterIndex_ = 0;
            std::unique_ptr<IVertexBufferRenderer> compiledSpriteVertexBuffer_;
            std::unique_ptr<IIndexBufferRenderer> compiledSpriteIndexBuffer_;
#endif
            int filter_ = 0;
            int addressU_ = 1;
            int addressV_ = 1;
            int addressW_ = 1;
            int maxAnisotropy_ = 4;
            int maxMipLevel_ = 0;
            float lodBias_ = 0.0f;
            bool immediate_ = false;
            bool begun_ = false;
        };
    }

    std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatchRenderer(
        RlglRenderer& renderer)
    {
        return std::make_unique<Detail::RlglSpriteBatchRenderer>(renderer);
    }
}
