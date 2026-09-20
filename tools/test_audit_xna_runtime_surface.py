#!/usr/bin/env python3
"""Regression checks for the Microsoft XNA runtime type audit."""

import tempfile
import unittest
from collections import Counter
from pathlib import Path
from unittest.mock import patch

import audit_xna_runtime_surface as audit
import xna_runtime_members as members


class RuntimeSurfaceAuditTests(unittest.TestCase):
    def test_member_signature_parses_overloads_generics_and_nested_owner(self):
        known = {"Microsoft.Xna.Framework.Content.ContentTypeReader",
                 "Microsoft.Xna.Framework.Content.ContentTypeReader`1",
                 "Microsoft.Xna.Framework.Graphics.ModelBoneCollection.Enumerator"}
        first = members.reference_signature({"reference_name":
            "Microsoft.Xna.Framework.Content.ContentTypeReader`1.Read``1(``0,Microsoft.Xna.Framework.Vector3@)",
            "category": "method"}, known)
        self.assertEqual(first["declaring_type"], "Microsoft.Xna.Framework.Content.ContentTypeReader`1")
        self.assertEqual(first["generic_arity"], 1)
        self.assertEqual(first["parameter_types"], ["``0", "Microsoft.Xna.Framework.Vector3@"])
        nested = members.reference_signature({"reference_name":
            "Microsoft.Xna.Framework.Graphics.ModelBoneCollection.Enumerator.MoveNext",
            "category": "method"}, known)
        self.assertEqual(nested["declaring_type"], "Microsoft.Xna.Framework.Graphics.ModelBoneCollection.Enumerator")

    def test_cpp_type_normalization_ref_out_array_nullable_and_primitives(self):
        known = {"Microsoft.Xna.Framework.Vector3"}
        owner = "Microsoft.Xna.Framework.Vector3"
        self.assertEqual(members.normalize_cpp("intcs", owner, known)[0], "System.Int32")
        self.assertEqual(members.normalize_cpp("SharpRuntime::intcs", owner, known)[0], "System.Int32")
        self.assertEqual(members.normalize_cpp("std::string", owner, known)[0], "System.String")
        self.assertEqual(members.normalize_cpp("System::TimeSpan", owner, known)[0], "System.TimeSpan")
        self.assertEqual(members.normalize_cpp("std::any", owner, known)[0], "System.Object")
        self.assertEqual(members.normalize_cpp("long long", owner, known)[0], "System.Int64")
        self.assertEqual(members.normalize_cpp("const std::type_info &", owner, known)[0], "System.Type")
        self.assertEqual(members.normalize_cpp("std::vector<SharpRuntime::bytecs>", owner, known)[0], "System.Byte[]")
        self.assertTrue(members._type_match("System.Collections.ObjectModel.ReadOnlyCollection{System.Single}",
                                             "const std::array<float, 256> &", owner, known)[0])
        self.assertEqual(members.normalize_cpp("std::optional<float>", owner, known)[0],
                         "System.Nullable{System.Single}")
        self.assertTrue(members._type_match("Microsoft.Xna.Framework.Vector3@", "Vector3 &", owner, known)[0])
        self.assertTrue(members._type_match("Microsoft.Xna.Framework.Vector3@", "const Vector3 &", owner, known, "ref")[0])
        self.assertFalse(members._type_match("Microsoft.Xna.Framework.Vector3@", "const Vector3 &", owner, known, "out")[0])
        self.assertTrue(members._type_match("Microsoft.Xna.Framework.Vector3[]", "std::vector<Vector3>", owner, known)[0])
        self.assertTrue(members._type_match("System.Nullable{Microsoft.Xna.Framework.Vector3}",
                                             "const Vector3 *", owner, known)[0])
        self.assertTrue(members._type_match("System.Collections.Generic.IList{Microsoft.Xna.Framework.Vector3}",
                                             "const std::vector<Vector3> &", owner, known)[0])

    @staticmethod
    def _entry(category, signature):
        return {"category": category, "reference_name": signature,
                "assembly_xml": "Microsoft.Xna.Framework.xml"}

    @staticmethod
    def _meta(kind, value_type="System.Void", parameters=(), static=False, getter=False, setter=False, arity=0):
        return {"kind": kind, "value_type": value_type, "static": static, "visibility": "public",
                "parameter_types": list(parameters), "parameter_directions":
                ["ref" if p.endswith("@") else "value" for p in parameters],
                "generic_arity": arity, "getter": getter, "setter": setter,
                "field_semantics": "", "enum_value": ""}

    @staticmethod
    def _decl(kind, name, params=(), value_type="void", static=False, arity=0, min_count=None):
        return {"kind": kind, "name": name, "parameter_types": list(params),
                "value_type": value_type, "static": static, "generic_arity": arity,
                "min_parameter_count": len(params) if min_count is None else min_count,
                "header": "Example.hpp", "visibility": "public"}

    def _classify(self, category, signature, meta, declarations):
        entry = self._entry(category, signature)
        known = {"Microsoft.Xna.Framework.Example", "Microsoft.Xna.Framework.Vector3"}
        parsed = members.reference_signature(entry, known)
        return members.classify(entry, parsed, meta, declarations, known, None, "")

    def test_constructor_and_method_overloads_require_full_signature(self):
        ctor = self._meta("constructor", parameters=("System.Int32",))
        found = self._classify("constructor", "Microsoft.Xna.Framework.Example.#ctor(System.Int32)",
                               ctor, [self._decl("constructor", "Example", ("int",))])
        self.assertEqual(found["classification"], "EXACT_EQUIVALENT")
        wrong = self._classify("constructor", "Microsoft.Xna.Framework.Example.#ctor(System.Single)",
                               self._meta("constructor", parameters=("System.Single",)),
                               [self._decl("constructor", "Example", ("int",))])
        self.assertEqual(wrong["classification"], "MISSING")
        missing = self._classify("method", "Microsoft.Xna.Framework.Example.Draw(System.Int32,System.Single)",
                                 self._meta("method", parameters=("System.Int32", "System.Single")),
                                 [self._decl("method", "Draw", ("int",))])
        self.assertEqual(missing["classification"], "MISSING")
        defaulted = self._classify("constructor", "Microsoft.Xna.Framework.Example.#ctor(System.Int32)",
                                   ctor, [self._decl("constructor", "Example", ("int", "float"),
                                                     min_count=1)])
        self.assertEqual(defaulted["classification"], "SEMANTIC_EQUIVALENT")

    def test_properties_indexers_fields_events_operators_and_generic_methods(self):
        prop = self._classify("property", "Microsoft.Xna.Framework.Example.Count",
                              self._meta("property", "System.Int32", getter=True, setter=True),
                              [self._decl("method", "getCountProperty", value_type="int"),
                               self._decl("method", "setCountProperty", ("int",))])
        self.assertEqual(prop["classification"], "SEMANTIC_EQUIVALENT")
        indexer = self._classify("property", "Microsoft.Xna.Framework.Example.Item(System.Int32)",
                                 self._meta("property", "Microsoft.Xna.Framework.Vector3",
                                            ("System.Int32",), getter=True),
                                 [self._decl("method", "operator[]", ("int",), "Vector3")])
        self.assertEqual(indexer["classification"], "SEMANTIC_EQUIVALENT")
        mutable_indexer = self._classify("property", "Microsoft.Xna.Framework.Example.Item(System.Int32)",
                                         self._meta("property", "Microsoft.Xna.Framework.Vector3",
                                                    ("System.Int32",), getter=True, setter=True),
                                         [self._decl("method", "operator[]", ("int",), "Vector3 &")])
        self.assertEqual(mutable_indexer["classification"], "SEMANTIC_EQUIVALENT")
        field = self._classify("field", "Microsoft.Xna.Framework.Example.Total",
                               self._meta("field", "System.Int32", static=True),
                               [self._decl("field", "Total", value_type="int", static=True)])
        self.assertEqual(field["classification"], "EXACT_EQUIVALENT")
        event = self._classify("event", "Microsoft.Xna.Framework.Example.Changed",
                               self._meta("event", "System.EventHandler{System.EventArgs}"),
                               [self._decl("field", "Changed", value_type="System::EventHandler<System::EventArgs>")])
        self.assertEqual(event["classification"], "SEMANTIC_EQUIVALENT")
        wrong_delegate = self._classify("event", "Microsoft.Xna.Framework.Example.Changed",
                               self._meta("event", "System.EventHandler{System.EventArgs}"),
                               [self._decl("field", "Changed", value_type="System::EventHandler<System::Exception>")])
        self.assertEqual(wrong_delegate["classification"], "MISSING")
        readonly = self._meta("field", "System.Int32", static=True)
        readonly["field_semantics"] = "readonly"
        self.assertEqual(self._classify("field", "Microsoft.Xna.Framework.Example.Total", readonly,
                         [self._decl("field", "Total", value_type="int", static=True)])["classification"], "MISSING")
        operator = self._classify("method", "Microsoft.Xna.Framework.Example.op_Addition(System.Int32,System.Int32)",
                                  self._meta("method", "System.Int32", ("System.Int32", "System.Int32"), static=True),
                                  [self._decl("method", "operator+", ("int", "int"), "int", static=True)])
        self.assertEqual(operator["classification"], "EXACT_EQUIVALENT")
        generic = self._classify("method", "Microsoft.Xna.Framework.Example.Read``1(``0)",
                                 self._meta("method", "``0", ("``0",), arity=1),
                                 [self._decl("method", "Read", ("T",), "T", arity=1)])
        self.assertEqual(generic["classification"], "EXACT_EQUIVALENT")

    def test_host_language_substitution_requires_native_iteration_pair(self):
        signature = "Microsoft.Xna.Framework.Example.System#Collections#IEnumerable#GetEnumerator"
        meta = self._meta("method", "System.Collections.IEnumerator")
        decls = [self._decl("method", "begin", value_type="Iterator"),
                 self._decl("method", "end", value_type="Iterator")]
        self.assertEqual(self._classify("method", signature, meta, decls)["classification"],
                         "HOST_LANGUAGE_SUBSTITUTION")
        self.assertEqual(self._classify("method", signature, meta, decls[:1])["classification"], "MISSING")
        direct = "Microsoft.Xna.Framework.Example.GetEnumerator"
        self.assertEqual(self._classify("method", direct, meta, decls)["classification"],
                         "HOST_LANGUAGE_SUBSTITUTION")

    def test_array_length_native_pointer_substitution_is_signature_aware(self):
        reference = "Microsoft.Xna.Framework.Example.GetData``1(``0[])"
        meta = self._meta("method", parameters=("``0[]",), arity=1)
        native = [self._decl("method", "GetData", ("T *", "int"), arity=1)]
        self.assertEqual(self._classify("method", reference, meta, native)["classification"],
                         "HOST_LANGUAGE_SUBSTITUTION")
        wrong = [self._decl("method", "GetData", ("float *", "int"), arity=1)]
        self.assertEqual(self._classify("method", reference, meta, wrong)["classification"], "MISSING")

    def test_member_operator_and_interface_event_accessor_are_semantic_equivalents(self):
        operator = self._classify("method", "Microsoft.Xna.Framework.Example.op_Equality(Microsoft.Xna.Framework.Example,Microsoft.Xna.Framework.Example)",
                                  self._meta("method", "System.Boolean",
                                             ("Microsoft.Xna.Framework.Example", "Microsoft.Xna.Framework.Example"), static=True),
                                  [self._decl("method", "operator==", ("const Example &",), "bool")])
        self.assertEqual(operator["classification"], "SEMANTIC_EQUIVALENT")
        event = self._classify("event", "Microsoft.Xna.Framework.Example.Changed",
                               self._meta("event", "System.EventHandler{System.EventArgs}"),
                               [self._decl("method", "getChangedEvent", value_type="System::EventHandler<System::EventArgs> &")])
        self.assertEqual(event["classification"], "SEMANTIC_EQUIVALENT")

    def test_hashcode_cpp_return_substitution_is_scoped_to_hashcode(self):
        actual = self._decl("method", "GetHashCode", value_type="std::size_t")
        hashcode = self._classify("method", "Microsoft.Xna.Framework.Example.GetHashCode",
                                  self._meta("method", "System.Int32"), [actual])
        self.assertEqual(hashcode["classification"], "SEMANTIC_EQUIVALENT")
        other = self._classify("method", "Microsoft.Xna.Framework.Example.Other",
                               self._meta("method", "System.Int32"),
                               [self._decl("method", "Other", value_type="std::size_t")])
        self.assertEqual(other["classification"], "MISSING")

    def test_missing_gap_review_requires_explicit_scope(self):
        tier_a, _ = members.review_gap({
            "xna_signature": "Microsoft.Xna.Framework.Input.GamePadType.BigButtonPad",
            "declaring_type": "Microsoft.Xna.Framework.Input.GamePadType",
            "name": "BigButtonPad", "subsystem": "Input", "category": "field"})
        self.assertEqual(tier_a, "A")
        tier, reason = members.review_gap({
            "xna_signature": "Microsoft.Xna.Framework.Graphics.GraphicsAdapter.UseNullDevice",
            "declaring_type": "Microsoft.Xna.Framework.Graphics.GraphicsAdapter",
            "name": "UseNullDevice", "subsystem": "Graphics", "category": "property"})
        self.assertEqual(tier, "C")
        self.assertIn("graphics device", reason)
        with self.assertRaises(ValueError):
            members.review_gap({"xna_signature": "New.Subsystem.Unknown.Method",
                                "declaring_type": "New.Subsystem.Unknown", "name": "Method",
                                "subsystem": "Unknown", "category": "method"})

    def test_runtime_assembly_subsystems_keep_touch_avatar_and_xact_separate(self):
        self.assertEqual(members.subsystem_for("modules/input/include/TouchLocation.hpp",
                                               "Microsoft.Xna.Framework.Input.Touch.xml"), "Touch")
        self.assertEqual(members.subsystem_for("modules/gamer-services/include/AvatarDescription.hpp",
                                               "Microsoft.Xna.Framework.Avatar.xml"), "Avatar")
        self.assertEqual(members.subsystem_for("modules/audio/include/AudioEngine.hpp",
                                               "Microsoft.Xna.Framework.Xact.xml"), "XACT")

    def test_clang_public_member_model_keeps_visibility_overloads_and_nested_enum(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / "modules/math/include/Microsoft/Xna/Framework/Example.hpp"
            header.parent.mkdir(parents=True)
            header.write_text(
                "namespace Microsoft::Xna::Framework {\n"
                "class Example {\n"
                "  void Hidden();\n"
                "public:\n"
                "  enum class Mode { First = 2, Second = 4 };\n"
                "  Example(int);\n"
                "  void Draw(int);\n"
                "  void Draw(float);\n"
                "  static int Count;\n"
                "  int getValueProperty() const;\n"
                "  template<class T> T Read(T);\n"
                "protected:\n"
                "  void Protected();\n"
                "};\n"
                "}\n", encoding="utf-8")
            findings = [{"reference_name": "Microsoft.Xna.Framework.Example",
                         "cpp_name": "Microsoft::Xna::Framework::Example",
                         "header": str(header.relative_to(root))},
                        {"reference_name": "Microsoft.Xna.Framework.Example.Mode",
                         "cpp_name": "Microsoft::Xna::Framework::Example::Mode",
                         "header": str(header.relative_to(root))}]
            with patch.object(audit, "REPO", root):
                native, errors = members.cna_members(findings)
            self.assertFalse(errors)
            decls = native["Microsoft.Xna.Framework.Example"]
            self.assertNotIn("Hidden", [d["name"] for d in decls])
            self.assertIn("Protected", [d["name"] for d in decls])
            self.assertEqual([d["parameter_types"] for d in decls if d["name"] == "Draw"],
                             [["int"], ["float"]])
            self.assertEqual([d["generic_arity"] for d in decls if d["name"] == "Read"], [1])
            self.assertEqual({d["name"]: d["enum_value"] for d in
                              native["Microsoft.Xna.Framework.Example.Mode"]},
                             {"First": 2, "Second": 4})

    def test_documented_explicit_enumerator_anomaly_is_visible(self):
        entry = self._entry("method", "Microsoft.Xna.Framework.Graphics.EffectPassCollection.System#Collections#IEnumerable#GetEnumerator")
        signature = members.reference_signature(entry, {"Microsoft.Xna.Framework.Graphics.EffectPassCollection"})
        meta, note = members.resolve_metadata("M:" + entry["reference_name"], signature, {})
        self.assertEqual(meta["value_type"], "System.Collections.IEnumerator")
        self.assertIn("absent from matching DLL metadata", note)

    def test_content_reader_inherits_binary_reader_members(self):
        header = "modules/content/include/Microsoft/Xna/Framework/Content/ContentReader.hpp"
        finding = {"reference_name": "Microsoft.Xna.Framework.Content.ContentReader",
                   "cpp_name": "Microsoft::Xna::Framework::Content::ContentReader", "header": header}
        native, errors = members.cna_members([finding])
        self.assertFalse(errors)
        inherited = {item["name"] for item in native[finding["reference_name"]]
                     if item.get("inherited_from") == "System::IO::BinaryReader"}
        self.assertTrue({"ReadSingle", "ReadDouble"}.issubset(inherited))

    def test_graphics_state_inherits_protected_graphics_resource_disposal(self):
        header = "modules/graphics/include/Microsoft/Xna/Framework/Graphics/BlendState.hpp"
        finding = {"reference_name": "Microsoft.Xna.Framework.Graphics.BlendState",
                   "cpp_name": "Microsoft::Xna::Framework::Graphics::BlendState", "header": header}
        native, errors = members.cna_members([finding])
        self.assertFalse(errors)
        inherited = [item for item in native[finding["reference_name"]]
                     if item.get("inherited_from") == "GraphicsResource"]
        self.assertTrue(any(item["name"] == "Dispose" and item["parameter_types"] == ["bool"]
                            for item in inherited))
        entry = self._entry("method", "Microsoft.Xna.Framework.Graphics.BlendState.Dispose(System.Boolean)")
        known = {finding["reference_name"]}
        parsed = members.reference_signature(entry, known)
        metadata = self._meta("method", parameters=("System.Boolean",))
        classified = members.classify(entry, parsed, metadata, native[finding["reference_name"]],
                                      known, None, "")
        self.assertEqual(classified["classification"], "SEMANTIC_EQUIVALENT")

    def test_nested_type_must_be_inside_declaring_collection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / "modules/graphics/include/ModelBoneCollection.hpp"
            header.parent.mkdir(parents=True)
            header.write_text(
                "namespace Microsoft::Xna::Framework::Graphics {\n"
                "class ModelBoneCollection { public: struct Enumerator {}; };\n"
                "}\n",
                encoding="utf-8",
            )
            reference = "Microsoft.Xna.Framework.Graphics.ModelBoneCollection.Enumerator"
            documented = {reference, "Microsoft.Xna.Framework.Graphics.ModelBoneCollection"}
            with patch.object(audit, "REPO", root):
                present = audit.represented_type(reference, {"ModelBoneCollection": [header]},
                                                 documented)
                self.assertEqual(present["classification"], "exactly represented")
                self.assertTrue(present["nested_public_type"])
                header.write_text(
                    "namespace Microsoft::Xna::Framework::Graphics {\n"
                    "class ModelBoneCollection {}; struct Enumerator {};\n"
                    "}\n",
                    encoding="utf-8",
                )
                missing = audit.represented_type(reference, {"ModelBoneCollection": [header]},
                                                 documented)
                self.assertEqual(missing["classification"], "missing")
                header.write_text(
                    "namespace Microsoft::Xna::Framework::Graphics {\n"
                    "class ModelBoneCollection { private: struct Enumerator {}; };\n"
                    "}\n",
                    encoding="utf-8",
                )
                private = audit.represented_type(reference, {"ModelBoneCollection": [header]},
                                                 documented)
                self.assertEqual(private["classification"], "missing")

    def test_forward_declaration_does_not_count(self):
        self.assertIsNone(audit.named_body("class ModelBoneCollection;", "ModelBoneCollection"))

    def test_clr_arity_pairs_remain_distinct(self):
        nongeneric, substitute = audit.normalized_segments(
            "Microsoft.Xna.Framework.Content.ContentTypeReader")
        generic, exact = audit.normalized_segments(
            "Microsoft.Xna.Framework.Content.ContentTypeReader`1")
        self.assertEqual(nongeneric[-1], "ContentTypeReaderBase")
        self.assertEqual(generic[-1], "ContentTypeReader")
        self.assertTrue(substitute)
        self.assertFalse(exact)

    def test_class_template_constructor_matches_the_generic_types_ctor(self):
        # Clang names a constructor of a class template with its template argument list --
        # `ContentTypeReader<T>`, not `ContentTypeReader` -- while the documented CLR name is
        # `ContentTypeReader`1.#ctor`, whose arity marker the matcher strips from the TYPE name.
        # Microsoft's metadata for that record is an instance, protected, parameterless constructor
        # with no generic arity of its own, and CNA declares exactly that inside
        # `template <typename T> class ContentTypeReader`. Matching the two is the matcher's job;
        # this pins it so the template-argument spelling cannot silently reopen the gap.
        signature = "Microsoft.Xna.Framework.Content.ContentTypeReader`1.#ctor"
        entry = self._entry("constructor", signature)
        known = {"Microsoft.Xna.Framework.Content.ContentTypeReader`1"}
        parsed = members.reference_signature(entry, known)
        metadata = self._meta("constructor")
        declarations = [self._decl("constructor", "ContentTypeReader<T>")]
        classified = members.classify(entry, parsed, metadata, declarations, known, None, "")
        self.assertEqual(classified["classification"], "EXACT_EQUIVALENT")

        # A differently named constructor is still not a match, so the rule is the template
        # argument list and nothing wider.
        unrelated = [self._decl("constructor", "ContentTypeReaderBase<T>")]
        self.assertEqual(
            members.classify(entry, parsed, metadata, unrelated, known, None, "")["classification"],
            "MISSING")

    def test_real_content_type_reader_constructor_is_represented(self):
        header = "modules/content/include/Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
        finding = {"reference_name": "Microsoft.Xna.Framework.Content.ContentTypeReader`1",
                   "cpp_name": "Microsoft::Xna::Framework::Content::ContentTypeReader",
                   "header": header}
        native, errors = members.cna_members([finding])
        self.assertFalse(errors)
        constructors = [item for item in native[finding["reference_name"]]
                        if item["kind"] == "constructor"]
        self.assertTrue(any(not item["parameter_types"] for item in constructors),
                        "ContentTypeReader<T> must declare the documented parameterless constructor")
        entry = self._entry("constructor",
                            "Microsoft.Xna.Framework.Content.ContentTypeReader`1.#ctor")
        known = {finding["reference_name"]}
        classified = members.classify(entry, members.reference_signature(entry, known),
                                      self._meta("constructor"),
                                      native[finding["reference_name"]], known, None, "")
        self.assertEqual(classified["classification"], "EXACT_EQUIVALENT")

    def test_incomplete_reference_corpus_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(FileNotFoundError):
                audit.reference_members(Path(directory))

    def test_current_microsoft_runtime_corpus_census(self):
        if not audit.DEFAULT_XML.is_dir():
            self.skipTest("Microsoft XNA reference XML corpus is unavailable")
        types, documented = audit.reference_members(audit.DEFAULT_XML)
        self.assertEqual(len(types), 331)
        self.assertEqual(len(documented), 3627)
        self.assertEqual(Counter(item["category"] for item in documented), {
            "constructor": 253, "method": 1518, "property": 1040,
            "field": 753, "event": 63})
        self.assertEqual(len({item["category"] + ":" + item["reference_name"]
                              for item in documented}), len(documented))


if __name__ == "__main__":
    unittest.main()
