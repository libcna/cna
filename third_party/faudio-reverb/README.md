# FAudio mono FXReverb extraction (altered source)

Upstream: FNA-XNA/FAudio, src/FAudioFX_reverb.c, commit
6839b88e304a046ae1a609ff14a085371e02a4e0 (local qualification reference).
The original zlib notice is retained verbatim in LICENSE and Reverb.hpp.

Changes: extracted the delay, biquad shelf, comb, all-pass and reverb network,
mono processing and reset; removed FAPO/XAudio/format-conversion entry points;
adapted primitive types, allocation callbacks and math/memory operations to C++;
wrapped the implementation in a private namespace. The only instantiated route
is mono-in/mono-out; CNA supplies stereo send folding and return routing outside
this file. DSP equations and constants are retained. No native XACT equivalence
is implied. Density and RoomSize remain unused by the upstream network.
