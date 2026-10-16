# Vertex texture device-slot fixture

`vertex-texture-device-slots-xna4.fx` is a minimal XNA 4 HiDef effect whose `vs_3_0` shader
samples sampler register 2. The sampler has no pass texture or state assignment, making the public
`GraphicsDevice.VertexTextures[2]` and `VertexSamplerStates[2]` collections authoritative.

`vertex-texture-device-slots-xna4.fxb` was compiled from that source with the Microsoft DirectX SDK
June 2010 `fxc.exe /T fx_2_0`. It is 972 bytes and has SHA-256
`79b83e71ec0463bcdedc01222838f7693b67660186082ce8adec1d1fbaa962ca`.
