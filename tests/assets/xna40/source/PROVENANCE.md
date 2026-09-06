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
