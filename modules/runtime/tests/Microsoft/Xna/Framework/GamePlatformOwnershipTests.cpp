// SPDX-License-Identifier: MS-PL
//
// PLAT-46: Game owns the platform instance.
//
// The properties tested here are all about *lifetime*, which is where this kind of ownership
// goes wrong. A platform that is created too late, destroyed too early, or silently duplicated
// still passes every functional test: the graphics device comes up, the loop runs, and the
// breakage only appears as a use-after-free at shutdown or as two implementations disagreeing
// about which one holds a subsystem.

#include <gtest/gtest.h>

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

using namespace Microsoft::Xna::Framework;

namespace
{
    // Every Game constructs a GraphicsDevice, and therefore a renderer. TinyGL keeps its GL
    // context in one process-wide global (glInit/glClose) and offers no make-current entry point,
    // so exactly one TinyGLRenderer -- and thus one live Game -- may exist at a time; its
    // constructor refuses the second one loudly rather than corrupting the first one's context.
    // The nested-lifetime cases below need two simultaneously live games by construction, so on
    // such a renderer there is no behaviour to assert rather than a behaviour that is wrong.
    // The sequential cases (construct, destroy, construct again) are unaffected and still run,
    // which is what keeps the install/uninstall contract covered on every renderer.
#if defined(CNA_RENDERER_TINYGL)
    constexpr bool kSupportsConcurrentGames = false;
#else
    constexpr bool kSupportsConcurrentGames = true;
#endif

    class QuietGame : public Game
    {
    public:
        QuietGame() = default;
        explicit QuietGame(std::unique_ptr<CNA::Platform::IPlatform> platform)
            : Game(std::move(platform))
        {
        }

    protected:
        void LoadContent() override {}
        void Update(GameTime& gameTime) override { Game::Update(gameTime); }
        void Draw(const GameTime& gameTime) override { Game::Draw(gameTime); }
    };

    class GamePlatformOwnershipTest : public ::testing::Test
    {
    protected:
        void SetUp() override { CNA::Platform::ResetCurrentPlatform(); }
        void TearDown() override { CNA::Platform::ResetCurrentPlatform(); }
    };
}

TEST_F(GamePlatformOwnershipTest, GameExposesAPlatformWithAName)
{
    QuietGame game;
    EXPECT_FALSE(game.GetPlatformEXT().GetName().empty());
}

TEST_F(GamePlatformOwnershipTest, TheSameInstanceIsReturnedEveryTime)
{
    // A fresh instance per call would mean each caller acquiring subsystems against a different
    // refcount, which is precisely the bug the single-owner rule prevents.
    QuietGame game;
    EXPECT_EQ(&game.GetPlatformEXT(), &game.GetPlatformEXT());
}

TEST_F(GamePlatformOwnershipTest, ExplicitPlatformIsOwnedAndInstalledBeforeGameMembers)
{
    // Created by name rather than by Create(), so this is genuinely a platform the caller supplied
    // -- but by the *build's own* name, because a Game constructs a GraphicsDevice and the selected
    // renderer may require services a differently-named platform does not offer. Naming "Headless"
    // here made the test pass on a headless renderer and throw "EasyGL renderer does not support
    // OpenGlContext" on a GL one, which says nothing about the ownership contract under test.
    const std::string& platformName = CNA::Platform::PlatformFactory::GetDefaultName();
    std::unique_ptr<CNA::Platform::IPlatform> supplied =
        CNA::Platform::PlatformFactory::Create(platformName);
    CNA::Platform::IPlatform* expected = supplied.get();

    QuietGame game(std::move(supplied));

    EXPECT_EQ(&game.GetPlatformEXT(), expected);
    EXPECT_EQ(&CNA::Platform::GetCurrentPlatform(), expected);
    EXPECT_EQ(game.GetPlatformEXT().GetName(), platformName);
}

TEST_F(GamePlatformOwnershipTest, AFailedConstructionLeavesNothingInstalled)
{
    // The installation happens from platform_'s own initialiser, so that later members find the
    // game's platform instead of lazily creating a second one. That timing also means a throw from
    // any later member unwinds without ~Game ever running: before PLAT-46's install guard, the
    // process-wide accessor stayed aimed at a platform that the same unwind then destroyed, and
    // every later test in the process was reading freed memory through it.
    //
    // Pairing Headless with a renderer that needs a real GL context is one concrete way to reach
    // that path; where the combination happens to be valid there is no failed construction to
    // observe and nothing to assert.
    bool constructionFailed = false;
    try
    {
        QuietGame game(CNA::Platform::PlatformFactory::Create("Headless"));
    }
    catch (const std::exception&)
    {
        constructionFailed = true;
    }

    if (!constructionFailed)
        GTEST_SKIP() << "this renderer accepts the Headless platform; no failed construction to observe";

    EXPECT_FALSE(CNA::Platform::HasCurrentPlatform());
}

TEST_F(GamePlatformOwnershipTest, ExplicitNullPlatformIsRejected)
{
    EXPECT_THROW(
        {
            QuietGame game(std::unique_ptr<CNA::Platform::IPlatform>{});
        },
        std::invalid_argument);
    EXPECT_FALSE(CNA::Platform::HasCurrentPlatform());
}

TEST_F(GamePlatformOwnershipTest, ConstructingAGameInstallsItsPlatformAsTheAmbientOne)
{
    // The static XNA surface (Keyboard::GetState, StorageDevice, TitleContainer) reaches the
    // platform ambiently. If a Game did not install its own, that surface would lazily create a
    // *second* platform and the process would hold two.
    QuietGame game;
    EXPECT_TRUE(CNA::Platform::HasCurrentPlatform());
    EXPECT_EQ(&CNA::Platform::GetCurrentPlatform(), &game.GetPlatformEXT());
}

TEST_F(GamePlatformOwnershipTest, DestroyingAGameUninstallsItsPlatform)
{
    // Leaving the pointer installed would leave the static API surface aimed at freed memory --
    // a use-after-free that only fires on the next StorageDevice call, long after the game went
    // away.
    {
        QuietGame game;
        ASSERT_TRUE(CNA::Platform::HasCurrentPlatform());
    }
    EXPECT_FALSE(CNA::Platform::HasCurrentPlatform());
}

TEST_F(GamePlatformOwnershipTest, AnInnerGameRestoresTheOuterOnesPlatformWhenItGoesAway)
{
    if (!kSupportsConcurrentGames)
        GTEST_SKIP() << "this renderer allows only one live Game per process";

    // Two live games is not a shape a real title has, but tests construct them in nested scopes
    // routinely. The inner one takes over the installation while it exists and must hand it back
    // on the way out: clearing instead of restoring would leave the outer, still-running game's
    // static API surface pointing at nothing, and that only surfaces later, at the next
    // StorageDevice or TitleContainer call.
    QuietGame outer;
    ASSERT_EQ(&CNA::Platform::GetCurrentPlatform(), &outer.GetPlatformEXT());

    {
        QuietGame inner;
        ASSERT_EQ(&CNA::Platform::GetCurrentPlatform(), &inner.GetPlatformEXT());
    }

    ASSERT_TRUE(CNA::Platform::HasCurrentPlatform());
    EXPECT_EQ(&CNA::Platform::GetCurrentPlatform(), &outer.GetPlatformEXT());
}

TEST_F(GamePlatformOwnershipTest, AnOuterGameOutlivingAnInnerOneDoesNotOverwriteTheInstallation)
{
    if (!kSupportsConcurrentGames)
        GTEST_SKIP() << "this renderer allows only one live Game per process";

    // The mirror case: destroying a game whose platform is no longer the installed one must
    // leave the installation alone rather than restore its own saved predecessor over the top.
    QuietGame* outer = new QuietGame();
    QuietGame inner;
    ASSERT_EQ(&CNA::Platform::GetCurrentPlatform(), &inner.GetPlatformEXT());

    delete outer;

    ASSERT_TRUE(CNA::Platform::HasCurrentPlatform());
    EXPECT_EQ(&CNA::Platform::GetCurrentPlatform(), &inner.GetPlatformEXT());
}

TEST_F(GamePlatformOwnershipTest, OutOfOrderDestructionLeavesNoDanglingInstallation)
{
    if (!kSupportsConcurrentGames)
        GTEST_SKIP() << "this renderer allows only one live Game per process";

    // The case that rules out the obvious implementation. If each game merely remembered "the
    // platform I displaced" and restored it, the inner game below would restore a pointer to the
    // outer game's platform -- which was already destroyed -- and the ambient accessor would hand
    // out freed memory to the next StorageDevice call. Nothing about that is visible at the point
    // of the mistake; it surfaces as a corrupted read somewhere else entirely.
    QuietGame* outer = new QuietGame();
    {
        QuietGame inner;
        ASSERT_EQ(&CNA::Platform::GetCurrentPlatform(), &inner.GetPlatformEXT());
        delete outer;
        // The outer game is gone but is not the installed one, so the installation is untouched.
        ASSERT_EQ(&CNA::Platform::GetCurrentPlatform(), &inner.GetPlatformEXT());
    }

    EXPECT_FALSE(CNA::Platform::HasCurrentPlatform())
        << "nothing is left alive to be installed; anything else here is a dangling pointer";
}

TEST_F(GamePlatformOwnershipTest, EachGameOwnsItsOwnPlatform)
{
    if (!kSupportsConcurrentGames)
        GTEST_SKIP() << "this renderer allows only one live Game per process";

    QuietGame first;
    QuietGame second;
    EXPECT_NE(&first.GetPlatformEXT(), &second.GetPlatformEXT());
}

TEST_F(GamePlatformOwnershipTest, ConsecutiveGamesEachInstallAndUninstallCleanly)
{
    // The repeat is the point: a stale installed pointer from the first game would make the
    // second one's assertions pass by accident on the first iteration and fail on a later one.
    for (int i = 0; i < 3; ++i)
    {
        QuietGame game;
        ASSERT_TRUE(CNA::Platform::HasCurrentPlatform()) << "iteration " << i;
        ASSERT_EQ(&CNA::Platform::GetCurrentPlatform(), &game.GetPlatformEXT())
            << "iteration " << i;
    }
    EXPECT_FALSE(CNA::Platform::HasCurrentPlatform());
}

TEST_F(GamePlatformOwnershipTest, ThePlatformIsUsableWhileTheGameIsAlive)
{
    // Ownership is only worth anything if the instance actually works: a timer that never
    // advances would make the fixed-timestep loop spin forever once PLAT-48 re-points it here.
    QuietGame game;
    CNA::Platform::IPlatform& platform = game.GetPlatformEXT();

    EXPECT_GT(platform.GetPerformanceFrequency(), 0u);
    const std::uint64_t first = platform.GetPerformanceCounter();
    platform.Delay(1);
    EXPECT_GE(platform.GetPerformanceCounter(), first);
}
