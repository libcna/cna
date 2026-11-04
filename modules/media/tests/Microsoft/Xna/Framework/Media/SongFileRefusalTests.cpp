// SPDX-License-Identifier: MS-PL
#include <filesystem>
#include <memory>
#include <typeinfo>
#include <gtest/gtest.h>
#include "CNA/Internal/PathUtf8.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "System/IO/FileNotFoundException.hpp"

using Microsoft::Xna::Framework::Media::Song;
namespace
{
constexpr const char* fixture = "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg";
template<class F> void ExpectMissingFile(F operation, const std::string& path)
{
    // FNA Song.cs:93..96 requires File.Exists; CNA's documented FileName mapping
    // retains the complete request even if it contains an embedded NUL.
    try { operation(); ADD_FAILURE() << "Non-file was accepted"; }
    catch (const System::IO::FileNotFoundException& error)
    {
        EXPECT_EQ(typeid(error), typeid(System::IO::FileNotFoundException));
        EXPECT_EQ(error.getFileNameProperty(), path);
    }
    catch (const std::exception& error) { ADD_FAILURE() << error.what(); }
}
}

TEST(SongFileRefusalTest, BothFileConstructorsRefuseDirectoriesMissingAndNulPaths)
{
    const std::string paths[] = {"", "tests/assets/media/music", "tests/assets/media/no-tq055-song.ogg",
                                std::string(fixture) + std::string(1, '\0') + "suffix"};
    for (const auto& path : paths)
    {
        SCOPED_TRACE(path);
        ExpectMissingFile([&] { Song song(path, "Name"); }, path);
        ExpectMissingFile([&] { Song song(path, "Name", 1234); }, path);
    }
    // Opposite control: guarding all inputs unconditionally must fail this test.
    Song valid(fixture, "Valid");
    EXPECT_EQ(valid.getNameProperty(), "Valid");
    EXPECT_EQ(valid.getHandle(), fixture);
}

TEST(SongFileRefusalTest, FileUriRefusesPercentDecodedNulAndDirectory)
{
    const std::string absolute = CNA::Internal::PathToGenericUtf8(std::filesystem::absolute(fixture));
    std::string uriPath = absolute;
    if (uriPath.front() != '/') uriPath.insert(uriPath.begin(), '/');
    const std::string uri = "file://" + uriPath + "%00suffix";
    const auto decoded = absolute + std::string(1, '\0') + "suffix";
    ExpectMissingFile([&] { std::unique_ptr<Song> song(Song::FromUri("Nul", uri)); }, decoded);
    ExpectMissingFile([&] { std::unique_ptr<Song> song(Song::FromUri("Nul", System::Uri(uri))); }, decoded);
    const auto directory = CNA::Internal::PathToGenericUtf8(std::filesystem::absolute("tests/assets/media/music"));
    ExpectMissingFile([&] { std::unique_ptr<Song> song(Song::FromUri("Dir", directory)); }, directory);
}
