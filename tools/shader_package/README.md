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

## Optimization level

SPIR-V is compiled at shaderc's `performance` level unless the manifest says otherwise, and every
package that predates the field keeps that default byte for byte. `performance` drops `OpName` and
`OpMemberName`. Vulkan's `ComputeShader::setUniform` binds a scalar by its push-constant **member
name**, so a package whose compute program uses named scalar uniforms declares
`"optimization": "zero"`, which keeps the names; the chosen level is recorded in the header as
`kCompilerOptimization` (`plans/plan_vulkan_modern_graphics.md` VMG-0012). The engine layer's own
packages avoid named scalars instead (constant buffers, packed vectors), which is equally portable.
