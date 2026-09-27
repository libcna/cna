# Using CNA's standalone graphics extensions

Configure with `-DCNA_CNAEXT=ON` and include `CNA/Graphics/CNAEXT.hpp`, or include only the
specific retained header. `AsciiPostProcessEffect::Draw(source)` quantizes a texture and draws its
glyph grid. `CRTEffect` and `DepthEffect` derive from `ShaderEffect`; bind either as the effect in
`SpriteBatch::Begin`, then draw a source texture or render target. `DebugDraw` draws lightweight
wireframe geometry between `begin(view, projection)` and `end()`.

The examples in `modules/graphics-ext/examples/` show ASCII quantization, direct CRT rendering,
and direct colour-depth rendering. For the API list and renderer-shader details, see
[cnaext-engine-layer.md](cnaext-engine-layer.md).
