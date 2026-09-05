# The XNA 4.0 intermediate XML format, as measured

This document is the specification CNA's `IntermediateSerializer` implements
(`plans/plan_xnapipeline_parity.md`, Phase 5). Every statement in it was measured by running the
genuine XNA Game Studio 4.0 `IntermediateSerializer` over CNA-authored types under the .NET
Framework 4.0 in Wine; the driver is `tools/xna-pipeline-oracle/intermediate/IntermediateOracle.cs`
and the resulting corpus is `tests/reference/xna40/intermediate/` (the `manifest.json` there names
every case and its verdict). Nothing here comes from XNA's IL, from MonoGame or from FNA. A
statement that is not in the corpus is not in this document; extend the driver before extending the
document.

Case names below (`primitives`, `accept_bool_case`, …) are files in the corpus:
`<case>.xml` is what the serializer wrote, `<case>.input.xml` is what was handed to the
deserializer and `<case>.normalized.xml` is how the accepted object serialized back.

## 1. Envelope

```xml
<?xml version="1.0" encoding="utf-8"?>
<XnaContent xmlns:IntermediateOracle="Cna.Xna40.IntermediateOracle" xmlns:Framework="Microsoft.Xna.Framework">
  <Asset Type="IntermediateOracle:Primitives">
    …
  </Asset>
  <Resources>
    <Resource ID="#Resource1" Type="IntermediateOracle:Referenced">…</Resource>
  </Resources>
  <ExternalReferences>
    <ExternalReference ID="#External1" TargetType="Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent">..\Textures\wall.png</ExternalReference>
  </ExternalReferences>
</XnaContent>
```

* Two-space indentation, `<X />` for empty elements, one XML declaration.
* The root is `XnaContent` (case-sensitive: `accept_xnacontent_lowercase` is rejected with
  *XML is not in the XNA intermediate format. Missing XnaContent root element.*). Unknown attributes
  on any element are ignored (`accept_root_attribute_extra`, `accept_asset_attribute_extra`,
  `accept_member_attribute_extra`); a default namespace is ignored (`accept_default_namespace`).
* Children of the root, in this order and each at most once: `Asset` (required), `Resources`
  (optional), `ExternalReferences` (optional). Any other order or repetition is rejected
  (`accept_resources_before_asset`, `accept_both_externals_then_resources`, `accept_two_assets`,
  `accept_resources_twice`, `accept_trailing_junk_element`); the writer emits `Resources` before
  `ExternalReferences` (`both_sections`). The `Asset` element may not be renamed
  (`accept_asset_renamed`). Comments and processing instructions between elements are skipped
  (`accept_comment_between_members`, `accept_processing_instruction`); a DOCTYPE is rejected
  (`accept_doctype`); a byte-order mark inside the text handed to the reader is rejected
  (`accept_bom_and_declaration`, an XML-level error).
* `Resources` and `ExternalReferences` are written only when non-empty. When present they must be
  spelled with an explicit end tag: the self-closing `<Resources />` and `<ExternalReferences />`
  are rejected (`accept_empty_resources_selfclosing`, `accept_empty_externals_selfclosing`) while
  `<Resources></Resources>` is accepted (`accept_empty_resources_expanded`,
  `accept_expanded_resources_then_externals`).
* Namespace aliases: the writer declares `xmlns:<Alias>="<Namespace>"` on the root for every
  namespace a `Type` attribute names, where `<Alias>` is the last dotted segment of the namespace
  (`IntermediateOracle` for `Cna.Xna40.IntermediateOracle`, `Framework` for
  `Microsoft.Xna.Framework`, `Generic` for `System.Collections.Generic`). The reader resolves a
  prefix through ordinary XML namespace scoping, so a declaration on `Asset` works too
  (`accept_alias_on_asset_element`) and an undeclared prefix resolves to the empty namespace and
  fails with *Cannot find type ".Cat"* (`accept_polymorphic_member_undeclared_alias`).

## 2. Type names

The writer spells types as follows (`root_*`, `packed_collections/BoxedPrimitives`):

| .NET type | Spelling |
|---|---|
| `Boolean Byte SByte Int16 UInt16 Int32 UInt32 Int64 UInt64 Single Double String` | C# keywords: `bool byte sbyte short ushort int uint long ulong float double string` |
| `Char Decimal TimeSpan DateTime` and every other `System` type | full name: `System.Char`, `System.Decimal`, … (`System.Object` in `root_list_object`) |
| any other type | `<Alias>:<Name>` with the alias declared on the root |
| arrays | element spelling plus `[]`: `int[]`, `byte[]`, `string[]` |
| generics | `Generic:List[int]`, `Generic:Dictionary[string,Framework:Vector2]`, `Generic:List[IntermediateOracle:Referenced]` — square brackets, arguments comma-separated without spaces, no arity |
| `Nullable<T>` | spelled as `T` (`root_nullable_int` writes `Type="int"`) |

The reader accepts any of: the alias form; the full CLR namespace-qualified name
(`accept_vector3_full_name`, `accept_list_int_full_full`); an assembly-qualified name
(`accept_assembly_qualified_type`); the keyword spelling; and the CLR name in place of a keyword
(`accept_int_type_full`, `accept_string_type_full`, `Generic:List[System.Int32]`). It rejects the
bare short name of a non-keyword type (`accept_vector3_short`, `accept_color_hex` before it was
respelled: *Cannot find type "Vector3"*), `char` as a keyword (`accept_char_keyword_type`), the
CLR backtick generic form `List`1[System.Int32]` (`accept_list_int_clr`) and whitespace inside
generic brackets (`accept_list_int_space_in_generic`: *Cannot find type " int "*).

### The `Type` attribute

* On `Asset` and `Resource` the writer always writes it. The reader treats it as optional on
  `Asset` (`accept_no_type_attribute`) except when the declared type is `object`
  (`accept_object_root_no_type`: *XML is missing a "Type" attribute.*); on `Resource` it is required
  (`accept_shared_resource_without_type`, same message).
* On a member or `Item`, the writer writes it only when the runtime type differs from the declared
  type (`polymorphism`: `Dog` in an `Animal` member, everything in an `object` member). The reader
  requires it wherever the declared type is `object` (`accept_boxed_without_type`) and otherwise
  instantiates the declared type (`accept_abstract_without_type`); an abstract declared type without
  it fails with `InvalidOperationException: Instances of abstract classes cannot be created.`
  (`accept_abstract_shape_without_type`, `accept_abstract_member_without_type`).
* A named type must be the declared type or a subclass: *XML "Type" attribute is invalid. Expecting a
  subclass of X, but XML contains Y.* (`accept_wrong_type_attribute`,
  `accept_int_array_from_list_type`, `accept_list_from_array_type` — `int[]` and `List<int>` are not
  interchangeable). For a shared resource the message is *XML specifies invalid type for shared
  resource. Expecting a subclass of "X", but XML contains "Y".* (`accept_shared_resource_wrong_type`).

## 3. Members of a class or struct

* Serialized members are the public instance **properties first, then the public instance fields,
  each group in declaration order** (`attributes`: `PublicProperty`, `GetOnlyList`, then the
  fields). Static, const, private, internal and protected members are never written; a `readonly`
  field is not a member (`attributes`, `accept_readonly_field_present` rejects one);
  a get-only property is a member only when its type is a collection, in which case its contents
  are written and, on reading, **appended to the existing instance** (`attributes.roundtrip.xml`
  shows `11 12` becoming `11 12 11 12`); a get-only scalar property is skipped and rejected if
  present (`accept_readonly_property_present`).
* Reading is strictly positional: elements must appear in that order and every non-optional member
  must be present. A missing member fails with *XML element "X" not found* (`accept_missing_member`,
  `accept_required_omitted`, `accept_fields_before_properties`); an unexpected element where a
  member is expected reports the expected member (`accept_ignored_member_present`,
  `accept_reordered`), and an extra element after the last member fails with *'Element' is an invalid
  XmlNodeType* (`accept_unknown_member`).
* A member's value is written as element text or child elements as the sections below describe. A
  null reference is `<X Null="true" />`; the reader also accepts `Null="false"` as not null
  (`accept_null_false`), lets `Null="true"` win over element content (`accept_null_with_content`),
  and crashes with `NullReferenceException` on `Null="true"` for a value-type member
  (`accept_null_on_value_type`) — CNA refuses that case with an `InvalidContentException` instead
  (recorded divergence). A null root asset cannot be written (`root_null_string`:
  `ArgumentNullException`) and `<Asset Null="true" />` is rejected the same way (`accept_asset_null`).

### `[ContentSerializer]` (`attributes`, `accept_*` over `Attributes`)

| Property | Measured effect |
|---|---|
| `ElementName` | renames the element (`Original` → `<Renamed>`). |
| `Optional` | a null value is omitted when writing (`OptionalNull`); a non-null default is still written (`<OptionalDefault>0</OptionalDefault>`); the reader accepts the element omitted (`accept_optional_omitted`) or present as `Null="true"` (`accept_optional_null_explicit`). |
| `AllowNull = false` | reading `Null="true"` fails with *XML element "X" is not allowed to be null.* (`accept_nevernull_null`). |
| `FlattenContent` on a class member | the member's own members are written in place, without a wrapper element (`Flattened` → `<Name>`, `<Value>`). |
| `FlattenContent` on a packed collection | the packed text is written in place as bare character data (`1 2` between `</Value>` and `<RenamedItems>`); an empty flattened packed collection cannot be spelled at all — the reader fails on the following element (`accept_flattened_empty_list`). |
| `CollectionItemName` | ignored for a packed collection, which stays packed text (`<RenamedItems>3 4</RenamedItems>`; `<Number>` items rejected, `accept_collection_item_name_ignored_for_packed`). Its effect on element-per-item collections is not in the corpus. |
| `SharedResource` | see §7. |

`[ContentSerializerIgnore]` removes the member (never written, rejected if present).
`[ContentSerializerRuntimeType]` and `[ContentSerializerTypeVersion]` leave the XML unchanged
(`runtime_type`, `type_version`). A get-only collection property's `CollectionItemName`,
inheritance of members from a base class (`polymorphism`: `Dog` writes `Name` then `Tricks`, base
members first) and structs (`root_struct`: same rules as classes) are in the corpus.

## 4. Scalars

Written and read with the invariant culture; the reader trims surrounding whitespace
(`accept_int_whitespace`, `accept_bool_spaces`) and character references are ordinary XML
(`accept_entity_numeric`, `accept_int_entity_spaces`).

| Type | Written | Accepted on reading | Rejected |
|---|---|---|---|
| `bool` | `true` / `false` | `true false 1 0` (`accept_bool_one`, `accept_bool_zero`) | `True` (`accept_bool_case`) |
| integers | plain decimal, all widths (`root_long`, `root_ulong`, `packed_collections`) | leading `+` (`accept_int_plus_sign`) | hex `0x2A`, thousands `1,000`, out of range (*Value was either too large or too small for an Int32.*), `-1` for `byte`, `three` |
| `float` / `double` | .NET round-trip form: `0.333333343`, `0.33333333333333331`, `1E+300`, `16777216`, `-0`, `NaN`, `INF`, `-INF` (`float_edges`, `root_float_negzero`) | exponents `1e3`, `.5`, `Infinity`/`-Infinity` (normalized to `INF`/`-INF`), `NaN`, underflow to `0` | `1.5f`, `0x10`, `nan`, `1,5`, overflow (*too large or too small for a Single*) |
| `decimal` | `12.34`, `1.5`, `2` | plain decimal | exponent (`accept_decimal_exponent`) |
| `char` | the character itself (`<Tab>	</Tab>`, `&lt;`, `&amp;`, a space) | exactly one character | `ab`, empty (*String must be exactly one character long.*); U+0000 cannot be written (`nul_character`: `ArgumentException` from the XML writer) |
| `string` | verbatim text with XML escaping; leading/trailing spaces and newlines preserved (`string_edges`, `root_string_whitespace`); empty string `<X></X>`; null `<X Null="true" />` | CDATA (`accept_cdata`), surrounding whitespace preserved (`accept_whitespace_value`) | — |
| `TimeSpan` | ISO 8601 duration `PT1.5S`, `PT1M30S`, `P1DT2H`, `-PT1S` | the same grammar | `00:01:30` (`accept_timespan_dotnet`) |
| `DateTime` | `2010-09-16T12:30:45Z`; kind preserved: unspecified `2000-01-02T00:00:00`, local `…+01:00`, fraction `…05.1234567Z` | date only, no zone, offset (converted to local time and written with the local offset) | — |
| enums | member name; `[Flags]` as `Cheese, Olives`; `None` for zero; an undefined numeric value as its number (`accept_enum_out_of_range_number` → `99`) | name (case-sensitive), number, `Cheese,Olives` or `Cheese, Olives`, numeric flags (`3` → `Cheese, Ham`) | `happy`, `Bored`, `Cheese Olives` (*XML contains invalid value "…" for enum …*) |

## 5. Framework value types

All are written as one whitespace-separated token list and read with the same tolerance as
scalars — tabs and newlines between tokens are fine (`accept_vector3_tabs`,
`accept_vector3_newlines`), commas are not (`accept_vector3_commas`), and the count must match
exactly (*XML does not have enough entries in space-separated list.* / *XML has too many entries in
the space-separated list.*).

| Type | Form | Example |
|---|---|---|
| `Vector2/3/4`, `Quaternion` | components | `1.5 -2.5`, `1 2 3`, `0.1 0.2 0.3 0.4`, `0 0 0 1` |
| `Matrix` | 16 values, row by row | `1 0 0 0 0 1 0 0 0 0 1 0 1 2 3 1` |
| `Rectangle` | `x y width height` (integers; `1.5` rejected) | `1 2 3 4` |
| `Point` | `x y` | `1 2` |
| `Plane` | `nx ny nz d` | `0 1 0 3` |
| `Color` | one packed `uint` in hex, `AARRGGBB`, upper case | `FF6495ED` (CornflowerBlue), `280A141E` for `Color(10,20,30,40)`; the reader accepts lower case and fewer digits (`80FF` → `000080FF`), rejects `0x`, decimal digits beyond `uint` and component lists |
| `BoundingBox` | child elements | `<Min>0 0 0</Min><Max>1 1 1</Max>` |
| `BoundingSphere` | child elements | `<Center>0 0 0</Center><Radius>2</Radius>` |
| `Ray` | child elements | `<Position>0 0 0</Position><Direction>0 0 1</Direction>` |
| `Curve` | `<PreLoop>` and `<PostLoop>` (`CurveLoopType` names, both required), `<Keys>` packed as `position value tangentIn tangentOut Continuity` per key (`0 0 0 1 Smooth 1 2 1 0 Step`; `<Keys />` when empty; an unknown continuity fails with *Requested value 'Bouncy' was not found.*) | `root_curve`, `accept_curve_*` |

## 6. Collections

Arrays, `List<T>` and every other collection the corpus tried follow one of two shapes:

* **Packed**: one whitespace-separated token list as element text. Used when the element type is
  one of `bool`, the integer types, `float`, `double`, or any of the single-token framework types of
  §5 (`Vector2/3/4`, `Quaternion`, `Matrix`, `Rectangle`, `Point`, `Plane`, `Color`)
  (`collections`, `packed_collections`). Values of the whole list are concatenated
  (`Vector3Array` → `1 2 3 4 5 6`; the count must be a multiple of the component count,
  `accept_list_vector3_odd_count`). An empty packed collection is `<X />` or `<X></X>`
  (`accept_list_int_empty_text`); `<Item>` children are rejected (`accept_list_int_short`,
  `accept_int_array_items`: *The ReadContentAsString method is not supported on node type Element*).
* **Element per item**: `<Item>…</Item>` children, each item written exactly as a member of that
  type would be (`Type` attribute when the runtime type differs, `Null="true"` for null). Used for
  `string`, `char`, `decimal`, `TimeSpan`, `DateTime`, enums, `Nullable<T>`, the multi-element
  framework types (`BoundingBox`, `BoundingSphere`, `Ray`, `Curve`), classes, structs, nested
  collections and `object`. Packed text for such a collection is rejected
  (`accept_string_array_packed`: *'Text' is an invalid XmlNodeType*).
* A null collection is `<X Null="true" />`; an empty one `<X />`.
* `Dictionary<K,V>` is `<Item><Key>…</Key><Value>…</Value></Item>` for every key type including
  packed-scalar keys (`IntIntMap`); `Key` must precede `Value` (`accept_dictionary_value_before_key`);
  a repeated key fails with *An item with the same key has already been added.*
* Jagged arrays and lists of lists are element-per-item with packed inner text (`arrays_of_arrays`,
  `ListOfLists`); multidimensional arrays cannot be written (`rectangular`: `RankException: Cannot
  serialize multidimensional arrays.`).
* Nesting depth: 200 levels of a self-referential class round-trip (`accept_deep_nesting_200`,
  `deep`).

## 7. Shared resources

A member marked `[ContentSerializer(SharedResource = true)]` is written as a reference id
(`<First>#Resource1</First>`), and the object itself is written once under `<Resources>` as
`<Resource ID="#Resource1" Type="…">` in first-reference order, ids numbered from 1
(`shared_resources`). The same object referenced twice gets one resource; a shared `List<T>` is
one resource whose type is the list (`Generic:List[IntermediateOracle:Referenced]`). A null shared
reference is written as an empty element without a `Null` attribute (`<NullShared />`). Graph
cycles are supported: when the cycle reaches the root asset, the root is written a second time as
a resource (`shared_cycle`, `shared_self`), and the reader resolves cycles and self-references
(`accept_shared_cycle_read`, `accept_shared_self_cycle_read`).

Reading: the reference text is matched verbatim against the `ID` attributes — any id string works
(`#Bob`, `Bob`; `accept_shared_resource_custom_id`, `accept_shared_resource_no_hash`), whitespace
is not trimmed (`accept_shared_reference_whitespace`: *Missing shared resource " #B "*), an unknown
id fails with *Missing shared resource "#Z"*, and forward references inside `Resources` are fine.
Every `Resource` needs a unique `ID` (*XML attribute "ID" was not found.*, *Duplicate XML ID
attribute "#A".*) and a `Type`. An unreferenced resource is ignored (`accept_shared_resource_unused`).
An empty reference (`<X />`) or `Null="true"` leaves the member **unassigned** — it keeps whatever
the constructor put there (`accept_shared_null_attribute`, `accept_shared_resource_no_hash` show the
constructor's objects surviving). Inline content in a shared-resource member is rejected
(`accept_shared_inline_instead_of_reference`).

## 8. External references

An `ExternalReference<T>` member is written as `<X><Reference>#External1</Reference></X>` with the
filename under `<ExternalReferences>` as `<ExternalReference ID="#External1" TargetType="<name of
T>">path</ExternalReference>`, numbered from 1 in first-use order and deduplicated by
object identity (`external_references`). `TargetType` is spelled with a namespace alias **only when
the document already declares one**, and in full otherwise: the same `TextureContent` is
`Graphics:TextureContent` in a material's document, whose root declares that alias for its own
`Asset Type`, and
`Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent` in a document that declares
only the oracle's namespace (`tests/reference/xna40/graphics` case `material/serialize_basic`
against `accept_external_relocated_relative`). The section is written after the root element, so it
cannot declare a new alias, and the runtime does not try. A null member is `<X Null="true" />`; an `ExternalReference`
with no filename is `<X />` and reads back as such (`accept_external_reference_empty_element`).

The filename is made relative to the `referenceRelocationPath` argument's **directory** when both
are on the same drive (`external_references_samedir`: `Textures\wall.png`; `_samedrive` with the
relocation file one directory down: `..\Textures\wall.png`, `..\..\Shared\other.dds`), and written
absolute otherwise or when no relocation path is given (`external_references_norelocation`,
`external_references` with a `C:` path while the files are on `Z:`). Backslashes are the separator.

Reading resolves the text against the relocation path's directory (`accept_external_relocated_*`:
`Textures\wall.png`, `..\Textures\wall.png` and `../Textures/wall.png` all resolve, forward slashes
are normalized to backslashes; an absolute path is kept). With no relocation path a relative filename
fails with *Invalid filesystem location "Textures\wall.png"* (`accept_external_reference_relative`).
`TargetType` is required and must be exactly `T` (*XML attribute "TargetType" was not found.*, *XML
specifies wrong type for external reference "#E".*, *Cannot find type "No.Such.Type"*); an unknown
id fails with *Missing external reference "#E"*; an empty filename fails with `ArgumentNullException`;
a filename written inline in the member instead of a `<Reference>` is rejected
(`accept_external_reference_inline_filename`). Unreferenced entries are ignored.

## 9. Root values

Any type may be the root: `root_int` … `root_curve` show scalars, framework types, arrays, lists,
dictionaries, enums, structs and `object` (`root_object_int` writes `Type="int"`,
`root_list_object` writes `Type="Generic:List[System.Object]"` with typed items). `Deserialize<T>`
with `T = object` needs the `Type` attribute (`accept_object_root_no_type`).

## 10. NamedValueDictionary and OpaqueDataDictionary

`opaque_data_dictionary.xml`: a `NamedValueDictionary<T>` is written as one element per entry, in
insertion order, each carrying a `Key` attribute. The element is `<Data>` unless the dictionary
declares a collection item name, as `TextureReferenceDictionary` declares `Texture` and writes
`<Texture Key="Texture"><Reference>#External1</Reference></Texture>`
(`tests/reference/xna40/graphics` case `material/serialize_basic`). Each entry is written with a
`Type` attribute only when the value's type is not
the dictionary's default serializer type -- for `OpaqueDataDictionary` that default is `string`,
so `<Data Key="Name">wall</Data>` carries no `Type` while `<Data Key="Count" Type="int">3</Data>`
and `<Data Key="Scale" Type="Framework:Vector3">1 2 3</Data>` do. `OpaqueDataDictionary.Add(key,
null)` throws `ArgumentNullException`. `GetContentAsXml()` returns the same document compact
(no indentation) with `encoding="utf-16"` in its declaration, and the empty string for an empty
dictionary (`opaque_data_dictionary.getcontentasxml.txt`, `opaque_data_dictionary_empty.getcontentasxml.txt`).

## 11. Errors

Every rejection the corpus recorded is an `InvalidContentException` whose message begins *There was
an error while deserializing intermediate XML.* when a lower layer (XML, number parsing, type lookup)
failed, or a direct message (*XML element "X" not found*, *Missing shared resource*, …) when the
serializer itself refused, with these exceptions that surface as other exception types:
`InvalidOperationException` for `<Item>` in a packed collection and for abstract types,
`NullReferenceException` for `Null="true"` on a value type, `ArgumentNullException` for a null
root or an empty external filename, `RankException` for multidimensional arrays and
`ArgumentException` for U+0000. CNA reports all of them as `InvalidContentException` carrying the
XML line and position; the message texts above are what CNA reproduces.

## 12. Where CNA differs, and why

Every difference between CNA's serializer and the corpus is one of these, each also named in
the corpus test (`modules/content-pipeline/tests/…/XnaIntermediateSerializerTests.cpp`):

* **A parser error reads differently.** A document the XML parser itself rejects -- not the
  serializer -- is refused with XNA's own sentence, "There was an error while deserializing
  intermediate XML.", followed by *this* parser's reason. .NET says "Data at the root level is
  invalid. Line 1, position 1." and "Root element is missing." where CNA's names its own error.
  The sentence is the part callers match on, and it is the same.
* **`std::string` has no null.** A C# `string` member that is null writes `Null="true"`; a C++
  `std::string` cannot, so it never writes `Null="true"` and reads it as the empty string. A
  `std::optional<std::string>` member behaves exactly like the C# string.
* **`std::vector<T>` is `List<T>`.** C# arrays and lists are one C++ type, spelled `List` when
  written; reading accepts both `T[]` and `List[T]` where XNA insists on the declared one
  (`accept_int_array_from_list_type`, `accept_list_from_array_type`).
* **`std::map<K,V>` writes in key order**, where a .NET `Dictionary` enumerates in insertion order.
* **Self-closing empty sections are accepted.** `<Resources />` and `<ExternalReferences />` are
  read as empty sections; XNA's reader loop fails on them (§1).
* **Three XNA crashes are refusals.** `Null="true"` on a value type, a multidimensional array and
  U+0000 in a string are `InvalidContentException`s in CNA (the first with a message naming the
  member and type; XNA's is a `NullReferenceException`).
* **Number formatting is .NET Framework 4.0's.** `R` writes 7 or 9 significant digits for `float`
  and 15 or 17 for `double`, with `E+300`-style exponents (§4); sharp-runtime's own `Single::ToString`
  follows .NET Core's shortest round-trip form, which XNA never wrote.
* **tinyxml2 is the XML substrate.** It rejects a processing instruction inside an element
  (`accept_processing_instruction`, which XNA accepts) and tolerates a byte-order mark inside the
  text handed to the reader (`accept_bom_and_declaration`, which .NET rejects); it keeps
  whitespace-only element content but drops indentation between elements, reports no
  column, so `Line L, position 0.` replaces .NET's column in messages, and refuses element
  nesting beyond 500 levels while parsing (the corpus's deepest graph has 200; XNA has no ceiling
  and overflows its stack instead). The serializer keeps its own 1024-level guard behind that for
  flattened members and refuses to write a graph that cycles through a member that is not a
  shared resource.
* **Paths.** Filenames under `ExternalReferences` are made relative to the relocation path's
  directory and written with backslashes as XNA does; a relative filename is resolved against that
  directory on reading and stored in the host's path form.
