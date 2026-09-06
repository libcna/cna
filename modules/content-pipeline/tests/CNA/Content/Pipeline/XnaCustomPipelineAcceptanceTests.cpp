// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-260: does a third party's content pipeline actually work?
//
// `modules/content-pipeline/examples/xna-custom-pipeline.cpp` is a user's code: its own
// intermediate types, its own source extension, an importer and processor derived from XNA's own
// bases, and a `ContentTypeWriter<T>` naming a runtime reader in the game's assembly. It is linked
// into its own executable and knows nothing about this test; this test runs that executable the way
// a user would and checks that everything an extension has to be able to do actually happened.
//
// A unit test proving the same route in-process already exists. What it cannot prove is that the
// pieces are reachable from outside: the registration functions, the container options a factory is
// given, the coordinator's dependency and fingerprint bookkeeping for a route it has never heard of,
// and the diagnostics a user sees when their own importer refuses a file.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace
{
    std::filesystem::path Locate(const std::filesystem::path& relative)
    {
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        return relative;
    }

#if !defined(_WIN32)
    /** @brief Runs the user's compiler and captures everything it said. */
    int RunCompiler(const std::vector<std::string>& arguments, std::string& log)
    {
        int pipes[2] = {-1, -1};
        if (::pipe(pipes) != 0) { return -1; }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addclose(&actions, pipes[0]);
        posix_spawn_file_actions_adddup2(&actions, pipes[1], 1);
        posix_spawn_file_actions_adddup2(&actions, pipes[1], 2);
        posix_spawn_file_actions_addclose(&actions, pipes[1]);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(CNA_XNA_CUSTOM_PIPELINE_PATH));
        for (const std::string& argument : arguments)
        {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);

        pid_t pid = -1;
        const int spawned = posix_spawn(&pid, CNA_XNA_CUSTOM_PIPELINE_PATH, &actions, nullptr,
                                        argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(pipes[1]);
        if (spawned != 0) { ::close(pipes[0]); return -1; }

        log.clear();
        char buffer[4096];
        for (ssize_t read = 0; (read = ::read(pipes[0], buffer, sizeof(buffer))) > 0;)
        {
            log.append(buffer, static_cast<std::size_t>(read));
        }
        ::close(pipes[0]);
        int status = 0;
        if (::waitpid(pid, &status, 0) < 0) { return -1; }
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
#endif

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    /** @brief What an `.xnb` says about itself, read straight out of the container. */
    struct XnbShape
    {
        /** @brief Every type reader named in the table, in order. */
        std::vector<std::string> readers;

        /** @brief How many shared resources follow the primary object. */
        std::int32_t sharedResources = -1;
    };

    /**
     * @brief Reads the type-reader table and the shared-resource count.
     *
     * Deliberately not through a `ContentManager`: the readers this file names live in a game's
     * assembly, and the point is what the container declares rather than what could load it.
     */
    XnbShape ReadXnbShape(const std::vector<std::uint8_t>& bytes)
    {
        XnbShape shape;
        std::size_t at = 10u;  // 'XNB', platform, version, flags, then the four-byte file size.
        const auto sevenBit = [&bytes, &at]
        {
            std::int32_t value = 0;
            int shift = 0;
            while (at < bytes.size())
            {
                const std::uint8_t byte = bytes[at++];
                value |= static_cast<std::int32_t>(byte & 0x7Fu) << shift;
                if ((byte & 0x80u) == 0u) { break; }
                shift += 7;
            }
            return value;
        };
        const auto text = [&bytes, &at, &sevenBit]
        {
            const std::int32_t length = sevenBit();
            std::string value;
            for (std::int32_t index = 0; index < length && at < bytes.size(); ++index)
            {
                value.push_back(static_cast<char>(bytes[at++]));
            }
            return value;
        };
        const std::int32_t readerCount = sevenBit();
        for (std::int32_t index = 0; index < readerCount; ++index)
        {
            shape.readers.push_back(text());
            at += 4u;  // the reader's version
        }
        shape.sharedResources = sevenBit();
        return shape;
    }

    /** @brief A scratch copy of the committed fixture, removed when the test ends. */
    class QuestSource
    {
    public:
        explicit QuestSource(const std::string& label)
            : root_(std::filesystem::temp_directory_path() / ("cna_xnapp260_" + label))
        {
            std::filesystem::remove_all(root_);
            std::filesystem::create_directories(Source());
            std::filesystem::create_directories(Output());
            std::filesystem::copy(Locate("tests/assets/xna_custom_pipeline"), Source(),
                                  std::filesystem::copy_options::recursive |
                                      std::filesystem::copy_options::overwrite_existing);
        }
        ~QuestSource()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }
        QuestSource(const QuestSource&) = delete;
        QuestSource& operator=(const QuestSource&) = delete;

        [[nodiscard]] std::filesystem::path Source() const { return root_ / "src"; }
        [[nodiscard]] std::filesystem::path Output() const { return root_ / "out"; }

        /** @brief The arguments a user would type. */
        [[nodiscard]] std::vector<std::string> Arguments() const
        {
            return {"build", Source().string(), "-o", Output().string(), "--format", "xnb",
                    "--config", (Source() / "quest-config.json").string()};
        }

    private:
        std::filesystem::path root_;
    };
}

#if defined(_WIN32) || !defined(CNA_XNA_CUSTOM_PIPELINE_PATH)
TEST(XnaCustomPipelineAcceptanceTest, TheExampleCompilerIsNotAvailableHere)
{
    GTEST_SKIP() << "the user-owned XNA pipeline example is not built in this configuration";
}
#else

// The route runs, and every name in it is the user's own.
TEST(XnaCustomPipelineAcceptanceTest, AUsersOwnRouteReachesAnXnbUnderItsOwnNames)
{
    const QuestSource source("route");
    std::string log;
    ASSERT_EQ(RunCompiler(source.Arguments(), log), 0) << log;

    EXPECT_NE(log.find("QuestImporter -> QuestProcessor -> CNA.XnaObjectXnbWriter[QuestGame.Quest]"),
              std::string::npos)
        << log;
    // The processor's own important message reaches the user's console. XNA has two message
    // levels and this is the one documented as reaching the user even at low verbosity; an
    // ordinary LogMessage, like the importer's step count, stays with the rest of the detail.
    EXPECT_NE(log.find("message (QuestProcessor): built the reward asset"), std::string::npos) << log;
    EXPECT_EQ(log.find("read 3 step(s)"), std::string::npos)
        << "an ordinary message is detail, not console output\n" << log;

    const std::filesystem::path built = source.Output() / "dawn.xnb";
    ASSERT_TRUE(std::filesystem::is_regular_file(built)) << log;
    const XnbShape shape = ReadXnbShape(ReadBytes(built));

    // The runtime readers named in the container are the game's, not CNA's.
    ASSERT_FALSE(shape.readers.empty());
    EXPECT_EQ(shape.readers.front(), "QuestGame.QuestReader, QuestGame");
    EXPECT_NE(std::find(shape.readers.begin(), shape.readers.end(),
                        "QuestGame.QuestStepReader, QuestGame"),
              shape.readers.end());

    // The hub is one of the three steps and is written once, however many references name it.
    EXPECT_EQ(shape.sharedResources, 1);
}

// The bookkeeping the coordinator does for a route it has never heard of.
TEST(XnaCustomPipelineAcceptanceTest, DependenciesParametersAndReferencesAreAllRecorded)
{
    const QuestSource source("manifest");
    std::string log;
    ASSERT_EQ(RunCompiler(source.Arguments(), log), 0) << log;

    const std::filesystem::path manifest = source.Output() / ".cna-content-manifest.json";
    ASSERT_TRUE(std::filesystem::is_regular_file(manifest));
    std::ifstream stream(manifest);
    const std::string text{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};

    // The sidecar the importer read, recorded because the importer said so.
    EXPECT_NE(text.find("dawn.notes"), std::string::npos) << text;
    // The nested build's source, and the reference the quest carries to what it produced.
    EXPECT_NE(text.find("reward.quest"), std::string::npos) << text;
    EXPECT_NE(text.find("\"logicalName\":\"reward\""), std::string::npos) << text;
    // The processor's own parameters, by the names it declared.
    EXPECT_NE(text.find("Repeats"), std::string::npos) << text;
    EXPECT_NE(text.find("QuestGame.QuestReader, QuestGame"), std::string::npos) << text;
}

// Incremental rebuilds work for a user's route, and the dependency the *importer* declared is what
// drives them: touching a file that is not the primary source still rebuilds the asset.
TEST(XnaCustomPipelineAcceptanceTest, ARepeatBuildSkipsAndAChangedDependencyRebuilds)
{
    const QuestSource source("incremental");
    std::string first;
    ASSERT_EQ(RunCompiler(source.Arguments(), first), 0) << first;
    EXPECT_NE(first.find("Built: 2"), std::string::npos) << first;

    std::string second;
    ASSERT_EQ(RunCompiler(source.Arguments(), second), 0) << second;
    EXPECT_NE(second.find("Built: 0  Skipped: 2"), std::string::npos) << second;

    { std::ofstream(source.Source() / "dawn.notes") << "chapter two\n"; }
    std::string third;
    ASSERT_EQ(RunCompiler(source.Arguments(), third), 0) << third;
    EXPECT_NE(third.find("[BUILD] dawn"), std::string::npos)
        << "a changed sidecar must rebuild the quest that reads it\n" << third;
    EXPECT_NE(third.find("[SKIP] reward"), std::string::npos)
        << "and must not rebuild the asset that does not\n" << third;
}

// A user's own refusal is a build failure with the user's own sentence, not a crash and not a
// silent skip.
TEST(XnaCustomPipelineAcceptanceTest, AUsersOwnRefusalIsReportedAsTheirOwnDiagnostic)
{
    const QuestSource source("refusal");
    { std::ofstream(source.Source() / "empty.quest") << "A Quest With No Steps\n"; }

    std::string log;
    EXPECT_NE(RunCompiler(source.Arguments(), log), 0) << log;
    EXPECT_NE(log.find("declares no steps"), std::string::npos) << log;
    EXPECT_NE(log.find("QuestImporter"), std::string::npos) << log;
    EXPECT_FALSE(std::filesystem::exists(source.Output() / "empty.xnb"));
}
#endif
