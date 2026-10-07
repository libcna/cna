// SPDX-License-Identifier: MS-PL
// GS-009: the standard XNA avatar API renders a real avatar -- no CNA avatar extension calls.
//
// A Game with a GamerServicesComponent creates an AvatarRenderer for a fixed description,
// waits for it to become Ready, and renders into a 256x256 render target:
//   - the loading effect draws while the avatar is still loading;
//   - the ready avatar shows its shirt, trousers and skin colors where the body is;
//   - a different AvatarExpression changes the face, and only the face;
//   - Wave moves the right arm above the shoulder compared with Stand0;
//   - the background around the avatar is untouched.
// Set CNA_AVATAR_SNAPSHOT_DIR to also write each frame as a PNG.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int Size = 256;
    const Color Background(20, 24, 32, 255);

    // A fixed description (the test builds the bytes directly so the expected colors are known;
    // games get theirs from CreateRandom or BeginGetFromGamer).
    std::vector<SharpRuntime::bytecs> FixedDescription()
    {
        namespace Avatars = CNA::Internal::GamerServices::Avatars;
        Avatars::AvatarDescriptor d;
        d.bodyType = 1;
        d.heightMillimeters = 1800;
        d.build = 128;
        d.colors = {{{235, 190, 150}, {40, 30, 24}, {60, 110, 160}, {230, 40, 40}, {40, 60, 220}, {250, 250, 250}, {30, 30, 30}}};
        d.items = {1, 20, 40, 60, 0, 0};
        return Avatars::encode(d);
    }

    class AvatarStandardRenderTest : public Game
    {
        GraphicsDeviceManager graphics_{this};
        std::unique_ptr<RenderTarget2D> target_;
        std::unique_ptr<AvatarDescription> description_;
        std::unique_ptr<AvatarRenderer> renderer_;
        int frame_ = 0;
        int result_ = 1;
        bool loadingChecked_ = false;
        bool loadingDrawn_ = false;
        std::vector<std::string> failures_;

        std::vector<Color> Render(AvatarRenderer& renderer, IAvatarAnimation* animation, const AvatarExpression* expression,
                                  bool closeUp = false)
        {
            auto& device = getGraphicsDeviceProperty();
            device.SetRenderTarget(target_.get());
            device.Clear(Background);
            // XNA avatars face -Z; a camera on +Z sees the face through a half-turn, as SAMPLE-085 sets it
            // (GS-009g). That half-turn cancels the renderer's own, so the model-space points below are
            // the catalog's: facing +Z with the avatar's left on +X.
            renderer.setWorldProperty(Matrix::CreateRotationY(MathHelper::Pi));
            renderer.setViewProperty(closeUp ? Matrix::CreateLookAt(Vector3(0, 1.64f, 0.75f), Vector3(0, 1.62f, 0), Vector3::Up)
                                             : Matrix::CreateLookAt(Vector3(0, 1.0f, 3.1f), Vector3(0, 0.95f, 0), Vector3::Up));
            renderer.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(0.7853982f, 1.0f, 0.1f, 20.0f));
            if (expression)
            {
                const auto bones = animation->getBoneTransformsProperty();
                renderer.Draw(std::vector<Matrix>(bones.begin(), bones.end()), *expression);
            }
            else
            {
                renderer.Draw(animation);
            }
            device.SetRenderTarget(nullptr);
            std::vector<Color> pixels(Size * Size);
            target_->GetData(pixels.data(), Size * Size);
            return pixels;
        }

        static Vector2 Project(const Vector3& point)
        {
            const Matrix viewProjection = Matrix::CreateLookAt(Vector3(0, 1.0f, 3.1f), Vector3(0, 0.95f, 0), Vector3::Up) *
                Matrix::CreatePerspectiveFieldOfView(0.7853982f, 1.0f, 0.1f, 20.0f);
            const Vector4 clip = Vector4::Transform(Vector4(point, 1.0f), viewProjection);
            return Vector2((clip.X / clip.W * 0.5f + 0.5f) * Size, (0.5f - clip.Y / clip.W * 0.5f) * Size);
        }

        static Color At(const std::vector<Color>& pixels, const Vector3& point)
        {
            const Vector2 p = Project(point);
            return pixels[static_cast<std::size_t>(static_cast<int>(p.Y) * Size + static_cast<int>(p.X))];
        }

        // Pixels that differ between two frames inside a box around a model-space point.
        static int Changed(const std::vector<Color>& a, const std::vector<Color>& b, const Vector3& centre, int radius)
        {
            const Vector2 p = Project(centre);
            int changed = 0;
            for (int y = static_cast<int>(p.Y) - radius; y <= static_cast<int>(p.Y) + radius; ++y)
            {
                for (int x = static_cast<int>(p.X) - radius; x <= static_cast<int>(p.X) + radius; ++x)
                {
                    if (x < 0 || y < 0 || x >= Size || y >= Size)
                    {
                        continue;
                    }
                    const auto& u = a[static_cast<std::size_t>(y * Size + x)];
                    const auto& v = b[static_cast<std::size_t>(y * Size + x)];
                    if (std::abs(u.getRProperty() - v.getRProperty()) + std::abs(u.getGProperty() - v.getGProperty()) +
                        std::abs(u.getBProperty() - v.getBProperty()) > 24)
                    {
                        ++changed;
                    }
                }
            }
            return changed;
        }

        static int ChangedOutside(const std::vector<Color>& a, const std::vector<Color>& b, const Vector3& centre, int radius)
        {
            int total = 0;
            for (int i = 0; i < Size * Size; ++i)
            {
                const auto& u = a[static_cast<std::size_t>(i)];
                const auto& v = b[static_cast<std::size_t>(i)];
                if (std::abs(u.getRProperty() - v.getRProperty()) + std::abs(u.getGProperty() - v.getGProperty()) +
                    std::abs(u.getBProperty() - v.getBProperty()) > 24)
                {
                    ++total;
                }
            }
            return total - Changed(a, b, centre, radius);
        }

        void Snapshot(const char* name, const std::vector<Color>& pixels)
        {
            const char* directory = std::getenv("CNA_AVATAR_SNAPSHOT_DIR");
            if (!directory)
            {
                return;
            }
            std::vector<std::uint8_t> rgba;
            for (const auto& c : pixels)
            {
                rgba.insert(rgba.end(), {c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty()});
            }
            CNA::Internal::Graphics::ImageLoader::SavePng(rgba.data(), Size, Size, std::string(directory) + "/" + name + ".png");
        }

        void Check(bool condition, const std::string& what)
        {
            if (!condition)
            {
                failures_.push_back(what);
            }
        }

        static bool Dominant(const Color& c, int channel)
        {
            const int v[3] = {c.getRProperty(), c.getGProperty(), c.getBProperty()};
            return v[channel] > 60 && v[channel] > v[(channel + 1) % 3] + 40 && v[channel] > v[(channel + 2) % 3] + 40;
        }

    public:
        AvatarStandardRenderTest()
        {
            getComponentsProperty().Add(new GamerServicesComponent(*this));
        }

    protected:
        void LoadContent() override
        {
            target_ = std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), Size, Size, false, SurfaceFormat::Color, DepthFormat::Depth24);
            description_ = std::make_unique<AvatarDescription>(FixedDescription());
            renderer_ = std::make_unique<AvatarRenderer>(description_.get(), true);
        }

        void Draw(const GameTime& gameTime) override
        {
            Game::Draw(gameTime);
            ++frame_;
            if (!loadingChecked_)
            {
                loadingChecked_ = true;
                // A second, different avatar is certainly still loading on its first frame.
                AvatarDescription other = AvatarDescription::CreateRandom();
                AvatarRenderer loading(&other, true);
                AvatarAnimation stand(AvatarAnimationPreset::Stand0);
                if (loading.getStateProperty() == AvatarRendererState::Loading)
                {
                    const auto glow = Render(loading, &stand, nullptr);
                    Snapshot("loading", glow);
                    const Color centre = At(glow, Vector3(0, 0.8f, 0));
                    loadingDrawn_ = true;
                    Check(centre != Background, "the loading effect draws while the avatar loads");
                }
                loading.Dispose();
            }
            if (renderer_->getStateProperty() == AvatarRendererState::Loading)
            {
                if (frame_ > 600)
                {
                    failures_.push_back("the avatar never became ready");
                    Finish();
                }
                return;
            }
            if (renderer_->getStateProperty() != AvatarRendererState::Ready)
            {
                failures_.push_back("a valid description must become Ready");
                Finish();
                return;
            }
            Check(renderer_->getBindPoseProperty().getCountProperty() == 71, "71 bind pose bones");

            AvatarAnimation stand(AvatarAnimationPreset::Stand0);
            const auto standing = Render(*renderer_, &stand, nullptr);
            Snapshot("stand0", standing);
            Check(Dominant(At(standing, Vector3(0, 1.12f, 0.12f)), 0), "the chest shows the red shirt");
            Check(Dominant(At(standing, Vector3(0.1f, 0.62f, 0.08f)), 2), "the thigh shows the blue trousers");
            const Color cheek = At(standing, Vector3(-0.075f, 1.6f, 0.11f));
            Check(cheek.getRProperty() > 120 && cheek.getRProperty() > cheek.getBProperty() + 20, "the face shows the skin color");
            Check(standing[0] == Background && standing[Size * Size - 1] == Background, "the background is untouched");

            AvatarExpression shocked;
            shocked.setMouthProperty(AvatarMouth::Shocked);
            shocked.setLeftEyeProperty(AvatarEye::Sleeping);
            shocked.setRightEyeProperty(AvatarEye::Sleeping);
            shocked.setLeftEyebrowProperty(AvatarEyebrow::Raised);
            shocked.setRightEyebrowProperty(AvatarEyebrow::Raised);
            const auto expressive = Render(*renderer_, &stand, &shocked);
            Snapshot("expression", expressive);
            const Vector3 faceCentre(0, 1.6f, 0.14f);
            Check(Changed(standing, expressive, faceCentre, 14) > 20, "a different expression changes the face");
            Check(ChangedOutside(standing, expressive, faceCentre, 14) == 0, "an expression changes only the face");

            AvatarAnimation wave(AvatarAnimationPreset::Wave);
            wave.Update(System::TimeSpan::FromSeconds(1.2), false);
            const auto waving = Render(*renderer_, &wave, nullptr);
            Snapshot("wave", waving);
            Check(Changed(standing, waving, Vector3(-0.3f, 1.55f, 0.0f), 16) > 40, "Wave raises the right arm");

            Snapshot("face", Render(*renderer_, &stand, nullptr, true));
            Snapshot("face-shocked", Render(*renderer_, &stand, &shocked, true));
            AvatarExpression laughing;
            laughing.setMouthProperty(AvatarMouth::Laughing);
            laughing.setLeftEyeProperty(AvatarEye::Laughing);
            laughing.setRightEyeProperty(AvatarEye::Laughing);
            Snapshot("face-laughing", Render(*renderer_, &stand, &laughing, true));
            AvatarExpression angry;
            angry.setMouthProperty(AvatarMouth::Angry);
            angry.setLeftEyeProperty(AvatarEye::Angry);
            angry.setRightEyeProperty(AvatarEye::Angry);
            angry.setLeftEyebrowProperty(AvatarEyebrow::Angry);
            angry.setRightEyebrowProperty(AvatarEyebrow::Angry);
            Snapshot("face-angry", Render(*renderer_, &stand, &angry, true));

            AvatarAnimation celebrate(AvatarAnimationPreset::Celebrate);
            celebrate.Update(System::TimeSpan::FromSeconds(1.2), false);
            Snapshot("celebrate", Render(*renderer_, &celebrate, nullptr));
            Finish();
        }

        void Finish()
        {
            renderer_->Dispose();
            if (failures_.empty())
            {
                std::printf("[PASS] AvatarStandardRender: loading effect %s, ready avatar colors, expression, animation\n",
                            loadingDrawn_ ? "checked" : "not observed");
                result_ = 0;
            }
            for (const auto& failure : failures_)
            {
                std::printf("[FAIL] AvatarStandardRender: %s\n", failure.c_str());
            }
            Exit();
        }

    public:
        int getResult() const { return result_; }
    };
}

int main()
{
    AvatarStandardRenderTest game;
    game.getGraphicsDeviceProperty().SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    game.Run();
    return game.getResult();
}
