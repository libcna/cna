# Restricted GLSL-to-SkSL translator contract (SKIA-155)

Status: normative SKIA-155 contract, fixed before the translator was implemented, matching
SKIA-144/153's own "fix the contract before implementation" precedent.

## What "deliberately restricted" means here

SKIA-155's own text is explicit: implement a translator "only for the source constructs proven
equivalent by SKIA-152/153" -- not a general GLSL ES 3.00 compiler front end. SKIA-152's inventory
found exactly one EasyGL fragment formula with proven `direct SkSL` equivalence today
(`dual_textured`/`dual_textured_colored`'s `base.rgb*=2.0; FragColor=base*tex2*tint;` combine,
already pixel-proven twice: once by SKIA-93's hand-written SkSL spike, once by SKIA-153's
`SkVertices`-driven spike). This translator's accepted grammar is scoped to exactly that shape: a
single fragment `main`, one UV varying, an arbitrary number of `uniform` scalars/vectors/matrices/
samplers, straight-line statements with no branching-around-early-exit, and texture sampling.
Everything else -- discard/alpha-test, fog, lighting, skinning, PBR helper functions, and every
other EasyGL construct SKIA-152 classified as `SkMesh`/`3D-only` -- is unconditionally rejected by
this translator, with a source location, rather than silently accepted and mistranslated. Widening
the grammar to cover more of SKIA-152's inventory (starting with `PbrEffect`'s `PbrLight()`, SKIA-152's
own flagged next acceptance bar) is explicitly deferred to a later task, not attempted here.

## Why a token-rewriter, not a general AST-based compiler

GLSL ES 3.00 and SkSL 100 are both already C-like, expression-and-statement languages with almost
identical grammars for ordinary arithmetic, control flow, and function calls -- SkSL is *itself*
"a restricted, safety-checked GLSL-like language" by Skia's own design. The differences this
translator's accepted subset actually needs to bridge are narrow and lexical, not structural:

- Vector/matrix type keywords differ (`vec2`/`vec3`/`vec4`/`mat3`/`mat4` vs `float2`/`float3`/
  `float4`/`float3x3`/`float4x4`); everything else (`float`, `int`, `bool`, operators, control flow
  keywords, function-call syntax) is spelled identically in both languages.
- A GLSL fragment shader is `void main()` writing an `out vec4` variable; an SkSL runtime-effect
  shader is `half4 main(float2 coord)` *returning* a value. The single accepted `in vec2` varying
  becomes the SkSL entry's coordinate parameter (by keeping its *own declared name*, not renaming
  every use site -- `in vec2 vUV;` simply becomes the parameter name `vUV` on `main`, so every
  existing reference to `vUV` inside the body needs no rewriting at all); the single accepted
  `out vec4` variable becomes a local declared at the top of the translated body and returned at
  the end (again keeping its own name, e.g. `FragColor`, so the body's existing `FragColor = ...`/
  `FragColor.rgb *= ...` assignments are emitted completely unchanged).
- `sampler2D` uniforms become `shader` uniforms, **renamed** to the mesh ABI's own reserved
  `cnaTexture0`-`7` child-naming convention (SKIA-154, `SkiaMeshEffectBackend`) in declaration
  order -- unlike the `in`/`out` variable above, a sampler's *original* GLSL name cannot be kept:
  SKIA-157's own public integration test caught this exact gap on its first run (the translator
  originally preserved each sampler's original name, e.g. `uTexture`, but the mesh ABI rejects any
  child name that isn't `cnaTexture0`-`7`). `texture(uTexture, uv)` therefore becomes
  `cnaTexture0.eval(uv)` (for the first declared sampler), not `uTexture.eval(uv)`. This is the one
  genuinely semantic (not just lexical) rewrite in this translator -- it requires knowing which
  identifiers were declared as sampler uniforms and their declaration order, so a call
  `texture(X, ...)` can be rewritten only when `X` is a known sampler name (a plain token-level
  regex could not tell a sampler `texture()` call from an unrelated same-named function), and to the
  *correct renamed* target, not `X` itself.

Because the accepted body statements otherwise need **no structural transformation at all** --
control flow, expressions, and every other keyword are identical token-for-token in both
languages -- a full recursive-descent parser producing a rewritten AST would reproduce the
input almost verbatim at far greater implementation cost and risk than the source actually
requires. The chosen design instead: tokenizes the whole source once; performs an unconditional
reject-scan for every disallowed keyword/identifier/preprocessor pattern (a linear token scan,
correct regardless of where in the grammar the construct appears); performs a **shallow structural
parse** of only the top level (uniform/`in`/`out` declarations and the `main` function's brace-
balanced body span) to validate the accepted shape and to learn which names are samplers; then
rewrites the body's token stream in place -- type-keyword renames and sampler `texture()`-call
rewrites only, everything else copied through unchanged -- and emits the final SkSL text.

## Accepted grammar (BNF-shaped, informal)

```
program        := (versionLine | uniformDecl | inDecl | outDecl | mainFunc)*
versionLine    := '#version' ... (to end of line; recognized and skipped, never emitted)
uniformDecl    := 'uniform' type IDENT ';'
inDecl         := 'in' 'vec2' IDENT ';'              -- at most ONE, anywhere in program
outDecl        := 'out' 'vec4' IDENT ';'             -- at most ONE, anywhere in program
mainFunc       := 'void' 'main' '(' ')' '{' body '}' -- exactly ONE, required
type           := 'float' | 'int' | 'vec2' | 'vec3' | 'vec4' | 'mat3' | 'mat4' | 'sampler2D'
body           := any balanced token sequence not containing a rejected construct (below);
                  statements/expressions are not further parsed -- see rationale above.
```

`body` is intentionally not decomposed into statement/expression productions: since every accepted
body construct is emitted unchanged apart from the two token-level rewrites above, validating
"balanced braces, no disallowed token" is sufficient and precisely as permissive as the informal
grammar promises -- it neither over-accepts (every disallowed keyword is still caught by the
reject-scan, which runs over the whole token stream including inside `body`) nor under-accepts
(no legal expression shape is rejected, since none are individually inspected).

## Unconditionally rejected, each reported with a 1-based line and column

- Any preprocessor directive other than a leading `#version` line (a lone `#version` line is
  recognized and silently dropped, not emitted, so a real EasyGL source's actual first-line
  boilerplate does not itself count as a rejection).
- `discard` (alpha-test/clip -- explicitly excluded by SKIA-155's own acceptance text).
- `gl_FragDepth` (explicit depth write).
- A second (or later) `out` declaration (MRT -- explicitly excluded).
- `dFdx`, `dFdy`, `fwidth`, `textureLod` (screen-space derivatives / explicit LOD).
- `precision` qualifier lines.
- `samplerCube`, `sampler3D`, `cnaSampleCubeEXT`, `cnaSampleVolumeEXT` (cube/volume sampling --
  explicitly excluded; SKIA-144-151's own bounded extension is a completely separate ABI with its
  own reserved preamble, not a target for this translator).
- `cnaSampleUV`, `CNA_GL_RT_SAMPLE_UV_DECL` (the render-target-source flip-V macro
  `docs/skia-easygl-effect-inventory.md` already flagged as backend-specific with no SkSL
  equivalent need -- Skia has no analogous render-target-source V-flip convention to reconcile).
- A second `in` declaration, or any `in` declaration whose type is not `vec2` (only the UV varying
  has a direct SkSL local-coordinate equivalent; per-vertex colour/normal/tangent/fog-factor
  varyings do not, per `docs/skia-vertices-2d-effect-contract.md`'s own finding that vertex colour
  combines externally through `drawVertices`' blend mode, never as a shader input).
- Any function definition other than `void main()` (helper functions, e.g. `PbrEffect`'s
  `PbrLight()`, are explicitly out of this MVP's grammar -- widening to cover them is deferred).
- A missing, duplicated, or malformed `main` (wrong return type, non-empty parameter list, or more
  than one function literally named `main`).

## What SKIA-155 built

`SkiaGlslToSkslTranslatorEXT.hpp`/`.cpp`: a tokenizer, a linear reject-scan, a shallow top-level
structural parse, and a body token-rewrite pass, exactly as described above. Proven two ways:

1. **Negative/rejection tests**: one isolated synthetic snippet per disallowed construct (discard,
   `gl_FragDepth`, a second `out`, `dFdx`, a second `in`, `precision`, `samplerCube`, `cnaSampleUV`,
   a helper function, a missing `main`), each asserted to fail with a message naming the construct
   and citing its exact source line. The real, complete `dual_textured` fragment source
   (`docs/skia-easygl-effect-inventory.md`) is also fed through unmodified and asserted to reject
   (it declares a second `in float vFogFactor;` varying and uses `discard`/`cnaSampleUV`, none of
   which this MVP grammar accepts) -- proving the translator does not silently mistranslate real,
   currently-unsupported EasyGL content.
2. **Differential/positive test**: a hand-extracted "just the accepted subset" snippet containing
   only `dual_textured`'s core formula (two `sampler2D` uniforms, one `vec4` tint uniform, one
   `in vec2` UV varying, one `out vec4` FragColor, the `base.rgb*=2.0; FragColor=base*tex2*tint;`
   combine, no alpha-test/fog/flip-V macro) is translated, compiled through
   `SkRuntimeEffect::MakeForShader`, and driven through the identical rendering path SKIA-153's own
   spike used, differentially compared against that spike's already-proven hand-written-SkSL pixel
   result for the same formula and inputs -- not just "it compiles," but "it renders the exact same
   pixels as the already-proven hand-written equivalent."
