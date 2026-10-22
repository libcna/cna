# SDK dependency notices

The adjacent unmodified upstream licence texts supplement dependencies whose
binary/header installation does not contain a licence file:

- `wgpu-native-MIT.txt`: [wgpu-native v29.0.1.1](https://github.com/gfx-rs/wgpu-native/blob/v29.0.1.1/LICENSE.MIT),
  using its MIT licensing option. CNA's pinned native WebGPU binary release is
  selected in `cmake/ThirdPartyWebGPU.cmake`.
- `webgpu-headers-BSD.txt`: [WebGPU headers](https://github.com/webgpu-native/webgpu-headers/blob/main/LICENSE).
  The distributed headers retain their own copyright/SPDX notices as well.
- `nlohmann-json-MIT.txt`: [JSON v3.11.3](https://github.com/nlohmann/json/blob/v3.11.3/LICENSE.MIT).
  Copied headers retain individual contributor notices, including conversion code.

Other bundled dependencies install their licence files directly from the actual
dependency source, or install the complete source files containing embedded
licence texts (stb/dr_libs). SDL's own installed licence directory is retained.
Sharp Runtime supplies its own notices in its separate install component.

These are notices for these dependencies, not a claim that the Rust dependencies
inside an externally supplied wgpu-native binary have been audited. Distributors
must also preserve the notices applicable to the exact binary they redistribute.
