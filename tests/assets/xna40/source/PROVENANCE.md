# Source corpus for the extensions with no binary format (authored)

`plans/plan_xnapipeline_parity.md` `XNAPP-021`. Three text sources, written for this repository:
a `.spritefont` that names every element of the font-description schema, an `.fx` with one
technique, one pass, a matrix parameter, a texture and a sampler, and an `.xml` naming a type the
intermediate serializer already knows. Nothing here was taken from a sample, a tutorial or another
project.

## The failure fixtures

`plans/plan_xnapipeline_parity.md` `XNAPP-267` added six more, all written here and all meant to
go wrong in one specific way, because a failure class needs a source that produces it:

* `broken.fx` -- an undeclared identifier, so the *compiler's* refusal reaches the build rather
  than the importer failing to read the file at all.
* `cyclic_include.fx` -- a source that `#include`s itself. The only cycle a single-asset build can
  actually contain; a cyclic content reference needs two assets and an `ExternalReference` each
  way.
* `shader_model_3.fx` -- ordinary in every respect except that it compiles `vs_3_0`/`ps_3_0`, so a
  build that refuses it refuses it for the target profile rather than for the code. The same file
  is the control at HiDef.
* `malformed.spritefont` -- an unclosed `<Size>`, so the failure happens in the reader before any
  schema question is asked.
* `unknown_type.spritefont` -- well-formed, and declaring a type no assembly defines. A different
  failure from the one above, and a build ought to say so differently.
* `missing_glyph.spritefont` -- a CJK character region a Latin font cannot cover, with
  `control_font.spritefont` beside it differing in that one element so a refusal there is about
  the glyphs and not about whether the font resolved at all.

The two font cases name the family `Liberation Mono`, which is the one string both sides resolve:
XNA asks Windows for an installed family and Wine exposes the host's fonts, while CNA looks for a
file beside the description, so the differential test copies
`tests/assets/fonts/LiberationMono-Regular.ttf` there under that name. One committed copy of the
font, presented to each side the way that side resolves one. Do **not** install it into the Wine
prefix as well: Wine then reports the family with its Regular style unavailable and every font
case fails for a reason that is not XNA's.

Nothing in an XML comment may contain two hyphens in a row. XNA's intermediate reader refuses the
whole document when one does, which is how three of these fixtures were found to be unbuildable
there before they measured anything.

## The font metric sweep

`plans/plan_xnapipeline_parity.md` `XNAPP-182` added seven more descriptions of one face:
`control_font.spritefont` and `font_size10/18/24/32.spritefont` are the same document at five
sizes, `font_spacing.spritefont` changes every policy element without touching the rasterization
(`<Spacing>`, `<UseKerning>false`, a `<DefaultCharacter>`), and `font_regions.spritefont` lists two
regions out of order with a default character neither covers.

Five sizes of one face rather than one size of five faces, because CNA resolves a font *file* and
XNA an installed *family*, and only one face is committed here. That is also the limit of what
these fixtures can settle on their own: two different rules for line spacing fit all five of them
and disagree on Arial. Where a rule needed a second face, Courier New, Arial and Georgia were
measured from the Wine prefix directly and the numbers written into the plan row; those runs are
not fixtures, because CNA cannot resolve those families here and a case CNA refuses measures
nothing.

## The effect sweep

`plans/plan_xnapipeline_parity.md` `XNAPP-191`/`XNAPP-192` added fifteen `.fx` files and one
`.fxh`. `fx_minimal.fx` is the smallest effect the corpus has -- one parameter, one technique, one
pass, shader model 2.0, nothing else -- and every other file is that one plus exactly one feature,
which is what makes a difference in the compiled container attributable to something:

* `fx_initializer.fx` gives a parameter a value; `fx_sampler.fx` adds a texture and its sampler;
  `fx_two_samplers.fx` adds a second, and `fx_sampler_register.fx` pins one to `register(s3)`,
  which is what tells a register mask apart from a count.
* `fx_two_techniques.fx` and `fx_two_passes.fx` grow the technique/pass graph, the second with
  render state on both passes.
* `fx_include.fx` includes `fx_common.fxh`, a real build dependency; `fx_macro.fx` branches on a
  symbol nothing defines.
* `fx_state_blend.fx`, `fx_state_depth.fx` and `fx_state_raster.fx` assign a few states of one
  group each; the three `*_wide.fx` files assign every state of one group and nothing else, which
  is how the container's dense render-state numbering was read rather than assumed.

`plans/plan_xna_sample_xnb_sweep.md` `XNASWEEP-201` added four more, and they are the ones that
separate what a *shader* declares from what the *effect* assigns -- a distinction every earlier
fixture happens to hide, because every sampler in them carries a texture and a filter at once:

* `fx_sampler_plain.fx` declares `sampler S : register(s0);` and assigns nothing to it. The shader
  still declares s0, so a mask taken from the bytecode answers 1 and a mask taken from the effect
  answers 0.
* `fx_sampler_state_only.fx` assigns filters and no texture, which is the other half: it reaches
  the first header dword and not the second.
* `fx_sampler_one_textured.fx` binds two samplers in one pass and gives only the second a texture.
* `fx_sampler_pass_split.fx` puts the two kinds in different passes, so a header field that
  summarized the effect rather than the pass would answer the same thing twice.

Ten further probes varying the same properties were measured against the genuine build and left
uncommitted: they were the held-out batch that the rule was checked against rather than derived
from, and they say nothing these four do not.

`broken.fx`, `cyclic_include.fx` and `shader_model_3.fx` from `XNAPP-267` belong to the same
family and are described above.

## The `.xml` documents

`plans/plan_xnapipeline_parity.md` `XNAPP-261` added five more beside `probe.xml`, written here,
each naming one shape the intermediate serializer has to resolve from a document rather than from a
C++ caller: `xml_int.xml` a primitive, `xml_vector3.xml` a framework value type, `xml_curve.xml` a
framework reference type with a nested collection of keys, `xml_dictionary.xml` a
`Dictionary[string,int]`, and `xml_rectangles.xml` a `List[Rectangle]` in the packed text form a
list of a space-separated element type takes. Two of them were wrong on the first attempt and the
genuine build said so, which is why they are what they are: a curve key is five tokens and not six,
and a list of `Rectangle` is packed text rather than `<Item>` elements.

`tests/assets/xna_custom_pipeline/library.xml` is the sixth, and lives with the custom-pipeline
project rather than here because the type it names is that project's own.

## The machine-readable half

`PROVENANCE.json` beside this file lists every fixture in this directory with where it came from.
`tools/provenance/provenance_gate.py` fails when a file here has no row, when a row names a file
that is not here, and when a row claims third-party content with no licence — which is what stops a
fixture arriving with nothing saying who wrote it.
