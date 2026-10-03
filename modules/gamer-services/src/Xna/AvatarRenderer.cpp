// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AvatarSpace.hpp"
#include "CNA/Internal/GamerServices/DispatcherGraphics.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include <chrono>
#include <cmath>
#include <map>

namespace Microsoft::Xna::Framework::GamerServices
{
    namespace
    {
        namespace Avatars = CNA::Internal::GamerServices::Avatars;
        namespace Graphics = Microsoft::Xna::Framework::Graphics;
        using Microsoft::Xna::Framework::Matrix;
        using Microsoft::Xna::Framework::Quaternion;
        using Microsoft::Xna::Framework::Vector3;

        struct GpuPart
        {
            std::unique_ptr<Graphics::VertexBuffer> vertices;
            std::unique_ptr<Graphics::IndexBuffer> indices;
            int vertexCount = 0;
            int primitiveCount = 0;
        };

        // The standard loading effect: a softly glowing silhouette that breathes while loading.
        std::vector<Graphics::VertexPositionColor> LoadingSilhouette(float height, std::vector<std::uint16_t>& indices)
        {
            std::vector<Graphics::VertexPositionColor> vertices;
            auto ellipsoid = [&](Vector3 centre, Vector3 radii) {
                const auto base = static_cast<std::uint16_t>(vertices.size());
                constexpr int Rings = 10, Segments = 16;
                for (int r = 0; r <= Rings; ++r)
                {
                    const float theta = 3.14159265f * static_cast<float>(r) / Rings;
                    for (int c = 0; c < Segments; ++c)
                    {
                        const float phi = 6.2831853f * static_cast<float>(c) / Segments;
                        const Vector3 p(centre.X + radii.X * std::sin(theta) * std::sin(phi), centre.Y + radii.Y * std::cos(theta),
                                        centre.Z + radii.Z * std::sin(theta) * std::cos(phi));
                        vertices.emplace_back(p, Microsoft::Xna::Framework::Color::White);
                    }
                }
                for (int r = 0; r < Rings; ++r)
                {
                    for (int c = 0; c < Segments; ++c)
                    {
                        const auto a = static_cast<std::uint16_t>(base + r * Segments + c);
                        const auto b = static_cast<std::uint16_t>(base + r * Segments + (c + 1) % Segments);
                        const auto d = static_cast<std::uint16_t>(a + Segments);
                        const auto e = static_cast<std::uint16_t>(b + Segments);
                        indices.insert(indices.end(), {a, e, d, a, b, e});
                    }
                }
            };
            ellipsoid(Vector3(0, height * 0.9f, 0), Vector3(0.13f, height * 0.1f, 0.13f));
            ellipsoid(Vector3(0, height * 0.43f, 0), Vector3(0.2f, height * 0.42f, 0.14f));
            return vertices;
        }
    }

    struct AvatarRenderer::Resources
    {
        std::shared_ptr<Avatars::AvatarLoad> load;
        std::shared_ptr<const Avatars::AvatarModel> model;
        float height = 1.7f;

        Graphics::GraphicsDevice* device = nullptr;
        std::unique_ptr<Graphics::SkinnedEffect> effect;
        std::vector<GpuPart> parts;
        std::vector<std::unique_ptr<Graphics::Texture2D>> images;
        std::map<int, std::unique_ptr<Graphics::Texture2D>> tiles;
        std::unique_ptr<Graphics::Texture2D> white;
        std::unique_ptr<Graphics::BasicEffect> loadingEffect;
        std::unique_ptr<Graphics::VertexBuffer> loadingVertices;
        std::unique_ptr<Graphics::IndexBuffer> loadingIndices;
        int loadingVertexCount = 0;
        int loadingPrimitiveCount = 0;

        void Release()
        {
            effect.reset();
            parts.clear();
            images.clear();
            tiles.clear();
            white.reset();
            loadingEffect.reset();
            loadingVertices.reset();
            loadingIndices.reset();
            device = nullptr;
        }

        // Resources belong to one device; a different device (or a recreated one) rebuilds them.
        Graphics::GraphicsDevice& Bind()
        {
            auto& current = CNA::Internal::GamerServices::dispatcherGraphicsDevice();
            if (&current != device)
            {
                Release();
                device = &current;
            }
            return current;
        }

        Graphics::Texture2D* Tile(int index)
        {
            auto found = tiles.find(index);
            if (found != tiles.end())
            {
                return found->second.get();
            }
            const auto& image = (*model->faceTiles)[static_cast<std::size_t>(index)];
            auto texture = std::make_unique<Graphics::Texture2D>(Graphics::Texture2D::CreateFromPixels(*device, image.width, image.height, image.rgba));
            return tiles.emplace(index, std::move(texture)).first->second.get();
        }

        void Upload()
        {
            if (effect)
            {
                return;
            }
            effect = std::make_unique<Graphics::SkinnedEffect>(*device);
            white = std::make_unique<Graphics::Texture2D>(Graphics::Texture2D::CreateFromPixels(*device, 1, 1, {255, 255, 255, 255}));
            for (const auto& image : model->images)
            {
                images.push_back(std::make_unique<Graphics::Texture2D>(
                    Graphics::Texture2D::CreateFromPixels(*device, image.width, image.height, image.rgba)));
            }
            for (const auto& part : model->parts)
            {
                std::vector<Graphics::VertexPositionNormalTextureSkinned> vertices;
                vertices.reserve(part.vertices.size());
                for (const auto& v : part.vertices)
                {
                    vertices.emplace_back(v.position, v.normal, v.uv, v.weights, v.joints);
                }
                GpuPart gpu;
                gpu.vertexCount = static_cast<int>(vertices.size());
                gpu.primitiveCount = static_cast<int>(part.indices.size() / 3);
                gpu.vertices = std::make_unique<Graphics::VertexBuffer>(*device, gpu.vertexCount);
                gpu.vertices->SetData(vertices.data(), gpu.vertexCount);
                gpu.indices = std::make_unique<Graphics::IndexBuffer>(*device, static_cast<int>(part.indices.size()));
                gpu.indices->SetData(part.indices.data(), static_cast<int>(part.indices.size()));
                parts.push_back(std::move(gpu));
            }
        }
    };

    namespace
    {
        // Restores whatever the game had set once the avatar is drawn.
        struct DeviceStateScope
        {
            explicit DeviceStateScope(Graphics::GraphicsDevice& device)
                : device(device)
                , blend(device.getBlendStateProperty())
                , depth(device.getDepthStencilStateProperty())
                , rasterizer(device.getRasterizerStateProperty())
                , sampler(device.getSamplerStatesProperty()[0])
            {
            }
            ~DeviceStateScope()
            {
                device.setBlendStateProperty(blend);
                device.setDepthStencilStateProperty(depth);
                device.setRasterizerStateProperty(rasterizer);
                device.getSamplerStatesProperty()[0] = sampler;
            }
            Graphics::GraphicsDevice& device;
            Graphics::BlendState blend;
            Graphics::DepthStencilState depth;
            Graphics::RasterizerState rasterizer;
            Graphics::SamplerState sampler;
        };

        template <typename TEnum>
        int StateIndex(TEnum value, int count)
        {
            const int index = static_cast<int>(value);
            return index >= 0 && index < count ? index : 0;
        }
    }

    AvatarRenderer::AvatarRenderer(AvatarDescription* avatarDescription)
        : AvatarRenderer(avatarDescription, true)
    {
    }

    AvatarRenderer::AvatarRenderer(AvatarDescription* avatarDescription, bool useLoadingEffect)
        : resources_(std::make_unique<Resources>())
        , useLoadingEffect_(useLoadingEffect)
        , world_(Microsoft::Xna::Framework::Matrix::getIdentityProperty())
        , view_(Microsoft::Xna::Framework::Matrix::getIdentityProperty())
        , projection_(Microsoft::Xna::Framework::Matrix::getIdentityProperty())
        , parentBoneIds_(Avatars::parentBones().begin(), Avatars::parentBones().end())
    {
        if (avatarDescription == nullptr)
        {
            throw System::ArgumentNullException("avatarDescription");
        }
        if (!avatarDescription->getIsValidProperty())
        {
            return;
        }
        const auto bytes = avatarDescription->getDescriptionProperty();
        const auto descriptor = Avatars::decode(bytes);
        if (!descriptor)
        {
            return;
        }
        resources_->height = static_cast<float>(descriptor->heightMillimeters) / 1000.0f;
        resources_->load = Avatars::loadAvatarAsync(*descriptor);
        state_ = AvatarRendererState::Loading;
    }

    AvatarRenderer::~AvatarRenderer() = default;

    void AvatarRenderer::Poll() const
    {
        if (state_ != AvatarRendererState::Loading || !resources_->load)
        {
            return;
        }
        std::lock_guard guard(resources_->load->lock);
        if (!resources_->load->done)
        {
            return;
        }
        resources_->model = resources_->load->model;
        resources_->load.reset();
        if (!resources_->model)
        {
            state_ = AvatarRendererState::Unavailable;
            return;
        }
        bindPoseArray_.clear();
        for (const auto& translation : resources_->model->bindTranslations)
        {
            bindPoseArray_.push_back(Avatars::changeAvatarSpace(
                Matrix::CreateTranslation(translation.X, translation.Y, translation.Z)));
        }
        state_ = AvatarRendererState::Ready;
    }

    Microsoft::Xna::Framework::Matrix AvatarRenderer::getWorldProperty() const { return world_; }
    void AvatarRenderer::setWorldProperty(Microsoft::Xna::Framework::Matrix value) { world_ = value; }

    Microsoft::Xna::Framework::Matrix AvatarRenderer::getViewProperty() const { return view_; }
    void AvatarRenderer::setViewProperty(Microsoft::Xna::Framework::Matrix value) { view_ = value; }

    Microsoft::Xna::Framework::Matrix AvatarRenderer::getProjectionProperty() const { return projection_; }
    void AvatarRenderer::setProjectionProperty(Microsoft::Xna::Framework::Matrix value) { projection_ = value; }

    System::Collections::ObjectModel::ReadOnlyCollection<int> AvatarRenderer::getParentBonesProperty() const
    {
        return System::Collections::ObjectModel::ReadOnlyCollection<int>(parentBoneIds_);
    }

    System::Collections::ObjectModel::ReadOnlyCollection<Microsoft::Xna::Framework::Matrix>
    AvatarRenderer::getBindPoseProperty() const
    {
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("AvatarRenderer");
        }
        Poll();
        if (state_ != AvatarRendererState::Ready)
        {
            throw System::InvalidOperationException("The avatar's bind pose is not available until the avatar is ready.");
        }
        return System::Collections::ObjectModel::ReadOnlyCollection<Microsoft::Xna::Framework::Matrix>(bindPoseArray_);
    }

    AvatarRendererState AvatarRenderer::getStateProperty() const
    {
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("AvatarRenderer");
        }
        Poll();
        return state_;
    }

    Microsoft::Xna::Framework::Vector3 AvatarRenderer::getLightColorProperty() const { return lightColor_; }
    void AvatarRenderer::setLightColorProperty(Microsoft::Xna::Framework::Vector3 value) { lightColor_ = value; }

    Microsoft::Xna::Framework::Vector3 AvatarRenderer::getLightDirectionProperty() const { return lightDirection_; }
    void AvatarRenderer::setLightDirectionProperty(Microsoft::Xna::Framework::Vector3 value) { lightDirection_ = value; }

    Microsoft::Xna::Framework::Vector3 AvatarRenderer::getAmbientLightColorProperty() const { return ambientLightColor_; }
    void AvatarRenderer::setAmbientLightColorProperty(Microsoft::Xna::Framework::Vector3 value) { ambientLightColor_ = value; }

    bool AvatarRenderer::getIsDisposedProperty() const { return isDisposed_; }

    void AvatarRenderer::Draw(IAvatarAnimation* animation)
    {
        if (animation == nullptr)
        {
            throw System::ArgumentNullException("animation");
        }
        auto boneTransforms = animation->getBoneTransformsProperty();
        std::vector<Microsoft::Xna::Framework::Matrix> bones(boneTransforms.begin(), boneTransforms.end());
        Draw(bones, animation->getExpressionProperty());
    }

    void AvatarRenderer::Draw(const std::vector<Microsoft::Xna::Framework::Matrix>& bones, AvatarExpression expression)
    {
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("AvatarRenderer");
        }
        if (static_cast<int>(bones.size()) != BoneCount)
        {
            throw System::ArgumentException("bones must contain exactly 71 entries.", "bones");
        }
        Poll();
        if (state_ == AvatarRendererState::Loading)
        {
            if (useLoadingEffect_)
            {
                DrawLoadingEffect();
            }
            return;
        }
        if (state_ == AvatarRendererState::Ready)
        {
            DrawAvatar(bones, expression);
        }
    }

    void AvatarRenderer::DrawAvatar(const std::vector<Microsoft::Xna::Framework::Matrix>& bones, AvatarExpression expression)
    {
        const auto& model = *resources_->model;
        std::array<Matrix, BoneCount> world;
        std::vector<Matrix> skin(BoneCount);
        for (int bone = 0; bone < BoneCount; ++bone)
        {
            Vector3 scale;
            Vector3 translation;
            Quaternion rotation;
            // Callers supply XNA coordinates; skinning uses the catalog's frozen asset coordinates.
            if (!Avatars::changeAvatarSpace(bones[bone]).Decompose(scale, rotation, translation))
            {
                throw System::InvalidOperationException("Every avatar bone transform must be decomposable.");
            }
            if (bone != 0)
            {
                translation = model.bindTranslations[bone];
            }
            const Matrix local = Matrix::CreateScale(scale) * Matrix::CreateFromQuaternion(rotation) *
                Matrix::CreateTranslation(translation.X, translation.Y, translation.Z);
            const int parent = parentBoneIds_[bone];
            world[bone] = parent < 0 ? local : local * world[parent];
            const auto& bind = model.bindPositions[bone];
            skin[bone] = Matrix::CreateTranslation(-bind.X, -bind.Y, -bind.Z) * world[bone];
        }

        auto& device = resources_->Bind();
        resources_->Upload();
        auto& effect = *resources_->effect;
        effect.setWorldProperty(Avatars::avatarSpaceBasis() * world_);
        effect.setViewProperty(view_);
        effect.setProjectionProperty(projection_);
        effect.SetBoneTransforms(skin);
        effect.setWeightsPerVertexProperty(4);
        effect.setPreferPerPixelLightingProperty(true);
        // Exactly the renderer's one directional light plus ambient, with a faint sheen from the key light.
        effect.getDirectionalLight0Property().setEnabledProperty(true);
        effect.getDirectionalLight0Property().setDirectionProperty(lightDirection_);
        effect.getDirectionalLight0Property().setDiffuseColorProperty(lightColor_);
        effect.getDirectionalLight0Property().setSpecularColorProperty(lightColor_ * 0.12f);
        effect.getDirectionalLight1Property().setEnabledProperty(false);
        effect.getDirectionalLight2Property().setEnabledProperty(false);
        effect.setAmbientLightColorProperty(ambientLightColor_);
        effect.setEmissiveColorProperty(Vector3::Zero);
        effect.setSpecularPowerProperty(24.0f);
        effect.setAlphaProperty(1.0f);

        const auto& face = model.face;
        const int mouth = StateIndex(expression.getMouthProperty(), 14);
        const std::array<int, 2> eyes{StateIndex(expression.getLeftEyeProperty(), 14), StateIndex(expression.getRightEyeProperty(), 14)};
        const std::array<int, 2> brows{StateIndex(expression.getLeftEyebrowProperty(), 5),
                                       StateIndex(expression.getRightEyebrowProperty(), 5)};

        DeviceStateScope scope(device);
        device.setRasterizerStateProperty(Graphics::RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = Graphics::SamplerState::LinearClamp;
        bool decals = false;
        for (std::size_t index = 0; index < model.parts.size(); ++index)
        {
            const auto& part = model.parts[index];
            Graphics::Texture2D* texture = resources_->white.get();
            if (part.feature == Avatars::AvatarFeature::None)
            {
                if (part.image >= 0)
                {
                    texture = resources_->images[static_cast<std::size_t>(part.image)].get();
                }
                device.setBlendStateProperty(Graphics::BlendState::Opaque);
                device.setDepthStencilStateProperty(Graphics::DepthStencilState::Default);
            }
            else
            {
                int tile = 0;
                switch (part.feature)
                {
                case Avatars::AvatarFeature::EyeLeft: tile = face.eyes[eyes[0]][0][part.layer]; break;
                case Avatars::AvatarFeature::EyeRight: tile = face.eyes[eyes[1]][1][part.layer]; break;
                case Avatars::AvatarFeature::EyebrowLeft: tile = face.eyebrows[brows[0]][0]; break;
                case Avatars::AvatarFeature::EyebrowRight: tile = face.eyebrows[brows[1]][1]; break;
                default: tile = face.mouths[mouth]; break;
                }
                texture = resources_->Tile(tile);
                if (!decals)
                {
                    device.setBlendStateProperty(Graphics::BlendState::AlphaBlend);
                    device.setDepthStencilStateProperty(Graphics::DepthStencilState::DepthRead);
                    decals = true;
                }
            }
            effect.setDiffuseColorProperty(part.color);
            // Materials scale the one light's faint highlight: hair and leather shine a little, cloth barely.
            effect.setSpecularColorProperty(Vector3(part.specular, part.specular, part.specular));
            effect.setTextureProperty(texture);
            effect.Apply();
            const auto& gpu = resources_->parts[index];
            device.SetVertexBuffer(gpu.vertices.get());
            device.SetIndexBuffer(gpu.indices.get());
            device.DrawIndexedPrimitives(Graphics::PrimitiveType::TriangleList, 0, 0, gpu.vertexCount, 0, gpu.primitiveCount);
        }
    }

    void AvatarRenderer::DrawLoadingEffect()
    {
        auto& device = resources_->Bind();
        auto& r = *resources_;
        if (!r.loadingEffect)
        {
            std::vector<std::uint16_t> indices;
            const auto vertices = LoadingSilhouette(r.height, indices);
            r.loadingEffect = std::make_unique<Graphics::BasicEffect>(device);
            r.loadingVertexCount = static_cast<int>(vertices.size());
            r.loadingPrimitiveCount = static_cast<int>(indices.size() / 3);
            r.loadingVertices = std::make_unique<Graphics::VertexBuffer>(device, r.loadingVertexCount);
            r.loadingVertices->SetData(vertices.data(), r.loadingVertexCount);
            r.loadingIndices = std::make_unique<Graphics::IndexBuffer>(device, static_cast<int>(indices.size()));
            r.loadingIndices->SetData(indices.data(), static_cast<int>(indices.size()));
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(seconds * 3.0));
        auto& effect = *r.loadingEffect;
        effect.setWorldProperty(Matrix::CreateScale(1.0f + 0.03f * pulse) * world_);
        effect.setViewProperty(view_);
        effect.setProjectionProperty(projection_);
        effect.setLightingEnabledProperty(false);
        effect.setVertexColorEnabledProperty(true);
        effect.setDiffuseColorProperty(Vector3(0.45f, 0.7f, 1.0f));
        effect.setAlphaProperty(0.25f + 0.35f * pulse);
        DeviceStateScope scope(device);
        device.setBlendStateProperty(Graphics::BlendState::AlphaBlend);
        device.setDepthStencilStateProperty(Graphics::DepthStencilState::DepthRead);
        device.setRasterizerStateProperty(Graphics::RasterizerState::CullCounterClockwise);
        effect.Apply();
        device.SetVertexBuffer(r.loadingVertices.get());
        device.SetIndexBuffer(r.loadingIndices.get());
        device.DrawIndexedPrimitives(Graphics::PrimitiveType::TriangleList, 0, 0, r.loadingVertexCount, 0, r.loadingPrimitiveCount);
    }

    void AvatarRenderer::Dispose()
    {
        Dispose(true);
    }

    void AvatarRenderer::Dispose(bool /*disposing*/)
    {
        if (isDisposed_)
        {
            return;
        }
        isDisposed_ = true;
        // A load still running on the loader thread finishes into its own shared state.
        resources_->Release();
        resources_->load.reset();
        resources_->model.reset();
    }
}
