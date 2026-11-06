// SPDX-License-Identifier: MS-PL
// Shared literal XSB variation bytes for NULL control and both real-mixer tests.
AuthoredEvent VariedAt(uint16_t time, uint8_t loops, XsbTrackVariationType type,
                      uint16_t effects = 0, bool hasVariation = true, uint16_t pitch = 300)
{
    // Literal XSB type3/6 payload, FACT_internal.c:2293-2435. Three candidates
    // weight1,2,3 and has_variation bit6 in the high16. ADD/NEW bits unmodified.
    AuthoredEvent event=PlayAt(time,0,loops); event.type=effects ? 6 : 3;
    auto& b=event.valuePayload;
    U8(b,0); U8(b,loops); U16(b,0); U16(b,0);
    if (effects)
    {
        U16(b,pitch); U16(b,pitch); U8(b,90); U8(b,90);
        // IEEE754 binary32:1000,5000,2,6 (Hz bounds then Q bounds).
        for (uint32_t bits : {0x447a0000u,0x459c4000u,0x40000000u,0x40c00000u}) U32(b,bits);
        U16(b,effects);
    }
    U32(b,3u|(uint32_t(type)<<16)|(hasVariation ? 0x00400000u : 0u)); U32(b,0);
    for (uint16_t i=0;i<3;++i) { U16(b,i); U8(b,0); U8(b,0); U8(b,i+1); }
    return event;
}

Bytes VariationSound(const AuthoredTracks& tracks, bool filter = false)
{
    auto b=ScheduleXsb(tracks);
    if (filter)
    {
        // Sound202, fixed header10, each track9. filterData at track+5,
        // frequency at+7: enabled low-pass=0, rawQ6 ->baseOneOverQ=.5.
        for (std::size_t t=0;t<tracks.size();++t)
        {
            const auto offset=212+t*9;
            b[offset+5]=1; b[offset+6]=6;
            b[offset+7]=0x40; b[offset+8]=0x1f; //8000Hz
        }
    }
    return b;
}

