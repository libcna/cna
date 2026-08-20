// Task 471: tiny, dependency-free JSON writer.
//
// This sandbox has mono/xbuild but no `dotnet` CLI and no nuget, so pulling in a real JSON
// library (Newtonsoft.Json, System.Text.Json) is not viable -- this hand-rolled writer covers
// exactly the shape Tasks 472+ need (a flat or one-level-nested object of numbers/strings/bools),
// nothing more.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace CNA.FnaReference
{
    /// <summary>
    /// Builds a single JSON object, in insertion order, from primitive values and nested
    /// JsonWriter objects. Not a general-purpose JSON library -- just enough for this tool's own
    /// reference-value output.
    /// </summary>
    public class JsonWriter
    {
        private readonly List<KeyValuePair<string, string>> fields =
            new List<KeyValuePair<string, string>>();

        public JsonWriter Add(string name, double value)
        {
            fields.Add(new KeyValuePair<string, string>(
                name, value.ToString("R", CultureInfo.InvariantCulture)));
            return this;
        }

        public JsonWriter Add(string name, int value)
        {
            fields.Add(new KeyValuePair<string, string>(
                name, value.ToString(CultureInfo.InvariantCulture)));
            return this;
        }

        public JsonWriter Add(string name, bool value)
        {
            fields.Add(new KeyValuePair<string, string>(name, value ? "true" : "false"));
            return this;
        }

        public JsonWriter Add(string name, string value)
        {
            // A null is emitted as JSON null rather than as an empty string: FNA leaves a missing
            // semantic null, and flattening that to "" would hide a real difference from a
            // consumer comparing against it.
            fields.Add(new KeyValuePair<string, string>(
                name, value == null ? "null" : Quote(value)));
            return this;
        }

        public JsonWriter Add(string name, JsonWriter nested)
        {
            fields.Add(new KeyValuePair<string, string>(name, nested.ToString()));
            return this;
        }

        /// <summary>
        /// Adds an already-serialized JSON fragment (an array, typically) under a name.
        /// plans/plan_fx.md FX-005 needs arrays of objects, which the value overloads above cannot
        /// express; the caller is responsible for the fragment being valid JSON.
        /// </summary>
        public JsonWriter AddRaw(string name, string json)
        {
            fields.Add(new KeyValuePair<string, string>(name, json));
            return this;
        }

        private static string Quote(string s)
        {
            var sb = new StringBuilder();
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default: sb.Append(c); break;
                }
            }
            sb.Append('"');
            return sb.ToString();
        }

        public override string ToString()
        {
            var sb = new StringBuilder();
            sb.Append("{");
            for (int i = 0; i < fields.Count; ++i)
            {
                if (i > 0) sb.Append(",");
                sb.Append(Quote(fields[i].Key));
                sb.Append(":");
                sb.Append(fields[i].Value);
            }
            sb.Append("}");
            return sb.ToString();
        }
    }
}
