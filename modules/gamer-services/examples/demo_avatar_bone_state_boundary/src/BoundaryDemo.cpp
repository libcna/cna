#include "BoundaryDemo.hpp"

#include "Microsoft/Xna/Framework/GamerServices/AvatarBone.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "common/ScreenshotEXT.hpp"
#include "common/SimpleFontEXT.hpp"

#include <cmath>
#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;

namespace
{
    constexpr int kFramesAfterReady = 240;

    const char* StateName(AvatarRendererState state)
    {
        switch (state)
        {
            case AvatarRendererState::Loading: return "Loading";
            case AvatarRendererState::Ready: return "Ready";
            case AvatarRendererState::Unavailable: return "Unavailable";
        }
        return "?";
    }

    // Replaces a local bone transform's rotation, keeping its translation (bind offset).
    Matrix Rotated(const Matrix& local, const Matrix& rotation)
    {
        return rotation * Matrix::CreateTranslation(local.getTranslationProperty());
    }
}

BoundaryDemo::BoundaryDemo()
{
    // HiDef lets the --screenshot option read the back buffer.
    graphics_.setGraphicsProfileProperty(GraphicsProfile::HiDef);
    getComponentsProperty().Add(new GamerServicesComponent(*this));
}

BoundaryDemo::~BoundaryDemo() = default;

void BoundaryDemo::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, {255, 255, 255, 255}));
    font_ = CNAExamplesEXT::MakeSimpleFontEXT(device);

    AvatarDescription none(std::vector<SharpRuntime::bytecs>(1021, 0));
    AvatarRenderer unavailable(&none);
    std::printf("1. A description without an avatar: IsValid=%d, renderer State=%s\n", none.getIsValidProperty(),
                StateName(unavailable.getStateProperty()));

    description_ = std::make_unique<AvatarDescription>(AvatarDescription::CreateRandom());
    renderer_ = std::make_unique<AvatarRenderer>(description_.get());
    animation_ = std::make_unique<AvatarAnimation>(AvatarAnimationPreset::Stand4);
    std::printf("2. A random avatar (%s, %.2f m): State=%s right after construction\n",
                description_->getBodyTypeProperty() == AvatarBodyType::Male ? "male" : "female",
                description_->getHeightProperty(), StateName(renderer_->getStateProperty()));
    const auto parents = renderer_->getParentBonesProperty();
    std::printf("3. ParentBones: %d entries; Head(%d)->%d, WristLeft(%d)->%d, FingerThumb3Right(%d)->%d\n",
                parents.getCountProperty(), static_cast<int>(AvatarBone::Head), parents[static_cast<int>(AvatarBone::Head)],
                static_cast<int>(AvatarBone::WristLeft), parents[static_cast<int>(AvatarBone::WristLeft)],
                static_cast<int>(AvatarBone::FingerThumb3Right), parents[static_cast<int>(AvatarBone::FingerThumb3Right)]);
}

void BoundaryDemo::Update(GameTime& gameTime)
{
    Game::Update(gameTime);
    if (Keyboard::GetState().IsKeyDown(Keys::Escape))
    {
        Exit();
        return;
    }
    ++frame_;
    seconds_ += gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty();
    animation_->Update(gameTime.getElapsedGameTimeProperty(), true);
    if (readyFrame_ < 0 && renderer_->getStateProperty() == AvatarRendererState::Ready)
    {
        readyFrame_ = frame_;
        const auto pose = renderer_->getBindPoseProperty();
        const auto parents = renderer_->getParentBonesProperty();
        Vector3 head;
        for (int bone = static_cast<int>(AvatarBone::Head); bone >= 0; bone = parents[bone])
        {
            head += pose[bone].getTranslationProperty();
        }
        std::printf("4. Ready after %d frames; BindPose has %d local transforms, head joint %.2f m above the feet\n",
                    frame_, pose.getCountProperty(), head.Y);
        std::printf("5. Drawing Stand4 with a game-posed head and left arm for %d frames\n", kFramesAfterReady);
    }
    if (readyFrame_ >= 0 && frame_ - readyFrame_ >= kFramesAfterReady)
    {
        Exit();
    }
}

void BoundaryDemo::Draw(const GameTime& gameTime)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(88, 112, 150, 255));
    const auto& viewport = device.getViewportProperty();
    const float aspect = viewport.getHeightProperty() > 0
        ? static_cast<float>(viewport.getWidthProperty()) / static_cast<float>(viewport.getHeightProperty())
        : 1.0f;
    renderer_->setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 1.3f, 2.6f), Vector3(0.0f, 1.0f, 0.0f), Vector3::Up));
    renderer_->setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(0.75f, aspect, 0.1f, 50.0f));

    // BoneTransforms are local to the parent: pose two bones and hand the rest over unchanged.
    const auto source = animation_->getBoneTransformsProperty();
    std::vector<Matrix> bones(source.begin(), source.end());
    const float t = static_cast<float>(seconds_);
    const int head = static_cast<int>(AvatarBone::Head);
    const int shoulder = static_cast<int>(AvatarBone::ShoulderLeft);
    bones[head] = Rotated(bones[head], Matrix::CreateRotationY(0.6f * std::sin(t * 1.3f)) * Matrix::CreateRotationX(-0.15f));
    bones[shoulder] = Rotated(bones[shoulder], Matrix::CreateRotationZ(1.2f + 0.3f * std::sin(t * 2.0f)));
    renderer_->Draw(bones, animation_->getExpressionProperty());

    if (showHelp_)
    {
        spriteBatch_->Begin();
        const char* lines[] = {"Standard AvatarRenderer state, ParentBones and BindPose", "Custom bones: head and left arm posed by the game",
                               "Esc: quit"};
        spriteBatch_->Draw(*whitePixel_, Rectangle(8, 8, 470, 56), Color(255, 255, 255, 210));
        float y = 16.0f;
        for (const char* line : lines)
        {
            spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color::Black, 0.0f, Vector2::Zero, 1.5f, SpriteEffects::None, 0.0f);
            y += 13.0f;
        }
        spriteBatch_->End();
    }
    if (readyFrame_ >= 0 && frame_ - readyFrame_ >= kFramesAfterReady / 2 && !screenshotPath_.empty())
    {
        SaveBackBufferScreenshotEXT(device, screenshotPath_);
        screenshotPath_.clear();
    }
    Game::Draw(gameTime);
}

GetTypeNameCPP(BoundaryDemo, "BoundaryDemo")
