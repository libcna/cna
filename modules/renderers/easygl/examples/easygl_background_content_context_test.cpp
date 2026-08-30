// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"

#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
#include <thread>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BackgroundContentContextTest final : public Game
{
public:
    BackgroundContentContextTest()
        : graphics_(std::make_unique<GraphicsDeviceManager>(this))
    {
    }

    ~BackgroundContentContextTest() override
    {
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    [[nodiscard]] int Result() const noexcept { return result_; }

protected:
    void Initialize() override
    {
        getContentProperty().setRootDirectoryProperty(
            "tests/assets/xnb/monogame/windows/uncompressed");
        Game::Initialize();
    }

    void Update(GameTime&) override
    {
        ++updateCount_;
        if (updateCount_ == 2)
        {
            worker_ = std::thread([this]()
            {
                try
                {
                    model_ = std::make_unique<Model>(
                        getContentProperty().Load<Model>("BlenderDefaultCube"));
                }
                catch (...)
                {
                    workerError_ = std::current_exception();
                }
                workerComplete_.store(true, std::memory_order_release);
            });
        }

        if (workerComplete_.load(std::memory_order_acquire) && worker_.joinable())
        {
            worker_.join();
            if (workerError_ != nullptr)
            {
                try
                {
                    std::rethrow_exception(workerError_);
                }
                catch (const std::exception& error)
                {
                    std::fprintf(stderr, "[FAIL] background model load: %s\n", error.what());
                }
                result_ = 1;
                Exit();
                return;
            }

            if (model_ == nullptr || model_->getMeshesProperty().getCountProperty() == 0)
            {
                std::fprintf(stderr, "[FAIL] background model load returned no meshes\n");
                result_ = 1;
                Exit();
                return;
            }
            loadObserved_ = true;
        }

        if (updateCount_ > 600)
        {
            std::fprintf(stderr, "[FAIL] background model load timed out\n");
            result_ = 1;
            Exit();
        }
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color::CornflowerBlue);

        if (!loadObserved_)
        {
            return;
        }

        model_->Draw(
            Matrix::CreateScale(0.5f),
            Matrix::CreateLookAt(
                Vector3(0.0f, 0.0f, 5.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)),
            Matrix::CreatePerspectiveFieldOfView(
                MathHelper::PiOver4, 4.0f / 3.0f, 0.1f, 100.0f));

        ++postLoadDrawCount_;
        if (postLoadDrawCount_ == 2)
        {
            std::printf(
                "[PASS] background XNB model load and two subsequent frames used one native GL context\n");
            result_ = 0;
            Exit();
        }
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::thread worker_;
    std::atomic<bool> workerComplete_{false};
    std::exception_ptr workerError_;
    std::unique_ptr<Model> model_;
    int updateCount_ = 0;
    int postLoadDrawCount_ = 0;
    int result_ = 1;
    bool loadObserved_ = false;
};

int main()
{
    BackgroundContentContextTest game;
    game.Run();
    return game.Result();
}
