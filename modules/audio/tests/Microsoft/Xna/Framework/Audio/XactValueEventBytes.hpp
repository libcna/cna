// SPDX-License-Identifier: MS-PL
// FACT_internal.c:2436-2467 literal payloads, shared without invoking CNA writers.
void ValueF32(Bytes& b, float value)
{
    uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); U32(b, bits);
}
[[maybe_unused]] void ValueU32At(Bytes& b, std::size_t offset, uint32_t value)
{
    for (std::size_t i=0;i<4;++i) b.at(offset+i)=static_cast<uint8_t>(value>>(8*i));
}
AuthoredEvent EquationAt(uint8_t type, uint16_t time, uint8_t flags, float first,
                         float second = 0, uint16_t repeats = 0, uint16_t frequency = 0)
{
    AuthoredEvent event; event.type=type; event.time=time;
    auto& b=event.valuePayload;
    U8(b,0); U8(b,flags); ValueF32(b,first); ValueF32(b,second);
    for (int i=0;i<5;++i) U8(b,0);
    if (type==16 || type==17) { U16(b,repeats); U16(b,frequency); }
    return event;
}
AuthoredEvent RampAt(uint8_t type, uint16_t time, float initial, float authoredSlope,
                     uint16_t duration, float slopeDelta = 0)
{
    AuthoredEvent event; event.type=type; event.time=time;
    auto& b=event.valuePayload;
    U8(b,1); ValueF32(b,initial); ValueF32(b,authoredSlope);
    ValueF32(b,slopeDelta); U16(b,duration); // repeating ramps have no trailer
    return event;
}

Bytes ValueRpcXgs()
{
    // FACT_internal.c:2047-2091,2210-2250: full77-byte header and tool42;
    // category/variable data77/87, neutral padding to two23-byte RPCs107/130,
    // names153/161 and six-byte name-index records168/174. PUBLIC|CUE is bits1|4.
    Bytes b; Text(b,"XGSF",4); U16(b,46); U16(b,42); U16(b,0); b.resize(18,0); U8(b,3);
    for (uint16_t n : {1,1,0,0,2,0,0}) U16(b,n);
    for (uint32_t offset : {77,87,0,168,0,174,153,161,107,0,0}) U32(b,offset);
    U8(b,255); U16(b,0); U16(b,0); U8(b,0); U16(b,65535); U8(b,180); U8(b,0);
    U8(b,5); ValueF32(b,0); ValueF32(b,0); ValueF32(b,1);
    b.resize(107,0);
    for (uint16_t parameter : {0,1})
    {
        U16(b,0); U8(b,2); U16(b,parameter);
        ValueF32(b,0); ValueF32(b,parameter ? -600.0f : -2000.0f); U8(b,0);
        ValueF32(b,1); ValueF32(b,parameter ? 600.0f : 0.0f); U8(b,0);
    }
    Text(b,"Default",8); Text(b,"Volume",7);
    U32(b,153); U16(b,255); U32(b,161); U16(b,255);
    return b;
}

[[maybe_unused]] Bytes ReleaseRpcXgs()
{
    // FACT_internal.c:2047-2091,2210-2250: tool42, full77-byte XGS header,
    // category/variable data at77/87, RPC codes107/130, names153/161,
    // and six-byte name-index records173/179. PUBLIC|CUE is bits1|4.
    // Neutral curves leave voice values unchanged; volume's last x=150 is release.
    Bytes b; Text(b,"XGSF",4); U16(b,46); U16(b,42); U16(b,0); b.resize(18,0); U8(b,3);
    for (uint16_t n : {1,1,0,0,2,0,0}) U16(b,n);
    for (uint32_t offset : {77,87,0,173,0,179,153,161,107,0,0}) U32(b,offset);
    U8(b,255); U16(b,0); U16(b,0); U8(b,0); U16(b,65535); U8(b,180); U8(b,0);
    U8(b,5); ValueF32(b,0); ValueF32(b,0); ValueF32(b,150);
    b.resize(107,0);
    for (uint16_t parameter : {0,1})
    {
        U16(b,0); U8(b,2); U16(b,parameter);
        ValueF32(b,0); ValueF32(b,0); U8(b,0);
        ValueF32(b,150); ValueF32(b,0); U8(b,0);
    }
    Text(b,"Default",8); Text(b,"ReleaseTime",12);
    U32(b,153); U16(b,255); U32(b,161); U16(b,255);
    return b;
}

Bytes TrackFilterRpcXgs()
{
    // Native v46 layout: four23-byte curves at107/130/153/176; names
    // at199/207 and index records214/220. Frequency/Q pairs deliberately
    // differ so sound inheritance and track overrides are distinguishable.
    Bytes b; Text(b,"XGSF",4); U16(b,46); U16(b,42); U16(b,0); b.resize(18,0); U8(b,3);
    for (uint16_t n : {1,1,0,0,4,0,0}) U16(b,n);
    for (uint32_t offset : {77,87,0,214,0,220,199,207,107,0,0}) U32(b,offset);
    U8(b,255); U16(b,0); U16(b,0); U8(b,0); U16(b,65535); U8(b,180); U8(b,0);
    U8(b,5); ValueF32(b,.25f); ValueF32(b,0); ValueF32(b,1);
    b.resize(107,0);
    const std::array<std::array<float,2>,4> endpoints{{{2000,12000},{2,4},{1000,5000},{4,8}}};
    for (std::size_t i=0;i<endpoints.size();++i)
    {
        U16(b,0); U8(b,2); U16(b,i%2==0 ? 3 : 4);
        ValueF32(b,0); ValueF32(b,endpoints[i][0]); U8(b,0);
        ValueF32(b,1); ValueF32(b,endpoints[i][1]); U8(b,0);
    }
    Text(b,"Default",8); Text(b,"Volume",7);
    U32(b,199); U16(b,255); U32(b,207); U16(b,255);
    return b;
}

