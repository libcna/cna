# Offline shader-package generator

`generate_shader_package.py` turns a JSON manifest plus declared shader sources into a checked-in
C++ header. Text variants are embedded verbatim. Vulkan GLSL variants are compiled to SPIR-V with
the system `libshaderc.so.1`; `--shaderc-library` or `CNA_SHADERC_LIBRARY` selects an explicit copy.

The generated header records the SHA-256 of the manifest, every source and the exact shaderc shared
library, together with shaderc's reported SPIR-V version/revision and all fixed compilation options.
The same inputs and compiler fingerprint must reproduce the header byte for byte. A different
compiler hash is an explicit toolchain-version change: regenerate the header, inspect the payload
diff and commit both the new fingerprint and payload together.

Generate the portable tint fixture from the repository root:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/shader_package/generate_shader_package.py \
  modules/graphics/examples/common/shaders/portable_tint/package.json \
  --output modules/graphics/examples/common/PortableTintShaderPackage.generated.hpp
```

Use `--check` for a write-free reproducibility gate. It exits 77 when shaderc is unavailable, so an
ordinary CNA build and CNA applications have no shaderc or DXC dependency. Compilers are offline
authoring tools only; CNA runtime targets never load or link them.
