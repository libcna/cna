// SPDX-License-Identifier: MS-PL
// cna_avatar_review JOBS.json OUTDIR
//
// Renders every job of an avatar review list (tools/avatar_builder/avatar_review.py) through the
// standard XNA avatar API -- AvatarDescription, AvatarAnimation, AvatarRenderer -- into
// OUTDIR/<name>.png, so the real renderer and the CPU preview can be compared cell for cell.
// Each frame is drawn at twice the job size and box-filtered down. Run it on a private display
// (tools/platform/wayland_test_server.sh or the private GPU runner), never on the desktop.

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
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    std::vector<SharpRuntime::bytecs> FromHex(const std::string& hex)
    {
        std::vector<SharpRuntime::bytecs> bytes;
        for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        {
            bytes.push_back(static_cast<SharpRuntime::bytecs>(std::stoi(hex.substr(i, 2), nullptr, 16)));
        }
        return bytes;
    }

    Vector3 Vec(const nlohmann::json& value) { return Vector3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>()); }

    struct Avatar
    {
        std::unique_ptr<AvatarDescription> description;
        std::unique_ptr<AvatarRenderer> renderer;
    };

    class AvatarReviewGame : public Game
    {
        GraphicsDeviceManager graphics_{this};
        nlohmann::json jobs_;
        std::string out_;
        int size_ = 320;
        Color background_;
        std::unique_ptr<RenderTarget2D> target_;
        std::map<std::string, Avatar> avatars_;
        std::size_t next_ = 0;
        int waited_ = 0;
        int failures_ = 0;

        Avatar& AvatarFor(const std::string& hex)
        {
            auto found = avatars_.find(hex);
            if (found == avatars_.end())
            {
                Avatar avatar;
                avatar.description = std::make_unique<AvatarDescription>(FromHex(hex));
                avatar.renderer = std::make_unique<AvatarRenderer>(avatar.description.get(), false);
                found = avatars_.emplace(hex, std::move(avatar)).first;
            }
            return found->second;
        }

        void Save(const std::string& name, const std::vector<Color>& pixels)
        {
            const int big = size_ * 2;
            std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size_) * size_ * 4);
            for (int y = 0; y < size_; ++y)
            {
                for (int x = 0; x < size_; ++x)
                {
                    int sum[3] = {0, 0, 0};
                    for (int k = 0; k < 4; ++k)
                    {
                        const auto& c = pixels[static_cast<std::size_t>((y * 2 + k / 2) * big + x * 2 + k % 2)];
                        sum[0] += c.getRProperty();
                        sum[1] += c.getGProperty();
                        sum[2] += c.getBProperty();
                    }
                    auto* p = &rgba[(static_cast<std::size_t>(y) * size_ + x) * 4];
                    p[0] = static_cast<std::uint8_t>((sum[0] + 2) / 4);
                    p[1] = static_cast<std::uint8_t>((sum[1] + 2) / 4);
                    p[2] = static_cast<std::uint8_t>((sum[2] + 2) / 4);
                    p[3] = 255;
                }
            }
            CNA::Internal::Graphics::ImageLoader::SavePng(rgba.data(), size_, size_, out_ + "/" + name + ".png");
        }

    public:
        AvatarReviewGame(nlohmann::json jobs, std::string out)
            : jobs_(std::move(jobs))
            , out_(std::move(out))
        {
            getComponentsProperty().Add(new GamerServicesComponent(*this));
            size_ = jobs_.value("size", 320);
            const auto& bg = jobs_["background"];
            background_ = Color(bg[0].get<int>(), bg[1].get<int>(), bg[2].get<int>());
        }

        int getFailures() const { return failures_; }

    protected:
        void LoadContent() override
        {
            target_ = std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), size_ * 2, size_ * 2, false,
                                                       SurfaceFormat::Color, DepthFormat::Depth24);
            // Start every avatar loading at once; the shared loader thread assembles them in turn.
            for (const auto& job : jobs_["jobs"])
            {
                AvatarFor(job["description"].get<std::string>());
            }
        }

        void Draw(const GameTime& gameTime) override
        {
            Game::Draw(gameTime);
            const auto& jobs = jobs_["jobs"];
            while (next_ < jobs.size())
            {
                const auto& job = jobs[next_];
                auto& renderer = *AvatarFor(job["description"].get<std::string>()).renderer;
                if (renderer.getStateProperty() == AvatarRendererState::Loading)
                {
                    if (++waited_ > 3000)
                    {
                        std::printf("[FAIL] %s never became ready\n", job["name"].get<std::string>().c_str());
                        ++failures_;
                        ++next_;
                        waited_ = 0;
                        continue;
                    }
                    return;
                }
                waited_ = 0;
                ++next_;
                const std::string name = job["name"].get<std::string>();
                if (renderer.getStateProperty() != AvatarRendererState::Ready)
                {
                    std::printf("[FAIL] %s is unavailable\n", name.c_str());
                    ++failures_;
                    continue;
                }
                AvatarAnimation animation(static_cast<AvatarAnimationPreset>(job["preset"].get<int>()));
                animation.setCurrentPositionProperty(System::TimeSpan::FromTicks(
                    static_cast<SharpRuntime::longcs>(job["time"].get<double>() * 1.0e7 + 0.5)));
                AvatarExpression expression = animation.getExpressionProperty();
                if (job.contains("expression") && job["expression"].is_array())
                {
                    const auto& e = job["expression"];
                    expression.setMouthProperty(static_cast<AvatarMouth>(e[0].get<int>()));
                    expression.setLeftEyeProperty(static_cast<AvatarEye>(e[1].get<int>()));
                    expression.setRightEyeProperty(static_cast<AvatarEye>(e[2].get<int>()));
                    expression.setLeftEyebrowProperty(static_cast<AvatarEyebrow>(e[3].get<int>()));
                    expression.setRightEyebrowProperty(static_cast<AvatarEyebrow>(e[4].get<int>()));
                }
                const auto& camera = job["camera"];
                renderer.setWorldProperty(Matrix::CreateRotationY(job.value("yaw", 0.0f) * 3.14159265f / 180.0f));
                renderer.setViewProperty(Matrix::CreateLookAt(Vec(camera["eye"]), Vec(camera["target"]), Vector3::Up));
                renderer.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(camera["fov"].get<float>(), 1.0f, 0.05f, 20.0f));
                if (job.contains("light"))
                {
                    const auto& light = job["light"];
                    Vector3 direction = Vec(light["direction"]);
                    direction.Normalize();
                    renderer.setLightDirectionProperty(direction);
                    renderer.setLightColorProperty(Vec(light["color"]));
                    renderer.setAmbientLightColorProperty(Vec(light["ambient"]));
                }
                auto& device = getGraphicsDeviceProperty();
                device.SetRenderTarget(target_.get());
                device.Clear(background_);
                const auto bones = animation.getBoneTransformsProperty();
                renderer.Draw(std::vector<Matrix>(bones.begin(), bones.end()), expression);
                device.SetRenderTarget(nullptr);
                std::vector<Color> pixels(static_cast<std::size_t>(size_) * size_ * 4);
                target_->GetData(pixels.data(), static_cast<int>(pixels.size()));
                Save(name, pixels);
            }
            std::printf("[%s] cna_avatar_review: %zu jobs, %d failures\n", failures_ ? "FAIL" : "PASS", jobs.size(), failures_);
            Exit();
        }
    };
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: cna_avatar_review JOBS.json OUTDIR\n");
        return 2;
    }
    std::ifstream in(argv[1]);
    const auto jobs = nlohmann::json::parse(in, nullptr, false);
    if (jobs.is_discarded() || !jobs.contains("jobs"))
    {
        std::fprintf(stderr, "cna_avatar_review: %s is not a review job list\n", argv[1]);
        return 2;
    }
    std::filesystem::create_directories(argv[2]);
    AvatarReviewGame game(jobs, argv[2]);
    game.getGraphicsDeviceProperty().SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    game.Run();
    return game.getFailures() ? 1 : 0;
}
