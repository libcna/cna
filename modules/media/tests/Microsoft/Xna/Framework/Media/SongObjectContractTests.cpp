// SPDX-License-Identifier: MS-PL
#include <functional>
#include <iterator>
#include <typeinfo>
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Media::Song;
namespace
{
constexpr const char* fixture = "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg";
class WrongObject final : public System::Object
{
public:
    const std::string& GetTypeName() const override
    { static const std::string name = "Tq055.WrongObject"; return name; }
};
template<class F> void ExpectDisposedSong(F operation)
{
    // Refresh Song::ThrowIfDisposed IL_0008..001d uses GetType().Name, exactly Song.
    try { operation(); ADD_FAILURE() << "Disposed Song operation succeeded"; }
    catch (const System::ObjectDisposedException& error)
    {
        EXPECT_EQ(typeid(error), typeid(System::ObjectDisposedException));
        EXPECT_EQ(error.getObjectNameProperty(), "Song");
    }
    catch (const std::exception& error) { ADD_FAILURE() << error.what(); }
}
}

TEST(SongObjectContractTest, EveryDisposedMemberNamesSongAndChecksBeforeEqualityShortcuts)
{
    Song song(fixture, "Disposed");
    Song peer(fixture, "Peer");
    WrongObject wrong;
    song.Dispose();
    song.Dispose();
    EXPECT_TRUE(song.getIsDisposedProperty());
    const std::function<void()> operations[] = {
        [&] { (void)song.getNameProperty(); }, [&] { (void)song.getAlbumProperty(); },
        [&] { (void)song.getArtistProperty(); }, [&] { (void)song.getGenreProperty(); },
        [&] { (void)song.getDurationProperty(); }, [&] { (void)song.getIsProtectedProperty(); },
        [&] { (void)song.getIsRatedProperty(); }, [&] { (void)song.getPlayCountProperty(); },
        [&] { (void)song.getRatingProperty(); }, [&] { (void)song.getTrackNumberProperty(); },
        [&] { (void)song.GetHashCode(); }, [&] { (void)song.ToString(); },
        // Both Equals overloads guard at IL_0000..0001, ahead of self/null/type tests.
        [&] { (void)song.Equals(&song); },
        [&] { (void)song.Equals(static_cast<const Song*>(nullptr)); },
        [&] { (void)song.Equals(static_cast<const System::Object*>(&song)); },
        [&] { (void)song.Equals(static_cast<const System::Object*>(nullptr)); },
        [&] { (void)song.Equals(static_cast<const System::Object*>(&wrong)); },
        [&] { (void)(song == peer); }, [&] { (void)(song != peer); }
    };
    for (std::size_t i = 0; i < std::size(operations); ++i)
    {
        SCOPED_TRACE(i);
        ExpectDisposedSong(operations[i]);
    }
    // Refresh op_Equality IL_0000..0007 delegates to Object.Equals (self shortcut).
    EXPECT_TRUE(song == song);
    EXPECT_FALSE(song != song);
    EXPECT_FALSE(peer.Equals(&song));
    EXPECT_FALSE(peer.Equals(static_cast<const System::Object*>(&song)));
    EXPECT_FALSE(peer == song);
    EXPECT_TRUE(peer != song);
    EXPECT_FALSE(peer.getIsDisposedProperty());
    EXPECT_EQ(peer.ToString(), "Peer");
}

TEST(SongObjectContractTest, EmptyNameTextAndHashStayIndependentOfMutableMetadata)
{
    Song song(fixture);
    Song renamed(fixture, "Different label");
    // Refresh ToString IL_0007..000c returns Name, including an empty file name label.
    EXPECT_EQ(song.getNameProperty(), "");
    EXPECT_EQ(song.ToString(), "");
    // Owner decision 2026-11-04, docs/xna-fna-behavior-decisions.md: file identity/hash
    // retained pending native MediaItem_IsEqual evidence; no Windows hash-bit claim.
    const auto expected = static_cast<int>(std::hash<std::string>{}(fixture));
    EXPECT_EQ(song.GetHashCode(), expected);
    EXPECT_EQ(renamed.GetHashCode(), expected);
    song.setDurationProperty(System::TimeSpan(1234567));
    song.setPlayCountProperty(-9);
    EXPECT_EQ(song.getDurationProperty().getTicksProperty(), 1234567);
    EXPECT_EQ(song.getPlayCountProperty(), -9);
    EXPECT_EQ(song.GetHashCode(), expected);
    EXPECT_TRUE(song.Equals(&renamed));
    EXPECT_TRUE(song.Equals(static_cast<const System::Object*>(&renamed)));
    EXPECT_FALSE(song.Equals(static_cast<const Song*>(nullptr)));
    EXPECT_FALSE(song.Equals(static_cast<const System::Object*>(nullptr)));
    EXPECT_EQ(song.ToString(), "");
}
