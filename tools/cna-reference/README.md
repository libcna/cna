# CNA reference-value dump tool

`cna_reference_dump` emits CNA's graphics enums/state presets, PackedVector, Viewport,
Vector2Edges, MatrixPerspectiveEdges, Vector3Edges, Vector4Edges, QuaternionEdges, MatrixEdges and GeometryEdges using the real C++ API. It needs no GraphicsDevice
or window. The matching FNA developer probe uses .NET 8 and a pinned external FNA.dll; this
is a reference test tool, not a language binding.

The target requires CNA_BUILD_EXAMPLES and is not registered as CTest. Use an existing
configured build tree, for example:

```sh
export CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv
cmake --build cmake-build-software --target cna_reference_dump --parallel 4
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  cmake-build-software/cna_reference_dump /path/to/fresh/cna-values.json
```

An omitted output path defaults to `cna-reference-values.json` in the current directory.
The tool does not protect existing outputs; choose a fresh path. It emits no FNA provenance,
because it does not load that assembly.

Generate the FNA reference with [the pinned recipe](../fna-reference/README.md). Always pass
FNA first and CNA second to `scripts/compare-fna-reference.py`. The default comparison selects
NonRenderingApis, PackedVector and Viewport. Select edge categories explicitly with repeatable `--category` arguments and `--tolerance 0`;
see the harness commands below for the complete seven-category selection.

See [the harness contract and dated results](../../docs/fna-reference-harness.md) for exact
comparison rules, bounded input domains, native regression tests, and the retained-difference
audit. Current CNA intentionally differs from FNA on measured XNA contracts, including
perspective validation and vector magnitude ranges. A raw comparison failure must be reviewed;
there is no blanket claim that every emitted category matches FNA.
