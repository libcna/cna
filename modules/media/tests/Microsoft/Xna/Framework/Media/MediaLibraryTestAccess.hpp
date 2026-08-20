// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Media
{
    // plans/plan_media.md MEDIA-2: placeholder scaffold for the real MediaLibrary renderer landing in
    // Phase 3/4 (CNA::Internal::Media::MediaLibraryPaths/MediaLibraryIndex/PictureLibraryIndex).
    // MediaLibrary has no private state to expose yet -- every member is public today and either a
    // real no-op (the default constructor) or throws (matching FNA's own NotImplementedException
    // stub, see plans/plan_media.md Section 1). Once Phase 3/4 lands, this struct gains static accessors
    // for the resolved music/picture roots and raw scan counts, following the same pattern as
    // SoundEffectInstanceTestAccess.hpp, so MediaLibraryTests (MEDIA-97) and the internal renderer
    // tests (MEDIA-113/114/117) can verify scan results without widening the public XNA API surface.
    struct MediaLibraryTestAccess
    {
    };
}
