// SPDX-License-Identifier: MS-PL
// ISSUE-028: constructor path/refusal cases measured by WaveBankPathOracle.cs.
// No native audio playback; a real minimal AudioEngine backs every non-null call.
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "Microsoft/Xna/Framework/TitleLocation.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IO/FileNotFoundException.hpp"

namespace
{
    namespace fs = std::filesystem;
    using Microsoft::Xna::Framework::Audio::AudioEngine;
    using Microsoft::Xna::Framework::Audio::WaveBank;
    using Microsoft::Xna::Framework::TitleLocation;

    class WaveBankConstructorContractTest : public ::testing::TestWithParam<bool>
    {
    protected:
        fs::path previousWorking;
        std::string previousTitle;
        fs::path root, title, working;
        bool ownsRoot = false;
        std::unique_ptr<AudioEngine> engine;

        void Write(const fs::path& file, const std::vector<unsigned char>& bytes)
        {
            std::ofstream output(file, std::ios::binary);
            ASSERT_TRUE(output.is_open());
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            output.close();
            ASSERT_TRUE(output);
        }

        void SetUp() override
        {
            previousWorking = fs::current_path();
            previousTitle = TitleLocation::Path();
            root = fs::temp_directory_path() / ("cna-wave-ctor-contract-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            ownsRoot = fs::create_directory(root);
            ASSERT_TRUE(ownsRoot);
            title = root / "title";
            working = root / "working";
            ASSERT_TRUE(fs::create_directory(title));
            ASSERT_TRUE(fs::create_directory(working));
            // Same zero-category/variable/RPC XGS shape used by native WaveBankTests.
            std::vector<unsigned char> settings(0x50, 0);
            settings[0] = 'X'; settings[1] = 'G'; settings[2] = 'S'; settings[3] = 'F';
            settings[4] = 46;
            ASSERT_NO_FATAL_FAILURE(Write(root / "settings.xgs", settings));
            engine = std::make_unique<AudioEngine>((root / "settings.xgs").string());
            ASSERT_NO_FATAL_FAILURE(Write(title / "asset.xwb", {0, 1, 2, 3, 4}));
            TitleLocation::setPathProperty(title.string());
            fs::current_path(working);
        }

        void TearDown() override
        {
            engine.reset();
            std::error_code error;
            fs::current_path(previousWorking, error);
            EXPECT_FALSE(error);
            TitleLocation::setPathProperty(previousTitle);
            if (ownsRoot)
            {
                fs::remove_all(root, error);
                EXPECT_FALSE(error);
            }
        }

        std::unique_ptr<WaveBank> Open(AudioEngine* owner, const std::string& file)
        {
            if (GetParam()) return std::make_unique<WaveBank>(owner, file, 0, 16);
            return std::make_unique<WaveBank>(owner, file);
        }

        template<class Expected>
        void ExpectRefusal(const std::string& file)
        {
            try
            {
                auto bank = Open(engine.get(), file);
                FAIL() << "Expected managed constructor refusal before parsing native bank metadata";
            }
            catch (const Expected& error)
            {
                EXPECT_EQ(typeid(error), typeid(Expected));
            }
            catch (const std::exception& error)
            {
                FAIL() << "Wrong exception family: " << error.what();
            }
        }

        void ExpectMissing(const std::string& file, const fs::path& fullPath)
        {
            try
            {
                auto bank = Open(engine.get(), file);
                FAIL() << "Expected FileNotFoundException, not an unprepared bank";
            }
            catch (const System::IO::FileNotFoundException& error)
            {
                EXPECT_EQ(typeid(error), typeid(System::IO::FileNotFoundException));
                EXPECT_EQ(fs::path(error.getFileNameProperty()), fullPath);
            }
            catch (const std::exception& error)
            {
                FAIL() << "Wrong exception family: " << error.what();
            }
        }
    };

    TEST_P(WaveBankConstructorContractTest, RelativeLookupDoesNotSubstituteTitleFile)
    {
        ExpectMissing("asset.xwb", working / "asset.xwb");
    }

    TEST_P(WaveBankConstructorContractTest, RelativeWorkingFileGetsManagedHeaderValidation)
    {
        ASSERT_NO_FATAL_FAILURE(Write(working / "asset.xwb", {0, 1, 2, 3, 4}));
        ExpectRefusal<System::ArgumentException>("asset.xwb");
    }

    TEST_P(WaveBankConstructorContractTest, AbsoluteTitleFileGetsTheSameHeaderValidation)
    {
        ExpectRefusal<System::ArgumentException>((title / "asset.xwb").string());
    }

    TEST_P(WaveBankConstructorContractTest, RemovedWorkingFileDoesNotFallBackToTitle)
    {
        ASSERT_NO_FATAL_FAILURE(Write(working / "asset.xwb", {0, 1, 2, 3, 4}));
        ASSERT_TRUE(fs::remove(working / "asset.xwb"));
        ExpectMissing("asset.xwb", working / "asset.xwb");
    }

    TEST_P(WaveBankConstructorContractTest, MissingAbsoluteFileRetainsItsFullName)
    {
        const auto missing = title / "missing.xwb";
        ExpectMissing(missing.string(), missing);
    }

    TEST_P(WaveBankConstructorContractTest, NullEngineIsRefusedBeforeFileLookup)
    {
        EXPECT_THROW((void)Open(nullptr, "asset.xwb"), System::ArgumentNullException);
    }

    TEST_P(WaveBankConstructorContractTest, EmptyFileNameIsRefusedBeforeFileLookup)
    {
        ExpectRefusal<System::ArgumentNullException>("");
    }

    TEST_P(WaveBankConstructorContractTest, EmptyExistingFileGetsManagedHeaderValidation)
    {
        ASSERT_NO_FATAL_FAILURE(Write(working / "empty.xwb", {}));
        ExpectRefusal<System::ArgumentException>("empty.xwb");
    }

    TEST_P(WaveBankConstructorContractTest, FourByteMagicIsNotEnoughForHeaderValidation)
    {
        ASSERT_NO_FATAL_FAILURE(Write(working / "short.xwb", {'W', 'B', 'N', 'D'}));
        ExpectRefusal<System::ArgumentException>("short.xwb");
    }

    // The genuine reference distinguishes CheckWaveBankHeader's managed rejection from
    // failed native resource creation. This is a source-backed boundary, not an XACT
    // playback result from the pre-native fourteen-case Windows oracle.
    TEST_P(WaveBankConstructorContractTest, MatchingMagicWithInvalidMetadataCannotCreateBank)
    {
        ASSERT_NO_FATAL_FAILURE(Write(working / "truncated.xwb", {'W', 'B', 'N', 'D', 0}));
        ExpectRefusal<System::InvalidOperationException>("truncated.xwb");
    }

    INSTANTIATE_TEST_SUITE_P(MemoryAndStreaming, WaveBankConstructorContractTest,
                            ::testing::Values(false, true));
}
