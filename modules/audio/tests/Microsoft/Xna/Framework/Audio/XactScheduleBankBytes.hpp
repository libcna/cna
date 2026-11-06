// SPDX-License-Identifier: MS-PL
// Literal bank bytes shared by offline output and NULL-audio clock regressions.
struct AuthoredEvent
{
    uint8_t type = 1;
    uint16_t time = 0, random = 0, wave = 0;
    uint8_t loops = 0, flags = 0, bank = 0;
    Bytes valuePayload{}; // literal equation/ramp bytes, empty for existing wave/Stop cases
};
AuthoredEvent PlayAt(uint16_t time, uint16_t wave = 0, uint8_t loops = 0,
                     uint16_t random = 0)
{ return {1, time, random, wave, loops, 0, 0}; }
AuthoredEvent StopAt(uint16_t time, uint8_t flags)
{ return {0, time, 0, 0, 0, flags, 0}; }
using AuthoredTracks = std::vector<std::vector<AuthoredEvent>>;
using Dispatch = std::tuple<std::size_t, std::size_t, uint64_t>;

Bytes ScheduleXsb(const AuthoredTracks& tracks, uint16_t fadeOut = 0, bool rpc = false)
{
    // Literal XSB v46: fixed 74-byte header + 64 bank + 64 wave-bank name.
    // Complex sound = 9 bytes + track count + 9 bytes per track. Event blocks
    // are count:u8 then {header:u32, random:u16, separator:u8, payload}.
    const uint32_t sound = 202;
    uint32_t cursor = sound + 10 + (rpc ? 11 : 0) + static_cast<uint32_t>(tracks.size()) * 9;
    std::vector<uint32_t> offsets;
    std::vector<Bytes> blocks;
    for (const auto& track : tracks)
    {
        offsets.push_back(cursor);
        Bytes block; U8(block, static_cast<uint8_t>(track.size()));
        for (const auto& event : track)
        {
            U32(block, (uint32_t(event.time) << 5) | event.type);
            U16(block, event.random); U8(block, 255);
            if (!event.valuePayload.empty())
                block.insert(block.end(), event.valuePayload.begin(), event.valuePayload.end());
            else if (event.type == 0) U8(block, event.flags);
            else if (event.type == 1 || event.type == 3 || event.type == 4 || event.type == 6)
            {
                const bool candidates = event.type == 3 || event.type == 6;
                U8(block, 0);
                if (!candidates) { U16(block, event.wave); U8(block, event.bank); }
                U8(block, event.loops); U16(block, 0); U16(block, 0);
                if (event.type == 4 || event.type == 6)
                {
                    U16(block, 0); U16(block, 0); U8(block, 180); U8(block, 180);
                    for (int i=0;i<4;++i) U32(block,0); // literal f32 zero, filter ranges unused
                    U16(block,0); // all effect-randomization flags clear
                }
                if (candidates)
                {
                    U32(block,1); U32(block,0); // count=1, Ordered, reserved zeros
                    U16(block,event.wave); U8(block,event.bank); U8(block,0); U8(block,1);
                }
            }
            // Unknown type 23 has no invented payload; parser marks track incomplete.
        }
        cursor += static_cast<uint32_t>(block.size()); blocks.push_back(std::move(block));
    }
    const uint32_t cue = cursor, names = cue + 15, text = names + 6;
    Bytes b; Text(b, "SDBK", 4); U16(b, 46); U16(b, 0); U16(b, 0);
    b.resize(18, 0); U8(b, 0);
    U16(b, 0); U16(b, 1); U16(b, 0); U16(b, 0); U8(b, 1);
    U16(b, 1); U16(b, 0); U16(b, 0);
    U32(b, -1); U32(b, cue); U32(b, -1); U32(b, 0); U32(b, -1);
    U32(b, 0); U32(b, 138); U32(b, 0); U32(b, names); U32(b, sound);
    Text(b, "ScheduleSound", 64); Text(b, "ScheduleWave", 64);
    U8(b, rpc ? 3 : 1); U16(b, 0); U8(b, 180); U16(b, 0); U8(b, 0); U16(b, 0);
    U8(b, static_cast<uint8_t>(tracks.size()));
    // Optional literal whole-sound RPC code list: XGS offsets 107 and 130.
    if (rpc) { U16(b, 11); U8(b, 2); U32(b, 107); U32(b, 130); }
    for (std::size_t t = 0; t < tracks.size(); ++t)
    { U8(b, 180); U32(b, offsets[t]); U16(b, 0); U16(b, 0); }
    for (const auto& block : blocks) b.insert(b.end(), block.begin(), block.end());
    U8(b, 4); U32(b, sound); U32(b, 0); U8(b, 255);
    U16(b, 0); U16(b, fadeOut); U8(b, 0);
    U32(b, text); U16(b, 0); Text(b, "Sequence", 9);
    return b;
}

Bytes ScheduleXwb()
{
    // Three mono S16LE entries, 441 frames each at 44100 Hz = exactly 10 ms.
    // Marker amplitudes: 4096/32768=1/8, 8192/32768=1/4, 12288/32768=3/8.
    Bytes b; Text(b, "WBND", 4); U32(b, 1);
    for (const auto [offset, length] : std::array<std::pair<int,int>,5>{
             {{48,96},{144,72},{216,0},{216,0},{216,2646}}})
    { U32(b, offset); U32(b, length); }
    U32(b, 0); U32(b, 3); Text(b, "ScheduleWave", 64);
    U32(b, 24); U32(b, 0); U32(b, 4); U32(b, 0); U32(b, 0); U32(b, 0);
    for (uint32_t wave = 0; wave < 3; ++wave)
    {
        U32(b, 441u << 4); U32(b, (1u<<2)|(44100u<<5)|(2u<<23)|(1u<<31));
        U32(b, wave*882); U32(b, 882); U32(b, 0); U32(b, 0);
    }
    for (uint16_t wave = 0; wave < 3; ++wave)
        for (int i = 0; i < 441; ++i) U16(b, (wave+1)*4096);
    return b;
}

