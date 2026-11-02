// SPDX-License-Identifier: MS-PL
using System;
using Microsoft.Xna.Framework;

namespace CNA.FnaReference
{
    internal static partial class ValueEdgeReference
    {
        private static Curve CurveBuild(Vector4[] keys, bool step, bool restoreOrder = true)
        {
            var curve = new Curve();
            for (int i = 0; i < keys.Length; ++i)
                curve.Keys.Add(new CurveKey(keys[i].X, keys[i].Y, keys[i].Z, keys[i].W,
                    step ? CurveContinuity.Step : CurveContinuity.Smooth));
            if (restoreOrder)
                for (int i = 0; i < keys.Length; ++i)
                    curve.Keys[i] = new CurveKey(keys[i].X, keys[i].Y, keys[i].Z, keys[i].W,
                        step ? CurveContinuity.Step : CurveContinuity.Smooth);
            return curve;
        }

        private static JsonWriter CurveKeys(Curve curve)
        {
            var result = new JsonWriter();
            for (int i = 0; i < curve.Keys.Count; ++i)
            {
                var key = curve.Keys[i];
                var item = new JsonWriter();
                Scalar(item, "position", key.Position); Scalar(item, "value", key.Value);
                Scalar(item, "in", key.TangentIn); Scalar(item, "out", key.TangentOut);
                item.Add("continuity", (int)key.Continuity);
                result.Add("k" + i, item);
            }
            return result;
        }

        private static JsonWriter CurveCase(Vector4[] keys, bool step)
        {
            var curve = CurveBuild(keys, step);
            var output = new JsonWriter().Add("keys", CurveKeys(curve))
                .Add("insertionKeys", CurveKeys(CurveBuild(keys, step, false))).Add("count", curve.Keys.Count)
                .Add("constant", curve.IsConstant).Add("defaultPre", (int)curve.PreLoop).Add("defaultPost", (int)curve.PostLoop);
            var evaluations = new JsonWriter();
            float first = keys.Length == 0 ? 0 : keys[0].X;
            float span = keys.Length <= 1 ? 1 : keys[keys.Length - 1].X - first;
            float[] amounts = { -2, -1, -0.5f, -0.0f, 0, 0.25f, 0.5f, 1, 1.5f, 2, 3 };
            for (int pre = 0; pre < 5; ++pre)
            for (int post = 0; post < 5; ++post)
            {
                curve.PreLoop = (CurveLoopType)pre; curve.PostLoop = (CurveLoopType)post;
                var item = new JsonWriter().Add("pre", (int)curve.PreLoop).Add("post", (int)curve.PostLoop);
                var values = new JsonWriter();
                for (int i = 0; i <= amounts.Length; ++i)
                {
                    float position = i == amounts.Length ? float.NaN : first + span * amounts[i];
                    var value = new JsonWriter(); Scalar(value, "position", position); Scalar(value, "value", curve.Evaluate(position));
                    values.Add("p" + i, value);
                }
                evaluations.Add("pre" + pre + "post" + post, item.Add("values", values));
            }
            var tangents = new JsonWriter();
            for (int input = 0; input < 3; ++input)
            for (int outgoing = 0; outgoing < 3; ++outgoing)
            {
                var both = CurveBuild(keys, step); both.ComputeTangents((CurveTangent)input, (CurveTangent)outgoing);
                var each = CurveBuild(keys, step);
                for (int i = 0; i < keys.Length; ++i) each.ComputeTangent(i, (CurveTangent)input, (CurveTangent)outgoing);
                var single = CurveBuild(keys, step); single.ComputeTangents((CurveTangent)input);
                var singleEach = CurveBuild(keys, step);
                for (int i = 0; i < keys.Length; ++i) singleEach.ComputeTangent(i, (CurveTangent)input);
                tangents.Add("in" + input + "out" + outgoing, new JsonWriter().Add("both", CurveKeys(both))
                    .Add("each", CurveKeys(each)).Add("single", CurveKeys(single)).Add("singleEach", CurveKeys(singleEach)));
            }
            try { curve.ComputeTangent(-1, CurveTangent.Flat); output.Add("negativeIndex", "none"); }
            catch (ArgumentOutOfRangeException) { output.Add("negativeIndex", "ArgumentOutOfRangeException"); }
            try { curve.ComputeTangent(keys.Length, CurveTangent.Flat, CurveTangent.Linear); output.Add("endIndex", "none"); }
            catch (ArgumentOutOfRangeException) { output.Add("endIndex", "ArgumentOutOfRangeException"); }
            return output.Add("evaluations", evaluations).Add("tangents", tangents);
        }

        public static JsonWriter Curves()
        {
            var cases = new JsonWriter();
            cases.Add("empty", CurveCase(new Vector4[] {}, false));
            cases.Add("single", CurveCase(new[] { new Vector4(2, 7, 3, 4) }, false));
            cases.Add("smooth", CurveCase(new[] { new Vector4(0, 1, 2, 3), new Vector4(1, -2, 4, 5), new Vector4(3, 5, 6, 7) }, false));
            cases.Add("stepShifted", CurveCase(new[] { new Vector4(2, 1, 0, 0), new Vector4(4, 9, 0, 0) }, true));
            cases.Add("duplicateStart", CurveCase(new[] { new Vector4(0, 0, 0, 0), new Vector4(0, 10, 0, 0), new Vector4(1, 20, 0, 0) }, false));
            cases.Add("allEqual", CurveCase(new[] { new Vector4(0, 3, 1, 2), new Vector4(0, 9, 4, 5) }, false));
            cases.Add("tiny", CurveCase(new[] { new Vector4(0, 7, 0, 0), new Vector4(1e-11f, 9, 0, 0) }, false));
            cases.Add("epsilon", CurveCase(new[] { new Vector4(0, 7, 0, 0), new Vector4(0.00000011920928955078125f, 9, 0, 0) }, false));
            uint state = Seed;
            for (int i = 0; i < 4; ++i)
            {
                var keys = new Vector4[3];
                for (int k = 0; k < keys.Length; ++k)
                    keys[k] = new Vector4(k == 2 ? 3 : k, Next(ref state), Next(ref state), Next(ref state));
                cases.Add("seeded" + i, CurveCase(keys, false));
            }
            return new JsonWriter().Add("seed", (double)Seed).Add("cases", cases);
        }
    }
}
