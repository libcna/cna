// SPDX-License-Identifier: MS-PL
// FACT_internal.c:2436-2467 literal payloads, shared without invoking CNA writers.
void ValueF32(Bytes& b, float value)
{
    uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); U32(b, bits);
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
    // Literal v46 XGS: header69 + category10 + variable13 + names8+7 =107.
    // Two 23-byte curves at107/130; both bind PUBLIC|CUE variable "Volume".
    // FACT_internal.c:2020+ (ParseAudioEngine), no CNA writer is used.
    Bytes b; Text(b,"XGSF",4); U16(b,46); U16(b,0); U16(b,0); b.resize(18,0); U8(b,3);
    for (uint16_t n : {1,1,0,0,2,0,0}) U16(b,n);
    for (uint32_t offset : {69,79,0,0,0,0,92,100,107}) U32(b,offset);
    U8(b,255); U16(b,0); U16(b,0); U8(b,0); U16(b,65535); U8(b,180); U8(b,0);
    U8(b,5); ValueF32(b,0); ValueF32(b,0); ValueF32(b,1);
    Text(b,"Default",8); Text(b,"Volume",7);
    for (uint16_t parameter : {0,1})
    {
        U16(b,0); U8(b,2); U16(b,parameter);
        ValueF32(b,0); ValueF32(b,parameter ? -600.0f : -2000.0f); U8(b,0);
        ValueF32(b,1); ValueF32(b,parameter ? 600.0f : 0.0f); U8(b,0);
    }
    return b;
}

