// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/MediaLibrary.hpp"
#include "CNA/Internal/Media/MediaLibraryPaths.hpp"

namespace Microsoft::Xna::Framework::Media::Test
{
    // Shared real-fixture MediaLibrary construction for Phase 4 tests -- redirects
    // MediaLibraryPaths at the checked-in tests/assets/media/{music,pictures} trees instead of
    // the real user's OS folders (plans/plan_media.md MEDIA-46's override hooks).
    class MediaLibraryTestFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            CNA::Internal::Media::MediaLibraryPaths::SetMusicRootOverride("tests/assets/media/music");
            CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride("tests/assets/media/pictures");
            library = std::make_unique<MediaLibrary>();
        }

        void TearDown() override
        {
            library.reset();
            CNA::Internal::Media::MediaLibraryPaths::SetMusicRootOverride("");
            CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride("");
        }

        std::unique_ptr<MediaLibrary> library;
    };
}
