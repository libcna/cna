// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0154: a drop reaches an XNA game as GameWindow's CNAEXT FileDropEXT and
// TextDropEXT. The platform's DropEvent sequence is scripted here and run through a real
// Game::RunOneFrame(); the X11 and SDL3 implementations produce that sequence in their own suites.

#include <gtest/gtest.h>

#include "CNA/Platform/PlatformEvent.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "Microsoft/Xna/Framework/FileDropEventArgsEXT.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/TextDropEventArgsEXT.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;

namespace
{
    namespace Platform = CNA::Platform;

    /** This build's own platform, with its events replaced by a script. */
    class ScriptedPlatform final : public Platform::Testing::PlatformTestDecorator
    {
    public:
        explicit ScriptedPlatform(std::unique_ptr<Platform::IPlatform> inner)
            : PlatformTestDecorator(std::move(inner))
        {
        }

        void Queue(std::vector<Platform::PlatformEvent> events)
        {
            for (Platform::PlatformEvent& event : events)
            {
                queued_.push_back(std::move(event));
            }
        }

        void PollEvents(std::vector<Platform::PlatformEvent>& destination) override
        {
            destination.clear();
            destination.swap(queued_);
        }

    private:
        std::vector<Platform::PlatformEvent> queued_;
    };

    class DropGame final : public Game
    {
    public:
        explicit DropGame(std::unique_ptr<Platform::IPlatform> platform) : Game(std::move(platform)) {}

    protected:
        void LoadContent() override {}
        void Update(GameTime& gameTime) override { Game::Update(gameTime); }
        void Draw(const GameTime& gameTime) override { Game::Draw(gameTime); }
    };

    Platform::PlatformEvent Drop(const Platform::DropEventKind kind, std::string data = {})
    {
        Platform::DropEvent drop;
        drop.window = 1;
        drop.kind = kind;
        drop.data = std::move(data);
        if (kind != Platform::DropEventKind::Begin && kind != Platform::DropEventKind::Complete)
        {
            drop.x = 10.0f;
            drop.y = 20.0f;
        }
        return drop;
    }

    class GameWindowDropTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            // The build's own platform rather than "Headless": GraphicsDeviceManager creates the
            // selected renderer's device on it, and a renderer that presents to a native surface
            // (DirectX11, every OpenGL profile) has none on Headless -- so on those builds SetUp
            // threw before a single drop was scripted. Only the event stream is under test here,
            // and the decorator replaces that whichever platform is underneath.
            auto platform = std::make_unique<ScriptedPlatform>(Platform::PlatformFactory::Create());
            platform_ = platform.get();
            game_ = std::make_unique<DropGame>(std::move(platform));
            manager_ = std::make_unique<GraphicsDeviceManager>(game_.get());
            game_->RunOneFrame();

            GameWindow& window = game_->getWindowProperty();
            window.FileDropEXT += [this](System::Object*, const FileDropEventArgsEXT& args) {
                files_.push_back(args.getFilesProperty());
                order_.push_back("files");
            };
            window.TextDropEXT += [this](System::Object*, const TextDropEventArgsEXT& args) {
                texts_.push_back(args.getTextProperty());
                order_.push_back("text");
            };
        }

        void TearDown() override
        {
            if (game_ != nullptr)
            {
                game_->RunApplication = false;
            }
            manager_.reset();
            game_.reset();
        }

        void Frame(std::vector<Platform::PlatformEvent> events)
        {
            platform_->Queue(std::move(events));
            game_->RunOneFrame();
        }

        ScriptedPlatform* platform_ = nullptr;
        std::unique_ptr<DropGame> game_;
        std::unique_ptr<GraphicsDeviceManager> manager_;
        std::vector<std::vector<std::string>> files_;
        std::vector<std::string> texts_;
        std::vector<std::string> order_;
    };
}

TEST_F(GameWindowDropTest, EveryFileOfADropArrivesTogetherWhenTheDropIsComplete)
{
    Frame({Drop(Platform::DropEventKind::Begin), Drop(Platform::DropEventKind::Position)});
    EXPECT_TRUE(files_.empty()) << "a drag in progress is not a drop";

    Frame({Drop(Platform::DropEventKind::File, "/tmp/a.xnb"),
           Drop(Platform::DropEventKind::File, "/tmp/b.xnb")});
    EXPECT_TRUE(files_.empty()) << "the drop is not complete yet";

    Frame({Drop(Platform::DropEventKind::Complete)});
    ASSERT_EQ(files_.size(), 1u);
    EXPECT_EQ(files_[0], (std::vector<std::string>{"/tmp/a.xnb", "/tmp/b.xnb"}));
    EXPECT_TRUE(texts_.empty());
}

TEST_F(GameWindowDropTest, TextIsRaisedPerTextAfterTheFiles)
{
    Frame({Drop(Platform::DropEventKind::Begin),
           Drop(Platform::DropEventKind::Text, "https://example.org/"),
           Drop(Platform::DropEventKind::File, "/tmp/level.xnb"),
           Drop(Platform::DropEventKind::Text, "second"),
           Drop(Platform::DropEventKind::Complete)});
    EXPECT_EQ(order_, (std::vector<std::string>{"files", "text", "text"}));
    EXPECT_EQ(texts_, (std::vector<std::string>{"https://example.org/", "second"}));
}

TEST_F(GameWindowDropTest, ADragThatLeavesRaisesNothing)
{
    Frame({Drop(Platform::DropEventKind::Begin), Drop(Platform::DropEventKind::Position),
           Drop(Platform::DropEventKind::Complete)});
    EXPECT_TRUE(order_.empty());
}

TEST_F(GameWindowDropTest, OneDropsFilesDoNotLeakIntoTheNext)
{
    Frame({Drop(Platform::DropEventKind::Begin), Drop(Platform::DropEventKind::File, "/tmp/first"),
           Drop(Platform::DropEventKind::Complete)});
    // A drop whose Complete never came -- its source died -- then a new one.
    Frame({Drop(Platform::DropEventKind::Begin), Drop(Platform::DropEventKind::File, "/tmp/lost")});
    Frame({Drop(Platform::DropEventKind::Begin), Drop(Platform::DropEventKind::File, "/tmp/second"),
           Drop(Platform::DropEventKind::Complete)});
    ASSERT_EQ(files_.size(), 2u);
    EXPECT_EQ(files_[0], (std::vector<std::string>{"/tmp/first"}));
    EXPECT_EQ(files_[1], (std::vector<std::string>{"/tmp/second"}));
}

TEST_F(GameWindowDropTest, ADesktopsOpenRequestNamingNoWindowStillReachesTheGame)
{
    Platform::DropEvent open;
    open.kind = Platform::DropEventKind::File;
    open.data = "/tmp/opened.xnb";
    Platform::DropEvent done;
    done.kind = Platform::DropEventKind::Complete;
    Frame({open, done});
    ASSERT_EQ(files_.size(), 1u);
    EXPECT_EQ(files_[0], (std::vector<std::string>{"/tmp/opened.xnb"}));
}

TEST(FileDropEventArgsEXTTests, CarryTheirFilesAndText)
{
    const FileDropEventArgsEXT files({"/a", "/b"});
    EXPECT_EQ(files.getFilesProperty(), (std::vector<std::string>{"/a", "/b"}));
    const TextDropEventArgsEXT text("dropped");
    EXPECT_EQ(text.getTextProperty(), "dropped");
}
