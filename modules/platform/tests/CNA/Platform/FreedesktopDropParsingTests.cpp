// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0154, plans/plan_wayland.md WAYLAND-0017: which type a drop is read as and
// what a text/uri-list names -- shared by the X11 (XDND) and Wayland (wl_data_device) drop
// targets, since both deal in MIME types. No server is needed; these run in both backends' builds.

#include <gtest/gtest.h>

#include "../../../src/Freedesktop/DropParsing.hpp"

#include <string>
#include <vector>

namespace {

using namespace CNA::Platform::Freedesktop;

// --- which type a drop is read as ---------------------------------------------------------------------

TEST(FreedesktopDropTarget, FilesWinOverTheirNamesAsText)
{
    // A file manager offers both; the game wants the files.
    const auto choice = ChooseDropTarget({"text/plain", "UTF8_STRING", "text/uri-list"});
    ASSERT_TRUE(choice.has_value());
    EXPECT_EQ(choice->index, 2u);
    EXPECT_EQ(choice->encoding, DropEncoding::UriList);
}

TEST(FreedesktopDropTarget, Utf8TextWinsOverUnspecifiedText)
{
    const auto choice = ChooseDropTarget({"text/plain", "TEXT", "text/plain;charset=utf-8"});
    ASSERT_TRUE(choice.has_value());
    EXPECT_EQ(choice->index, 2u);
    EXPECT_EQ(choice->encoding, DropEncoding::Utf8);

    // The charset is case-insensitive, as MIME parameters are.
    EXPECT_EQ(ChooseDropTarget({"STRING", "text/plain;charset=UTF-8"})->index, 1u);
}

TEST(FreedesktopDropTarget, AmongEquallyGoodTypesTheSourcesOrderDecides)
{
    EXPECT_EQ(ChooseDropTarget({"UTF8_STRING", "text/plain;charset=utf-8"})->index, 0u);
    EXPECT_EQ(ChooseDropTarget({"text/plain;charset=utf-8", "UTF8_STRING"})->index, 0u);
}

TEST(FreedesktopDropTarget, StringIsLatin1AndTheLastResort)
{
    const auto choice = ChooseDropTarget({"image/png", "STRING"});
    ASSERT_TRUE(choice.has_value());
    EXPECT_EQ(choice->index, 1u);
    EXPECT_EQ(choice->encoding, DropEncoding::Latin1);
}

TEST(FreedesktopDropTarget, NothingTheWindowCanTakeIsNoChoice)
{
    EXPECT_FALSE(ChooseDropTarget({"image/png", "text/html", "application/x-kde-cutselection"}));
    EXPECT_FALSE(ChooseDropTarget({}));
    // An atom the backend could not name arrives as an empty string and is never chosen.
    EXPECT_FALSE(ChooseDropTarget({""}));
}

// --- what a text/uri-list names ----------------------------------------------------------------------

TEST(FreedesktopUriList, FileUrisOfEveryLocalSpellingBecomePaths)
{
    EXPECT_EQ(FileUriToPath("file:///home/player/save.dat", "arcade"), "/home/player/save.dat");
    EXPECT_EQ(FileUriToPath("file://localhost/tmp/a", "arcade"), "/tmp/a");
    EXPECT_EQ(FileUriToPath("file://arcade/tmp/a", "arcade"), "/tmp/a");
    EXPECT_EQ(FileUriToPath("FILE:///tmp/a", "arcade"), "/tmp/a");
    // The older single-slash form some file managers still send.
    EXPECT_EQ(FileUriToPath("file:/tmp/a", "arcade"), "/tmp/a");
}

TEST(FreedesktopUriList, EscapesAreDecodedToTheBytesOfTheName)
{
    EXPECT_EQ(FileUriToPath("file:///tmp/a%20b.txt", ""), "/tmp/a b.txt");
    // UTF-8 bytes, percent-encoded: Úroveň.
    EXPECT_EQ(FileUriToPath("file:///tmp/%C3%9Arove%C5%88", ""), "/tmp/\xC3\x9Arove\xC5\x88");
    EXPECT_EQ(FileUriToPath("file:///tmp/100%25", ""), "/tmp/100%");
}

TEST(FreedesktopUriList, WhatIsNotALocalFileIsNotAPath)
{
    EXPECT_FALSE(FileUriToPath("file://otherhost/tmp/a", "arcade"));
    EXPECT_FALSE(FileUriToPath("https://example.org/", "arcade"));
    EXPECT_FALSE(FileUriToPath("file:relative", "arcade"));
    EXPECT_FALSE(FileUriToPath("file://", "arcade"));
    // A malformed escape, a truncated one, and a NUL no path can hold.
    EXPECT_FALSE(FileUriToPath("file:///tmp/%zz", ""));
    EXPECT_FALSE(FileUriToPath("file:///tmp/%2", ""));
    EXPECT_FALSE(FileUriToPath("file:///tmp/a%00b", ""));
}

TEST(FreedesktopUriList, AListIsSplitOnLinesWithCommentsSkippedAndLinksKeptAsText)
{
    const auto items = ParseUriList("# dragged from a file manager\r\n"
                                    "file:///tmp/one.png\r\n"
                                    "https://example.org/page\r\n"
                                    "\r\n"
                                    "file:///tmp/two%20words.png",
                                    "arcade");
    ASSERT_EQ(items.size(), 3u);
    EXPECT_TRUE(items[0].file);
    EXPECT_EQ(items[0].value, "/tmp/one.png");
    EXPECT_FALSE(items[1].file) << "a link is text, not a file -- and not nothing";
    EXPECT_EQ(items[1].value, "https://example.org/page");
    EXPECT_TRUE(items[2].file);
    EXPECT_EQ(items[2].value, "/tmp/two words.png");
}

TEST(FreedesktopUriList, BareNewlinesAndATrailingNulAreTolerated)
{
    const std::string list("file:///a\nfile:///b\n\0", 22);
    const auto items = ParseUriList(list, "");
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[1].value, "/b");
}

TEST(FreedesktopDropText, ANulIsDroppedAndInvalidUtf8IsReadAsLatin1)
{
    EXPECT_EQ(DecodeDropText({'a', 'b', '\0'}, DropEncoding::Utf8), "ab");
    EXPECT_EQ(DecodeDropText({0xC5, 0xA1}, DropEncoding::Utf8), "\xC5\xA1") << "valid UTF-8 passes through";
    EXPECT_EQ(DecodeDropText({0xE9}, DropEncoding::Utf8), "\xC3\xA9") << "a lone Latin-1 byte becomes its UTF-8 form";
    EXPECT_EQ(DecodeDropText({0xE9}, DropEncoding::Latin1), "\xC3\xA9");
}

} // namespace
