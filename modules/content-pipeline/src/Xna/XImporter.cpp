// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/ModelImporters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <tuple>
#include <set>
#include <vector>

#include "CNA/Content/Pipeline/DirectXFileReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/AnimationContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/EffectContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/StockMaterials.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannelNames.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/TextureImporter.hpp"
#include "System/IO/FileNotFoundException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    namespace
    {
        namespace Canon = CNA::Content::Pipeline;
        using Graphics::AnimationChannel;
        using Graphics::AnimationContent;
        using Graphics::AnimationKeyframe;
        using Graphics::BasicMaterialContent;
        using Graphics::BoneContent;
        using Graphics::BoneWeight;
        using Graphics::BoneWeightCollection;
        using Graphics::GeometryContent;
        using Graphics::MeshContent;
        using Graphics::NodeContent;
        using Graphics::VertexChannelNames;

        /** @brief The DirectX default when a file declares no `AnimTicksPerSecond`. */
        constexpr double DefaultTicksPerSecond = 4800.0;

        /** @brief XNA's sentence for a `.x` it could not read, with the D3DX code appended. */
        [[nodiscard]] std::string Unreadable(const char* code)
        {
            return std::string("Could not read the X file. The file is corrupt or invalid. Error code: ") +
                   code + ".";
        }

        /** @brief One float out of an object's flat number list, with a bounds check. */
        [[nodiscard]] float At(const Canon::DirectXFileObject& object, const std::size_t index)
        {
            if (index >= object.numbers.size())
            {
                // A well-formed file whose object is short of the data its template declares is
                // the E_FAIL case rather than a parse error: the tokens read, the object did not.
                throw InvalidContentException(Unreadable("E_FAIL"));
            }
            return static_cast<float>(object.numbers[index]);
        }

        /** @brief The same, as a count that must fit an index. */
        [[nodiscard]] std::size_t Count(const Canon::DirectXFileObject& object, const std::size_t index)
        {
            const double value = At(object, index);
            if (value < 0.0 || value > 100000000.0)
            {
                throw InvalidContentException(Unreadable("E_FAIL"));
            }
            return static_cast<std::size_t>(value);
        }

        /**
         * @brief A `MeshNormals` entry as the pipeline holds it: the basis change, then normalized.
         *
         * Two things separate this from the position conversion below, and both are measured on
         * the genuine importer over `x_normal_rules.x`.
         *
         * **The basis change is a transform, and not the same one a position gets.** A position at
         * `(0, -1, 0)` comes back with `+0` in Z; a *normal* at `(0, -1, 0)` comes back with `-0`,
         * which is what an accumulation whose first term is `x * -0` leaves behind and what
         * neither a plain negation nor a `+ 0.0f` can produce. The other five axis directions and
         * the zero vector agree with the same matrix.
         *
         * **The matrix is `[[1, +0, -0], [+0, 1, +0], [-0, +0, -1]]`, and the zero vector answers
         * exactly `(+0, +0, +0)`.** `M31` is a negative zero as well as `M13`, and the two
         * together are the only assignment of the six off-diagonal zero signs that reproduces all
         * sixty-four sign combinations of `{+0, -0, +1, -1}^3` the genuine importer was measured
         * over -- one fixture per combination, so no folding can hide one. Neither sign of zero
         * is reachable from a source whose `x` is `+0`, which is why `x_normal_rules.x` could not
         * see `M31`: it takes an `x` of `-0` with a `y` below zero and a `z` above it, which is
         * `Car.x`'s `(-0.000000, -0.697342, 0.716738)` and eighty-six others like it. The
         * zero-length branch is not the raw accumulation either -- `(-0, -0, -0)` accumulates to
         * `(+0, -0, +0)` and the genuine importer answers `(+0, +0, +0)`
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-181`).
         *
         * **The normalization is wide.** The sum of squares and the square root are computed in
         * `double` and rounded to a `float` once, and the three components are *divided* by it.
         * `(0.855686, 0, 0.517496)` -- SAMPLE-014's own, 4.2e-7 longer than unit -- answers
         * `0x3F5B0E38`; a float sum of squares answers `0x3F5B0E3A` and multiplying by a float
         * reciprocal `0x3F5B0E39`. Over `asteroid1.x`'s 396 declared normals the wide division
         * reproduces all 396 and the float form 230.
         *
         * A position is neither transformed this way nor normalized: `0.855686` stays
         * `0x3F5B0E3D` (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-170`).
         */
        [[nodiscard]] Vector3 ConvertNormal(const Vector3 value)
        {
            static constexpr float kNegativeZero = -0.0f;
            const float x = ((value.X * 1.0f) + (value.Y * 0.0f)) + (value.Z * kNegativeZero);
            const float y = ((value.X * 0.0f) + (value.Y * 1.0f)) + (value.Z * 0.0f);
            const float z = ((value.X * kNegativeZero) + (value.Y * 0.0f)) + (value.Z * -1.0f);
            const double wide = (static_cast<double>(x) * static_cast<double>(x)) +
                                (static_cast<double>(y) * static_cast<double>(y)) +
                                (static_cast<double>(z) * static_cast<double>(z));
            const float length = static_cast<float>(std::sqrt(wide));
            if (!(length > 0.0f))
            {
                return Vector3(0.0f, 0.0f, 0.0f);
            }
            return Vector3(x / length, y / length, z / length);
        }

        /** @brief The left-handed source vector as the right-handed pipeline holds it. */
        [[nodiscard]] Vector3 Convert(const Vector3 value)
        {
            // `+ 0.0f` is not decoration. XNA's importer applies the basis change as a transform,
            // so every component is a sum ending in a zero term, and IEEE addition turns a
            // negative zero into a positive one: a `.x` vertex at `z = 0` comes back `0`, where
            // negating it on its own answers `-0`. Same bits everywhere else, and the difference
            // used to be invisible because `MeshHelper.TransformScene` ran over every model and
            // washed the sign out (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-167`).
            return Vector3(value.X + 0.0f, value.Y + 0.0f, -value.Z + 0.0f);
        }

        /**
         * @brief The matrix the `.x` importer multiplies a transform by on the left.
         *
         * `diag(1, 1, -1, 1)` in value, and its zeros carry a sign: `M24` and `M31` are negative
         * zeros. That is not decoration -- a zero times a negative number is a negative zero, so
         * which of a dot product's four terms is negative decides the sign of a zero in the
         * answer. Those two are what 144 measured matrices pin down; `M12`, `M13`, `M14` and the
         * fourth row are not constrained by any of them, and are written positive here except
         * `M43`, which cannot be, because no assignment with all three of the fourth row's zeros
         * positive fits (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-189`).
         */
        [[nodiscard]] Matrix BasisChangeLeft()
        {
            static constexpr float kNegativeZero = -0.0f;
            Matrix basis = Matrix::getIdentityProperty();
            basis.M33 = -1.0f;
            basis.M24 = kNegativeZero;
            basis.M31 = kNegativeZero;
            basis.M43 = kNegativeZero;
            return basis;
        }

        /**
         * @brief The matrix it multiplies by on the right, which is not the same one.
         *
         * That the two sides differ is the whole of what a third measured batch added: with one
         * shared basis, two of 144 matrices come out with the wrong sign of zero and no assignment
         * of its twelve signs fixes them. Here `M32` is the negative zero and everything else is
         * positive -- the first and third columns because the measurements say so, the fourth
         * because nothing constrains it (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-189`).
         */
        [[nodiscard]] Matrix BasisChangeRight()
        {
            static constexpr float kNegativeZero = -0.0f;
            Matrix basis = Matrix::getIdentityProperty();
            basis.M33 = -1.0f;
            basis.M32 = kNegativeZero;
            return basis;
        }

        /**
         * @brief The left-handed matrix as the right-handed pipeline holds it.
         *
         * The basis change `B_L M B_R`, as two matrix multiplications in that order, and *not* the
         * five negations it is equal to in value: the third row and the third column are negated
         * and `M33` is left alone because it is negated twice, but negating an entry on its own
         * turns a zero into a negative zero where a dot product's sum of four terms does not.
         * Measured over 144 random sign patterns in three independent batches: the negations
         * reproduce 3 of 144, one shared basis 142, and this all 144 to the bit
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-189`).
         */
        [[nodiscard]] Matrix Convert(const Matrix& m)
        {
            return Matrix::Multiply(Matrix::Multiply(BasisChangeLeft(), m), BasisChangeRight());
        }

        [[nodiscard]] const Canon::DirectXFileObject* Find(const Canon::DirectXFileObject& parent,
                                                           const std::string& type)
        {
            for (const Canon::DirectXFileObject& child : parent.children)
            {
                if (child.TypeIs(type))
                {
                    return &child;
                }
            }
            return nullptr;
        }

        /** @brief A matrix out of a `FrameTransformMatrix`'s sixteen numbers, row by row. */
        [[nodiscard]] Matrix ReadMatrix(const Canon::DirectXFileObject& object, const std::size_t at)
        {
            Matrix m;
            m.M11 = At(object, at + 0); m.M12 = At(object, at + 1);
            m.M13 = At(object, at + 2); m.M14 = At(object, at + 3);
            m.M21 = At(object, at + 4); m.M22 = At(object, at + 5);
            m.M23 = At(object, at + 6); m.M24 = At(object, at + 7);
            m.M31 = At(object, at + 8); m.M32 = At(object, at + 9);
            m.M33 = At(object, at + 10); m.M34 = At(object, at + 11);
            m.M41 = At(object, at + 12); m.M42 = At(object, at + 13);
            m.M43 = At(object, at + 14); m.M44 = At(object, at + 15);
            return m;
        }

        /** @brief Everything one `.x` file's importer needs to carry between its stages. */
        struct Importing
        {
            /** @brief Frames by name, for a SkinWeights or an Animation to reach. */
            std::map<std::string, std::shared_ptr<NodeContent>> framesByName;
            /** @brief Frames named by a SkinWeights, which is what makes a frame a bone. */
            std::set<std::string> boneNames;
            /** @brief Whether any mesh declared a skeleton. */
            bool hasSkeleton = false;
            /** @brief The file's own tick rate. */
            double ticksPerSecond = DefaultTicksPerSecond;
            /** @brief Where the source lives, for an external texture reference. */
            std::filesystem::path directory;
            /** @brief Every named `Material` in the file, for a `{Name}` reference to reach. */
            std::map<std::string, const Canon::DirectXFileObject*> materialsByName;
        };

        /** @brief One vertex's worth of bone weights, gathered before the channel is built. */
        using WeightsPerVertex = std::vector<std::vector<BoneWeight>>;

        /**
         * @brief One `.x` `Material`, as the material content the genuine importer answers for it.
         *
         * A `Material` carrying an `EffectInstance` is an `EffectMaterialContent` and nothing of
         * the material's own colours survives: the opaque data is the effect reference followed by
         * the instance's parameters in the order the file writes them, and a string parameter is a
         * *texture* under that parameter's name. Measured on the genuine importer with a fixture
         * carrying one of each parameter template -- `EffectParamString`, `EffectParamFloats` and
         * `EffectParamDWord` -- which answers `Effect`, `Tint=(0.25,0.5,0.75)`, `Passes=2` and
         * `materialTexture DiffuseTexture` (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-174`).
         */
        [[nodiscard]] std::shared_ptr<Graphics::MaterialContent> ReadMaterialContent(
            const Canon::DirectXFileObject& object, const Importing& importing);

        void ReadMaterial(const Canon::DirectXFileObject& object, BasicMaterialContent& material,
                          const Importing& importing)
        {
            // Face colour (RGBA), power, specular (RGB), emissive (RGB): the template's own order.
            // The order matters: OpaqueData keeps what it was given in the order it was given,
            // and the genuine importer's order is diffuse, specular, emissive, alpha, power.
            material.setDiffuseColorProperty(Vector3(At(object, 0), At(object, 1), At(object, 2)));
            material.setSpecularColorProperty(Vector3(At(object, 5), At(object, 6), At(object, 7)));
            material.setEmissiveColorProperty(Vector3(At(object, 8), At(object, 9), At(object, 10)));
            material.setAlphaProperty(At(object, 3));
            // A power of zero is not a value: the genuine importer writes no `SpecularPower` at
            // all for it, and the material then carries `BasicEffect`'s own default of 16 into the
            // `.xnb` (measured, `x/zero_power.x`; and 3 of the sample corpus's `.x` materials are
            // exactly that -- ReachGraphicsDemo's `grid.x` and ColorReplacement's `Car.x` both
            // answer 16 in XNA's own build where CNA wrote 0,
            // plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-106`).
            if (At(object, 4) != 0.0f)
            {
                material.setSpecularPowerProperty(At(object, 4));
            }
            const Canon::DirectXFileObject* texture = Find(object, "TextureFilename");
            if (texture != nullptr && !texture->strings.empty())
            {
                // The reference is the file beside the source, which is where a `.x` names it --
                // spelled the way the tool that wrote the file spells a path. Every `.x` in the
                // public sample corpus was written on Windows, so `..\textures\p1_dual.tga` is
                // an ordinary relative path and not a filename with backslashes in it
                // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-114`).
                std::string named = texture->strings.front();
                std::replace(named.begin(), named.end(), '\\', '/');
                material.setTextureProperty(std::make_shared<ExternalReference<Graphics::TextureContent>>(
                    (importing.directory / named).lexically_normal().string()));
            }
        }

        std::shared_ptr<Graphics::MaterialContent> ReadMaterialContent(const Canon::DirectXFileObject& object,
                                                             const Importing& importing)
        {
            const Canon::DirectXFileObject* instance = Find(object, "EffectInstance");
            if (instance == nullptr || instance->strings.empty())
            {
                auto basic = std::make_shared<BasicMaterialContent>();
                ReadMaterial(object, *basic, importing);
                return basic;
            }
            auto effect = std::make_shared<Graphics::EffectMaterialContent>();
            std::string named = instance->strings.front();
            std::replace(named.begin(), named.end(), '\\', '/');
            effect->setEffectProperty(std::make_shared<ExternalReference<Graphics::EffectContent>>(
                (importing.directory / named).lexically_normal().string()));
            for (const Canon::DirectXFileObject& parameter : instance->children)
            {
                if (parameter.strings.empty())
                {
                    continue;
                }
                const std::string& key = parameter.strings.front();
                if (parameter.TypeIs("EffectParamString"))
                {
                    if (parameter.strings.size() < 2u)
                    {
                        continue;
                    }
                    std::string value = parameter.strings[1];
                    std::replace(value.begin(), value.end(), '\\', '/');
                    effect->getTexturesProperty().Set(
                        key, std::make_shared<ExternalReference<Graphics::TextureContent>>(
                                 (importing.directory / value).lexically_normal().string()));
                }
                else if (parameter.TypeIs("EffectParamDWord"))
                {
                    if (parameter.numbers.empty())
                    {
                        continue;
                    }
                    effect->getOpaqueDataProperty().SetValue<SharpRuntime::intcs>(
                        key, static_cast<SharpRuntime::intcs>(parameter.numbers.front()));
                }
                else if (parameter.TypeIs("EffectParamFloats"))
                {
                    // The first number is the count, and the count *is* the type: one float is a
                    // `Single`, two a `Vector2`, three a `Vector3`, four a `Vector4`, sixteen a
                    // `Matrix`, and every other count a `Single[]`. Measured on the genuine
                    // importer over counts 1, 2, 3, 4, 16, 5, 6, 9 and 12
                    // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-174`).
                    if (parameter.numbers.empty()) { continue; }
                    const auto count = static_cast<std::size_t>(parameter.numbers.front());
                    if (parameter.numbers.size() < count + 1u) { continue; }
                    const auto at = [&parameter](const std::size_t index)
                    { return static_cast<float>(parameter.numbers[index + 1u]); };
                    switch (count)
                    {
                        case 1u:
                            effect->getOpaqueDataProperty().SetValue<float>(key, at(0));
                            break;
                        case 2u:
                            effect->getOpaqueDataProperty().SetValue<Vector2>(
                                key, Vector2(at(0), at(1)));
                            break;
                        case 3u:
                            effect->getOpaqueDataProperty().SetValue<Vector3>(
                                key, Vector3(at(0), at(1), at(2)));
                            break;
                        case 4u:
                            effect->getOpaqueDataProperty().SetValue<Vector4>(
                                key, Vector4(at(0), at(1), at(2), at(3)));
                            break;
                        case 16u:
                        {
                            Matrix matrix;
                            matrix.M11 = at(0);  matrix.M12 = at(1);  matrix.M13 = at(2);  matrix.M14 = at(3);
                            matrix.M21 = at(4);  matrix.M22 = at(5);  matrix.M23 = at(6);  matrix.M24 = at(7);
                            matrix.M31 = at(8);  matrix.M32 = at(9);  matrix.M33 = at(10); matrix.M34 = at(11);
                            matrix.M41 = at(12); matrix.M42 = at(13); matrix.M43 = at(14); matrix.M44 = at(15);
                            effect->getOpaqueDataProperty().SetValue<Matrix>(key, matrix);
                            break;
                        }
                        default:
                            // The genuine importer answers a `Single[]` here and the genuine
                            // writer writes it through `ArrayReader`1[[System.Single]]`. CNA has
                            // no array-valued content object, so the file is refused rather than
                            // built with the parameter missing: no `.x` in the corpus carries one,
                            // and a model whose effect loses a parameter is worse than a build
                            // that says why it stopped.
                            throw InvalidContentException(
                                "the EffectInstance parameter '" +
                                key + "' has " + std::to_string(count) +
                                " floats. XNA reads that as a Single[], which this pipeline has "
                                "no content type for; 1, 2, 3, 4 and 16 floats are read as "
                                "Single, Vector2, Vector3, Vector4 and Matrix.");
                    }
                }
            }
            return effect;
        }

        /** @brief Turns one `Mesh` object into a MeshContent, with every channel XNA fills. */
        [[nodiscard]] std::shared_ptr<MeshContent> ReadMesh(const Canon::DirectXFileObject& object,
                                                            Importing& importing)
        {
            auto mesh = std::make_shared<MeshContent>();
            mesh->setNameProperty(object.name);

            std::size_t at = 0u;
            const std::size_t vertexCount = Count(object, at++);
            std::vector<Vector3> positions;
            positions.reserve(vertexCount);
            for (std::size_t i = 0; i < vertexCount; ++i)
            {
                positions.push_back(Convert(Vector3(At(object, at), At(object, at + 1), At(object, at + 2))));
                at += 3u;
            }
            // A mesh's positions are the *distinct* ones, and the order is the order the
            // material batches ask for them: each batch adds the positions its own faces name, in
            // ascending file order, and a position an earlier batch already added keeps the index
            // it was given. The genuine importer answers 3,815 positions for Spacewar's `p1_bfg.x`,
            // which declares 5,299 vertices and holds exactly 3,815 different ones, and 12,802 of
            // `p2_dual.x`'s 23,305 -- in an order that is a *permutation* of the file's, and this
            // is the permutation. Nothing about the vertex buffer depends on it; the mesh's
            // bounding sphere does, because it is computed over this list and the growth pass is
            // order-dependent (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-173`).
            constexpr std::size_t kUnplaced = static_cast<std::size_t>(-1);
            std::vector<std::size_t> positionOf(vertexCount, kUnplaced);
            std::map<std::array<std::uint32_t, 3>, std::size_t> mergedPositions;
            const auto placePosition = [&](const std::size_t vertex)
            {
                if (vertex >= positionOf.size() || positionOf[vertex] != kUnplaced)
                {
                    return;
                }
                std::array<std::uint32_t, 3> key{};
                const float components[3] = {positions[vertex].X, positions[vertex].Y,
                                             positions[vertex].Z};
                for (std::size_t axis = 0; axis < 3u; ++axis)
                {
                    std::memcpy(&key[axis], &components[axis], sizeof(std::uint32_t));
                }
                const auto found = mergedPositions.find(key);
                if (found != mergedPositions.end())
                {
                    positionOf[vertex] = found->second;
                    return;
                }
                const std::size_t assigned =
                    static_cast<std::size_t>(mesh->getPositionsProperty().getCountProperty());
                mergedPositions.emplace(key, assigned);
                mesh->getPositionsProperty().Add(positions[vertex]);
                positionOf[vertex] = assigned;
            };

            const std::size_t faceCount = Count(object, at++);
            std::vector<std::vector<std::size_t>> faces;
            faces.reserve(faceCount);
            for (std::size_t i = 0; i < faceCount; ++i)
            {
                const std::size_t corners = Count(object, at++);
                if (corners < 3u)
                {
                    throw InvalidContentException(Unreadable("E_FAIL"));
                }
                std::vector<std::size_t> face;
                face.reserve(corners);
                for (std::size_t corner = 0; corner < corners; ++corner)
                {
                    const std::size_t index = Count(object, at++);
                    if (index >= vertexCount)
                    {
                        // A face naming a vertex the mesh does not have: the genuine reader
                        // answers E_FAIL for exactly this (measured, x/index_out_of_range.x).
                        throw InvalidContentException(Unreadable("E_FAIL"));
                    }
                    face.push_back(index);
                }
                faces.push_back(std::move(face));
            }

            /**
             * @brief One step of the normalization the genuine importer applies.
             *
             * The sum of squares is accumulated wider than `float` and the square root is narrowed
             * when it is stored; each component is then *divided* by that length rather than
             * multiplied by its reciprocal. Over 120 triangles whose cross product is exact by
             * construction -- an origin and two small integer vectors, so the only rounding left
             * is here -- this form answers all 120 and a `float` sum answers 101, a `double`
             * square root 53 and a reciprocal multiply 48
             * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-231`).
             */
            const auto normalizeStep = [](const Vector3 value)
            {
                const double square = static_cast<double>(value.X) * value.X +
                                      static_cast<double>(value.Y) * value.Y +
                                      static_cast<double>(value.Z) * value.Z;
                if (square == 0.0) { return Vector3(0.0f, 0.0f, 0.0f); }
                const float length = static_cast<float>(std::sqrt(square));
                if (length == 0.0f) { return Vector3(0.0f, 0.0f, 0.0f); }
                return Vector3(value.X / length, value.Y / length, value.Z / length);
            };

            /** @brief The same step until it stops moving, at most `times` of them. */
            const auto normalizeTimes = [&normalizeStep](Vector3 value, const int times)
            {
                for (int pass = 0; pass < times; ++pass)
                {
                    const Vector3 stepped = normalizeStep(value);
                    if (stepped.X == value.X && stepped.Y == value.Y && stepped.Z == value.Z)
                    {
                        return stepped;
                    }
                    value = stepped;
                }
                return value;
            };

            /**
             * @brief The normal a mesh with no `MeshNormals` block answers, per vertex.
             *
             * **The face normal.** A polygon is a fan of triangles from its first corner, and a
             * triangle's normal is `cross(p0 - p1, p2 - p1)` -- the edges are taken from the
             * *second* corner, not the first, which is what separates this from every other
             * pairing: over 72 isolated triangles with arbitrary coordinates it answers all 72
             * and the `p1 - p0` / `p2 - p0` pairing answers 20. Each component is one wide
             * expression narrowed when it is stored, which is the x87 shape XNA 4.0 is compiled
             * to: over 120 triangles whose edges are exact and whose products are not, that
             * answers all 120 against 42 for a `float` product and 50 for a `double` one.
             * The face normal is then normalized once.
             *
             * **The accumulation.** Every triangle adds its unit normal to each of its three
             * vertices, in `float`, in the file's own face order, and the buckets are *positions*
             * rather than vertices: a mesh that names the same position twice accumulates into it
             * once. The vertex normal is that sum normalized, twice.
             *
             * Measured against the genuine importer over 432 purpose-built triangles, the 39
             * committed `.x` fixtures and SAMPLE-141's `xwing.x`, whose 1,293 positions it answers
             * bit for bit (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-231`).
             *
             * **What is left.** Thirteen of those 1,291 probe normals and one of the 76 fixture
             * ones differ in the *sign of a zero* and in nothing else -- the genuine importer
             * answers `-0` in Z where a sum that starts at `+0` cannot. No corpus reference
             * carries one.
             *
             * @param positions The mesh's positions, as imported.
             * @param faces The mesh's faces, as vertex indices.
             * @return One normal per vertex.
             */
            const auto generateNormals =
                [&normalizeStep, &normalizeTimes](
                    const std::vector<Vector3>& positions,
                    const std::vector<std::vector<std::size_t>>& faces)
            {
                // The bucket a vertex accumulates into is its *position*, by the same exact-bits
                // identity the position merge uses.
                const auto keyOf = [&positions](const std::size_t vertex)
                {
                    std::array<std::uint32_t, 3> key{};
                    const float components[3] = {positions[vertex].X, positions[vertex].Y,
                                                 positions[vertex].Z};
                    for (std::size_t axis = 0; axis < 3u; ++axis)
                    {
                        std::memcpy(&key[axis], &components[axis], sizeof(std::uint32_t));
                    }
                    return key;
                };
                std::map<std::array<std::uint32_t, 3>, Vector3> accumulated;
                for (const std::vector<std::size_t>& face : faces)
                {
                    for (std::size_t corner = 1u; corner + 1u < face.size(); ++corner)
                    {
                        const std::size_t triangle[3] = {face[0], face[corner], face[corner + 1u]};
                        const Vector3& p0 = positions[triangle[0]];
                        const Vector3& p1 = positions[triangle[1]];
                        const Vector3& p2 = positions[triangle[2]];
                        const float ux = p0.X - p1.X, uy = p0.Y - p1.Y, uz = p0.Z - p1.Z;
                        const float wx = p2.X - p1.X, wy = p2.Y - p1.Y, wz = p2.Z - p1.Z;
                        const Vector3 face_normal(
                            static_cast<float>(static_cast<double>(uy) * wz -
                                               static_cast<double>(uz) * wy),
                            static_cast<float>(static_cast<double>(uz) * wx -
                                               static_cast<double>(ux) * wz),
                            static_cast<float>(static_cast<double>(ux) * wy -
                                               static_cast<double>(uy) * wx));
                        const Vector3 unit = normalizeStep(face_normal);
                        for (const std::size_t vertex : triangle)
                        {
                            Vector3& bucket = accumulated[keyOf(vertex)];
                            bucket.X += unit.X;
                            bucket.Y += unit.Y;
                            bucket.Z += unit.Z;
                        }
                    }
                }
                std::vector<Vector3> generated(positions.size(), Vector3(0.0f, 0.0f, 0.0f));
                for (std::size_t vertex = 0; vertex < positions.size(); ++vertex)
                {
                    const auto found = accumulated.find(keyOf(vertex));
                    const Vector3 sum =
                        found == accumulated.end() ? Vector3(0.0f, 0.0f, 0.0f) : found->second;
                    const Vector3 answer = normalizeTimes(sum, 2);
                    // A vertex whose faces cancel out has no answer this has measured; the
                    // constant is what CNA answered everywhere before and is kept for it alone.
                    generated[vertex] =
                        (answer.X == 0.0f && answer.Y == 0.0f && answer.Z == 0.0f)
                            ? Vector3(0.0f, 0.0f, -1.0f)
                            : answer;
                }
                return generated;
            };

            // The optional channels, each indexed by the mesh's own vertices.
            std::vector<Vector3> normals;
            // `MeshNormals` carries its own face list, and it is not the mesh's: a normal is
            // indexed per face corner, not per position. SAMPLE-032's `Cube.x` has 8 positions and
            // **6** normals -- one per cube face -- with twelve `3;n,n,n;` rows saying which. Read
            // as though a normal belonged to a position it answers 8 vertices where XNA answers
            // **24**, because a vertex is a position *and* its channel values and every corner of
            // that cube has a different normal (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-144`;
            // `XNASWEEP-112` is the same rule on the FBX route).
            std::vector<std::vector<std::size_t>> normalFaces;
            if (const Canon::DirectXFileObject* object2 = Find(object, "MeshNormals"); object2 != nullptr)
            {
                std::size_t normalAt = 0u;
                const std::size_t count = Count(*object2, normalAt++);
                for (std::size_t i = 0; i < count; ++i)
                {
                    // The `.x` route normalizes what the file declares and the FBX route does
                    // not. Measured on both genuine importers: a `MeshNormals` entry of
                    // `(0, 0, 2)` comes back `(0, 0, -1)` and `0.801785` comes back `0.801784`,
                    // while an `.fbx` mesh's normals are answered as the file's own floats bit for
                    // bit (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-167`).
                    normals.push_back(ConvertNormal(
                        Vector3(At(*object2, normalAt), At(*object2, normalAt + 1), At(*object2, normalAt + 2))));
                    normalAt += 3u;
                }
                const std::size_t normalFaceCount = Count(*object2, normalAt++);
                normalFaces.reserve(normalFaceCount);
                for (std::size_t i = 0; i < normalFaceCount; ++i)
                {
                    const std::size_t corners = Count(*object2, normalAt++);
                    std::vector<std::size_t> face;
                    face.reserve(corners);
                    for (std::size_t corner = 0; corner < corners; ++corner)
                    {
                        const std::size_t index = Count(*object2, normalAt++);
                        face.push_back(index < normals.size() ? index : 0u);
                    }
                    normalFaces.push_back(std::move(face));
                }
            }
            // Two normal entries holding the same three numbers are one normal. A corner names an
            // index into the list, but what separates two vertices is the *value*: SAMPLE-057's
            // `cylinder.x` writes 418 normal entries over 21 distinct values, and XNA answers 135
            // vertices -- the distinct (position, normal value) pairs -- where keying on the index
            // gives 418, one per entry (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-148`).
            std::vector<std::size_t> canonicalNormal(normals.size());
            {
                std::map<std::tuple<float, float, float>, std::size_t> first;
                for (std::size_t i = 0; i < normals.size(); ++i)
                {
                    const std::tuple<float, float, float> key{normals[i].X, normals[i].Y,
                                                              normals[i].Z};
                    canonicalNormal[i] = first.emplace(key, i).first->second;
                }
            }
            const std::vector<Vector3> generated =
                normals.empty() ? generateNormals(positions, faces) : std::vector<Vector3>{};
            std::vector<Vector2> textureCoordinates;
            if (const Canon::DirectXFileObject* object2 = Find(object, "MeshTextureCoords");
                object2 != nullptr)
            {
                std::size_t uvAt = 0u;
                const std::size_t count = Count(*object2, uvAt++);
                for (std::size_t i = 0; i < count; ++i)
                {
                    textureCoordinates.emplace_back(At(*object2, uvAt), At(*object2, uvAt + 1));
                    uvAt += 2u;
                }
            }
            std::vector<Vector4> colors;
            if (const Canon::DirectXFileObject* object2 = Find(object, "MeshVertexColors");
                object2 != nullptr)
            {
                std::size_t colorAt = 0u;
                const std::size_t count = Count(*object2, colorAt++);
                colors.assign(vertexCount, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::size_t index = Count(*object2, colorAt++);
                    // A colour reaches the pipeline through eight bits per channel, which is what
                    // makes 0.5 come back as 0.501961 (measured, x/quad_textured.x).
                    const auto quantize = [](const float value)
                    {
                        const float clamped = std::min(1.0f, std::max(0.0f, value));
                        return static_cast<float>(static_cast<int>(clamped * 255.0f + 0.5f)) / 255.0f;
                    };
                    const Vector4 color(quantize(At(*object2, colorAt)), quantize(At(*object2, colorAt + 1)),
                                        quantize(At(*object2, colorAt + 2)), quantize(At(*object2, colorAt + 3)));
                    colorAt += 4u;
                    if (index < colors.size())
                    {
                        colors[index] = color;
                    }
                }
            }

            // Skinning: every SkinWeights names a frame and the vertices it moves.
            // SkinWeights are read only where the mesh declares a skeleton: without an
            // XSkinMeshHeader the genuine importer answers no Weights channel at all and leaves
            // the frames plain nodes (measured, x/two_bones_animated.x against
            // x/skinned_two_animations.x, which differ in nothing else).
            WeightsPerVertex weights;
            const bool skeleton = Find(object, "XSkinMeshHeader") != nullptr;
            if (skeleton)
            {
                importing.hasSkeleton = true;
            }
            for (const Canon::DirectXFileObject& child : object.children)
            {
                if (!skeleton)
                {
                    break;
                }
                if (!child.TypeIs("SkinWeights") || child.strings.empty())
                {
                    continue;
                }
                if (weights.empty())
                {
                    weights.assign(vertexCount, {});
                }
                const std::string bone = child.strings.front();
                importing.boneNames.insert(bone);
                std::size_t weightAt = 0u;
                const std::size_t count = Count(child, weightAt++);
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::size_t vertex = Count(child, weightAt + i);
                    const float weight = At(child, weightAt + count + i);
                    // A weight of zero is not carried: the genuine importer drops it rather than
                    // answering a bone that moves the vertex not at all.
                    if (vertex < weights.size() && weight != 0.0f)
                    {
                        weights[vertex].emplace_back(bone, weight);
                    }
                }
            }

            // A vertex is everything the file says about it, not its index: two entries carrying
            // the same position, the same normal and the same channel values are one vertex, which
            // is why `x_position_merge.x` declares eight and the genuine importer answers six.
            // The other channels are grouped here into a class per file vertex so that the vertex
            // key stays three integers wide (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-173`).
            std::vector<std::size_t> channelClassOf(vertexCount, 0u);
            {
                std::map<std::string, std::size_t> classes;
                for (std::size_t i = 0; i < vertexCount; ++i)
                {
                    std::string signature;
                    const auto append = [&signature](float value)
                    {
                        std::uint32_t bits = 0;
                        std::memcpy(&bits, &value, sizeof(bits));
                        signature.append(reinterpret_cast<const char*>(&bits), sizeof(bits));
                    };
                    if (i < colors.size())
                    {
                        append(colors[i].X);
                        append(colors[i].Y);
                        append(colors[i].Z);
                        append(colors[i].W);
                    }
                    signature.push_back('\x1f');
                    if (i < textureCoordinates.size())
                    {
                        append(textureCoordinates[i].X);
                        append(textureCoordinates[i].Y);
                    }
                    signature.push_back('\x1f');
                    if (i < weights.size())
                    {
                        for (const BoneWeight& weight : weights[i])
                        {
                            signature += weight.getBoneNameProperty();
                            signature.push_back('=');
                            append(weight.getWeightProperty());
                            signature.push_back(',');
                        }
                    }
                    channelClassOf[i] =
                        classes.emplace(signature, classes.size()).first->second;
                }
            }

            // One batch per material, or one batch for the whole mesh where the file names none.
            std::vector<std::size_t> materialPerFace(faces.size(), 0u);
            std::vector<std::shared_ptr<Graphics::MaterialContent>> materials;
            if (const Canon::DirectXFileObject* list = Find(object, "MeshMaterialList"); list != nullptr)
            {
                std::size_t listAt = 0u;
                const std::size_t materialCount = Count(*list, listAt++);
                const std::size_t indexCount = Count(*list, listAt++);
                for (std::size_t i = 0; i < indexCount && i < materialPerFace.size(); ++i)
                {
                    materialPerFace[i] = Count(*list, listAt + i);
                }
                for (const Canon::DirectXFileObject& child : list->children)
                {
                    if (!child.TypeIs("Material"))
                    {
                        continue;
                    }
                    materials.push_back(ReadMaterialContent(child, importing));
                }
                // A `.x` may name its materials rather than nest them -- `{phong1SG}` refers to a
                // `Material` declared elsewhere in the file, which is what every model an
                // exporter writes from Maya does. Reading only the nested ones left such a mesh
                // with no material at all and so with one part where XNA has one per material:
                // 18 of the 52 differing `.x` models in the public sample corpus are exactly that
                // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-114`).
                for (const std::string& named : list->references)
                {
                    const auto found = importing.materialsByName.find(named);
                    if (found == importing.materialsByName.end()) { continue; }
                    materials.push_back(ReadMaterialContent(*found->second, importing));
                }
                if (materials.size() != materialCount && !materials.empty())
                {
                    // The list's own count disagreeing with the materials it holds is a file that
                    // parsed but does not describe itself.
                    throw InvalidContentException(Unreadable("E_FAIL"));
                }
            }
            // A triangle two of whose corners are the same corner is discarded, and the rest of
            // its polygon is not: SAMPLE-141's `racer.x` declares 2,447 faces in its chassis and
            // the genuine importer answers 2,445, the two it drops being `(1067,1068,1067)` and
            // `(1077,1078,1077)` (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-198`). What makes
            // two corners the same is the *vertex* and not the index: `x_face_degenerate.x` names
            // one vertex twice in a face and loses it, and `x_face_degenerate_normals.x` names the
            // same vertex twice with two different normals and keeps it, because those are two
            // vertices. An n-gon loses only the fan triangles that are degenerate
            // (`x_face_degenerate_quad.x`), a material whose faces all go produces no geometry,
            // and a position no surviving triangle names is not in the mesh's position list.
            // `VertexDuplicationIndices` says which vertex each one was split from, and the
            // genuine importer treats two corners whose vertices share an entry as one corner even
            // where their positions and their normals differ. SAMPLE-141's `target.x` declares 180
            // faces and XNA answers 170: the ten it drops are exactly the ten that name two
            // vertices of one duplication group, and collapsing a single pair of that file's map
            // drops exactly the faces naming both (`XNASWEEP-198`).
            //
            // The two conditions on it are measured rather than derived, and are stated as
            // measurements: the same map is *inert* when the mesh declares no `MeshNormals`
            // (`target.x`'s own data with that block removed answers all 180) and when it declares
            // `MeshTextureCoords` (the genuine importer splits a vertex instead of dropping the
            // face). What is behind that gate is not understood, so the rule is kept inside the
            // window it was measured in rather than generalized past it; §11.3.1 of the plan
            // records the probe families the window does not explain.
            std::vector<std::size_t> originalOf;
            if (const Canon::DirectXFileObject* duplication = Find(object, "VertexDuplicationIndices");
                duplication != nullptr && !normals.empty() && Find(object, "MeshTextureCoords") == nullptr)
            {
                std::size_t duplicationAt = 0u;
                const std::size_t count = Count(*duplication, duplicationAt++);
                // `nOriginalVertices`: declaring it wrongly changes nothing the importer answers.
                ++duplicationAt;
                originalOf.resize(vertexCount);
                for (std::size_t i = 0; i < vertexCount; ++i) { originalOf[i] = i; }
                for (std::size_t i = 0; i < count && i < vertexCount; ++i)
                {
                    originalOf[i] = Count(*duplication, duplicationAt + i);
                }
            }

            const auto normalOfCorner = [&](const std::size_t face, const std::size_t corner)
            {
                if (face < normalFaces.size() && corner < normalFaces[face].size())
                {
                    return normalFaces[face][corner];
                }
                return faces[face][corner];
            };
            const auto sameCorner = [&](const std::size_t face, const std::size_t left,
                                        const std::size_t right)
            {
                const std::size_t leftVertex = faces[face][left];
                const std::size_t rightVertex = faces[face][right];
                if (!originalOf.empty() && originalOf[leftVertex] == originalOf[rightVertex])
                {
                    return true;
                }
                // The file's own vertex index, not the merged position: `x_position_merge.x`
                // names vertices 7 and 0 in one face, they hold the same position *and* the same
                // normal so they are one vertex in the geometry, and the genuine importer keeps
                // the triangle -- a face with two corners at the same point is not the same thing
                // as a face that names one corner twice.
                if (leftVertex != rightVertex) { return false; }
                const std::size_t leftNormal = normalOfCorner(face, left);
                const std::size_t rightNormal = normalOfCorner(face, right);
                const std::size_t leftKey =
                    leftNormal < canonicalNormal.size() ? canonicalNormal[leftNormal] : leftNormal;
                const std::size_t rightKey =
                    rightNormal < canonicalNormal.size() ? canonicalNormal[rightNormal] : rightNormal;
                return leftKey == rightKey;
            };
            std::vector<std::vector<std::array<std::size_t, 3>>> keptTriangles(faces.size());
            for (std::size_t face = 0; face < faces.size(); ++face)
            {
                for (std::size_t corner = 2; corner < faces[face].size(); ++corner)
                {
                    const std::array<std::size_t, 3> triangle{0u, corner - 1u, corner};
                    if (sameCorner(face, triangle[0], triangle[1]) ||
                        sameCorner(face, triangle[1], triangle[2]) ||
                        sameCorner(face, triangle[0], triangle[2]))
                    {
                        continue;
                    }
                    keptTriangles[face].push_back(triangle);
                }
            }

            const std::size_t batches = materials.empty() ? 1u : materials.size();

            for (std::size_t batch = 0; batch < batches; ++batch)
            {
                // The batch's positions come first, in ascending file order, because that is the
                // order the genuine importer adds them in (`XNASWEEP-173`).
                {
                    std::set<std::size_t> referenced;
                    for (std::size_t face = 0; face < faces.size(); ++face)
                    {
                        if (!materials.empty() && materialPerFace[face] != batch)
                        {
                            continue;
                        }
                        for (const std::array<std::size_t, 3>& triangle : keptTriangles[face])
                        {
                            for (const std::size_t corner : triangle)
                            {
                                referenced.insert(faces[face][corner]);
                            }
                        }
                    }
                    for (const std::size_t vertex : referenced)
                    {
                        placePosition(vertex);
                    }
                }
                // A batch's vertices are the mesh positions its own faces name, in first-use
                // order, which is what makes its indices local and its position indices shared.
                std::vector<std::size_t> used;
                std::vector<std::size_t> usedNormals;
                std::map<std::tuple<std::size_t, std::size_t, std::size_t>, SharpRuntime::intcs> local;
                std::vector<SharpRuntime::intcs> indices;
                for (std::size_t face = 0; face < faces.size(); ++face)
                {
                    if (!materials.empty() && materialPerFace[face] != batch)
                    {
                        continue;
                    }
                    // A polygon becomes a triangle fan, which is how every reader of this format
                    // turns an n-gon into triangles; the fan triangles a corner test discarded are
                    // not emitted and their corners create no vertex (`XNASWEEP-198`).
                    const auto place = [&](const std::size_t corner)
                    {
                        const std::size_t vertex = faces[face][corner];
                        // The key is the position *and* the normal the corner names, because a
                        // vertex is both (`XNASWEEP-144`) -- and the position is the *merged* one,
                        // so two file vertices that name the same position and the same normal are
                        // one vertex. `x_position_merge.x` declares eight vertices over five
                        // positions and the genuine importer answers six (`XNASWEEP-173`).
                        std::size_t normalIndex = vertex;
                        if (face < normalFaces.size() && corner < normalFaces[face].size())
                        {
                            normalIndex = normalFaces[face][corner];
                        }
                        // A *generated* normal belongs to the position rather than to the corner,
                        // because that is what it is accumulated into -- so two file vertices at
                        // the same position are one vertex, where two that name the same position
                        // and different declared normals are two. `x_generated_normals_exact.x` is
                        // twenty-four triangles sharing an origin: the genuine importer answers 49
                        // vertices for its 49 positions, and keying on the corner answers 72
                        // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-233`).
                        const std::size_t normalKey =
                            normals.empty()
                                ? positionOf[vertex]
                                : (normalIndex < canonicalNormal.size() ? canonicalNormal[normalIndex]
                                                                        : normalIndex);
                        const std::tuple<std::size_t, std::size_t, std::size_t> key{
                            positionOf[vertex], normalKey, channelClassOf[vertex]};
                        const auto found = local.find(key);
                        if (found == local.end())
                        {
                            const auto assigned = static_cast<SharpRuntime::intcs>(used.size());
                            local.emplace(key, assigned);
                            used.push_back(vertex);
                            usedNormals.push_back(normalIndex);
                            return assigned;
                        }
                        return found->second;
                    };
                    for (const std::array<std::size_t, 3>& triangle : keptTriangles[face])
                    {
                        for (const std::size_t corner : triangle)
                        {
                            indices.push_back(place(corner));
                        }
                    }
                }
                if (used.empty())
                {
                    continue;
                }
                auto geometry = std::make_shared<GeometryContent>();
                mesh->getGeometryProperty().Add(geometry);
                std::vector<SharpRuntime::intcs> positionIndices;
                positionIndices.reserve(used.size());
                for (const std::size_t vertex : used)
                {
                    positionIndices.push_back(static_cast<SharpRuntime::intcs>(positionOf[vertex]));
                }
                geometry->getVerticesProperty().AddRange(positionIndices);
                geometry->getIndicesProperty().AddRange(indices);
                if (batch < materials.size())
                {
                    geometry->setMaterialProperty(materials[batch]);
                }

                // The channel order the genuine importer answers: weights, normals, colours, then
                // texture coordinates.
                if (!weights.empty())
                {
                    std::vector<BoneWeightCollection> channel;
                    channel.reserve(used.size());
                    for (const std::size_t vertex : used)
                    {
                        BoneWeightCollection collection;
                        for (const BoneWeight& weight : weights[vertex])
                        {
                            collection.Add(weight);
                        }
                        channel.push_back(std::move(collection));
                    }
                    geometry->getVerticesProperty().getChannelsProperty().Add<BoneWeightCollection>(
                        VertexChannelNames::Weights(0), channel);
                }
                // A file's own normals take their place among the channels the file declares; a
                // channel the importer *generates* because the file has none is appended after
                // them. Measured both ways: `quad_textured.x` declares MeshNormals and answers
                // Normal, Color, TextureCoordinate, while `two_textures.x` declares none and
                // answers TextureCoordinate, Normal (tests/reference/xna40/model, x/*). The order
                // is not cosmetic -- it is the order of the elements in the vertex buffer
                // (plans/plan_xnapipeline_parity.md XNAPP-266).
                const auto addNormals = [&]
                {
                    std::vector<Vector3> channel;
                    channel.reserve(used.size());
                    for (std::size_t v = 0; v < used.size(); ++v)
                    {
                        // A file with no MeshNormals still answers a normal channel: the genuine
                        // importer generates one from the geometry (measured, x/bare_mesh.x and
                        // x/generated_normals.x).
                        const std::size_t normalIndex = usedNormals[v];
                        const std::size_t vertex = used[v];
                        channel.push_back(normalIndex < normals.size() ? normals[normalIndex]
                                          : vertex < generated.size()  ? generated[vertex]
                                                                       : Vector3(0.0f, 0.0f, -1.0f));
                    }
                    geometry->getVerticesProperty().getChannelsProperty().Add<Vector3>(
                        VertexChannelNames::Normal(), channel);
                };
                if (!normals.empty()) { addNormals(); }
                if (!colors.empty())
                {
                    std::vector<Vector4> channel;
                    channel.reserve(used.size());
                    for (const std::size_t vertex : used)
                    {
                        channel.push_back(vertex < colors.size() ? colors[vertex]
                                                                 : Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                    }
                    geometry->getVerticesProperty().getChannelsProperty().Add<Vector4>(
                        VertexChannelNames::Color(0), channel);
                }
                if (!textureCoordinates.empty())
                {
                    std::vector<Vector2> channel;
                    channel.reserve(used.size());
                    for (const std::size_t vertex : used)
                    {
                        channel.push_back(vertex < textureCoordinates.size() ? textureCoordinates[vertex]
                                                                            : Vector2(0.0f, 0.0f));
                    }
                    geometry->getVerticesProperty().getChannelsProperty().Add<Vector2>(
                        VertexChannelNames::TextureCoordinate(0), channel);
                }
                if (normals.empty()) { addNormals(); }
            }
            // A mesh no triangle survived is not a node: the genuine importer answers a root with
            // no children at all for a file whose every face is degenerate, and for one that
            // declares no faces (measured, `x_face_degenerate_all.x` and `x_no_faces.x`).
            if (mesh->getGeometryProperty().getCountProperty() == 0)
            {
                return nullptr;
            }
            return mesh;
        }

        void ReadFrame(const Canon::DirectXFileObject& object, const std::shared_ptr<NodeContent>& node,
                       Importing& importing)
        {
            node->setNameProperty(object.name);
            importing.framesByName[object.name] = node;
            // A frame's meshes come before its child frames, whatever order the file wrote them
            // in (measured: a file declaring Frame Bone0 then Mesh Skin answers Skin first).
            for (const Canon::DirectXFileObject& child : object.children)
            {
                if (child.TypeIs("FrameTransformMatrix"))
                {
                    node->setTransformProperty(Convert(ReadMatrix(child, 0u)));
                }
            }
            for (const Canon::DirectXFileObject& child : object.children)
            {
                if (child.TypeIs("Mesh"))
                {
                    if (std::shared_ptr<MeshContent> mesh = ReadMesh(child, importing); mesh != nullptr)
                    {
                        node->getChildrenProperty().Add(std::move(mesh));
                    }
                }
            }
            for (const Canon::DirectXFileObject& child : object.children)
            {
                if (child.TypeIs("Frame"))
                {
                    auto sub = std::make_shared<NodeContent>();
                    ReadFrame(child, sub, importing);
                    node->getChildrenProperty().Add(sub);
                }
            }
        }

        /** @brief Replaces a frame with a BoneContent carrying the same state, in place. */
        [[nodiscard]] std::shared_ptr<NodeContent> AsBone(const std::shared_ptr<NodeContent>& node)
        {
            auto bone = std::make_shared<BoneContent>();
            bone->setNameProperty(node->getNameProperty());
            bone->setTransformProperty(node->getTransformProperty());
            bone->setIdentityProperty(node->getIdentityProperty());
            while (node->getChildrenProperty().getCountProperty() > 0)
            {
                const std::shared_ptr<NodeContent> child =
                    static_cast<const System::Collections::ObjectModel::Collection<
                        std::shared_ptr<NodeContent>>&>(node->getChildrenProperty())[0];
                node->getChildrenProperty().RemoveAt(0);
                bone->getChildrenProperty().Add(child);
            }
            return bone;
        }

        /** @brief Turns every frame a SkinWeights named, and its ancestors' subtree, into bones. */
        void PromoteBones(const std::shared_ptr<NodeContent>& node, Importing& importing)
        {
            auto& children = node->getChildrenProperty();
            for (SharpRuntime::intcs i = 0; i < children.getCountProperty(); ++i)
            {
                const std::shared_ptr<NodeContent> child =
                    static_cast<const System::Collections::ObjectModel::Collection<
                        std::shared_ptr<NodeContent>>&>(children)[i];
                const bool isBone = importing.boneNames.count(child->getNameProperty()) != 0;
                if (isBone && std::dynamic_pointer_cast<MeshContent>(child) == nullptr &&
                    std::dynamic_pointer_cast<BoneContent>(child) == nullptr)
                {
                    const std::shared_ptr<NodeContent> bone = AsBone(child);
                    children.RemoveAt(i);
                    children.Insert(i, bone);
                    importing.framesByName[bone->getNameProperty()] = bone;
                    PromoteBones(bone, importing);
                    continue;
                }
                PromoteBones(child, importing);
            }
        }

        /** @brief The topmost bone of the skeleton, or null when the file declared none. */
        [[nodiscard]] std::shared_ptr<NodeContent> FindSkeletonRoot(const std::shared_ptr<NodeContent>& node)
        {
            if (std::dynamic_pointer_cast<BoneContent>(node) != nullptr)
            {
                return node;
            }
            const auto& children = static_cast<const System::Collections::ObjectModel::Collection<
                std::shared_ptr<NodeContent>>&>(node->getChildrenProperty());
            for (SharpRuntime::intcs i = 0; i < children.getCountProperty(); ++i)
            {
                if (const std::shared_ptr<NodeContent> found = FindSkeletonRoot(children[i]); found != nullptr)
                {
                    return found;
                }
            }
            return nullptr;
        }

        /** @brief One `Animation` object: the target it names and the keys it holds. */
        struct ReadAnimation
        {
            std::string target;
            std::map<double, Matrix> keys;
            double lastTick = 0.0;
        };

        /**
         * @brief Merges an `Animation`'s separate key lists into one matrix track.
         *
         * A `.x` animation stores rotation, scale and position as three key lists with times of
         * their own; XNA answers a single channel of matrices at the union of those times
         * (measured, x/skinned_animated.x, whose two lists of three and two keys become one
         * channel of three). Each component is held at the last key at or before the time, which
         * is what makes the merged track agree with each list where they share a time.
         */
        [[nodiscard]] ReadAnimation ReadOneAnimation(const Canon::DirectXFileObject& object)
        {
            ReadAnimation animation;
            if (!object.references.empty())
            {
                animation.target = object.references.front();
            }
            std::map<double, Quaternion> rotations;
            std::map<double, Vector3> scales;
            std::map<double, Vector3> positions;
            std::map<double, Matrix> matrices;
            std::set<double> times;
            for (const Canon::DirectXFileObject& child : object.children)
            {
                if (child.TypeIs("Frame") && animation.target.empty())
                {
                    animation.target = child.name;
                    continue;
                }
                if (!child.TypeIs("AnimationKey"))
                {
                    continue;
                }
                std::size_t at = 0u;
                const std::size_t kind = Count(child, at++);
                const std::size_t count = Count(child, at++);
                for (std::size_t i = 0; i < count; ++i)
                {
                    const double time = At(child, at++);
                    const std::size_t values = Count(child, at++);
                    times.insert(time);
                    animation.lastTick = std::max(animation.lastTick, time);
                    if (kind == 0u && values >= 4u)
                    {
                        // A `.x` rotation key is w, x, y, z, and the rotation it names is the
                        // inverse of the quaternion those spell.
                        rotations[time] = Quaternion(-At(child, at + 1), -At(child, at + 2),
                                                     -At(child, at + 3), At(child, at));
                    }
                    else if (kind == 1u && values >= 3u)
                    {
                        scales[time] = Vector3(At(child, at), At(child, at + 1), At(child, at + 2));
                    }
                    else if (kind == 2u && values >= 3u)
                    {
                        positions[time] = Vector3(At(child, at), At(child, at + 1), At(child, at + 2));
                    }
                    else if (kind == 4u && values >= 16u)
                    {
                        matrices[time] = ReadMatrix(child, at);
                    }
                    at += values;
                }
            }
            // Each component is *interpolated* at the union time, not held: a rotation list with
            // keys at 0 and 20 and a position list with keys at 0, 10 and 20 answer a rotation
            // half way through at the merged time 10 (measured, x/skinned_animated.x, whose
            // middle key carries a 45-degree rotation the rotation list never states).
            const auto sample = [](const auto& track, const double time, const auto fallback,
                                   const auto& blend)
            {
                if (track.empty())
                {
                    return fallback;
                }
                auto after = track.lower_bound(time);
                if (after != track.end() && after->first == time)
                {
                    return after->second;
                }
                if (after == track.begin())
                {
                    return track.begin()->second;
                }
                if (after == track.end())
                {
                    return std::prev(track.end())->second;
                }
                auto before = std::prev(after);
                const double span = after->first - before->first;
                const float amount = span > 0.0 ? static_cast<float>((time - before->first) / span) : 0.0f;
                return blend(before->second, after->second, amount);
            };
            const auto lerp3 = [](const Vector3& from, const Vector3& to, const float amount)
            { return Vector3::Lerp(from, to, amount); };
            const auto slerp = [](const Quaternion& from, const Quaternion& to, const float amount)
            { return Quaternion::Slerp(from, to, amount); };
            const auto lerpMatrix = [](const Matrix& from, const Matrix& to, const float amount)
            { return amount < 0.5f ? from : to; };

            for (const double time : times)
            {
                if (!matrices.empty())
                {
                    animation.keys[time] =
                        Convert(sample(matrices, time, Matrix::getIdentityProperty(), lerpMatrix));
                    continue;
                }
                const Quaternion rotation = sample(rotations, time, Quaternion::Identity, slerp);
                const Vector3 scale = sample(scales, time, Vector3(1.0f, 1.0f, 1.0f), lerp3);
                const Vector3 position = sample(positions, time, Vector3(0.0f, 0.0f, 0.0f), lerp3);
                Matrix transform = Matrix::CreateScale(scale) *
                                   Matrix::CreateFromQuaternion(rotation) *
                                   Matrix::CreateTranslation(position);
                animation.keys[time] = Convert(transform);
            }
            return animation;
        }

        void ReadAnimationSet(const Canon::DirectXFileObject& object,
                              const std::shared_ptr<NodeContent>& root, Importing& importing)
        {
            std::vector<ReadAnimation> animations;
            double lastTick = 0.0;
            for (const Canon::DirectXFileObject& child : object.children)
            {
                if (!child.TypeIs("Animation"))
                {
                    continue;
                }
                ReadAnimation one = ReadOneAnimation(child);
                lastTick = std::max(lastTick, one.lastTick);
                animations.push_back(std::move(one));
            }
            if (animations.empty())
            {
                return;
            }
            const double rate = importing.ticksPerSecond > 0.0 ? importing.ticksPerSecond
                                                               : DefaultTicksPerSecond;
            const auto toTicks = [rate](const double tick)
            {
                return static_cast<SharpRuntime::longcs>(tick / rate * 10000000.0);
            };
            // The duration is the last key's time truncated to whole milliseconds, while the keys
            // keep their full precision (measured: 20 ticks at the default rate is 41666 ticks of
            // key time and a duration of 40000).
            const System::TimeSpan duration(toTicks(lastTick) / 10000 * 10000);

            // Where the file declares a skeleton, every animation in the set lands on the
            // skeleton's root bone as one AnimationContent with a channel per target; where it
            // does not, each animation lands on the node it names (measured,
            // x/skinned_two_animations.x against x/two_bones_animated.x).
            const std::shared_ptr<NodeContent> skeleton =
                importing.hasSkeleton ? FindSkeletonRoot(root) : nullptr;
            const auto channelFor = [&](const ReadAnimation& one) -> std::shared_ptr<AnimationChannel>
            {
                auto channel = std::make_shared<AnimationChannel>();
                for (const auto& [time, transform] : one.keys)
                {
                    channel->Add(std::make_shared<AnimationKeyframe>(System::TimeSpan(toTicks(time)),
                                                                     transform));
                }
                return channel;
            };
            if (skeleton != nullptr)
            {
                auto content = std::make_shared<AnimationContent>();
                content->setNameProperty(object.name);
                content->setDurationProperty(duration);
                for (const ReadAnimation& one : animations)
                {
                    content->getChannelsProperty().Add(one.target, channelFor(one));
                }
                skeleton->getAnimationsProperty().Add(object.name, content);
                return;
            }
            for (const ReadAnimation& one : animations)
            {
                const auto found = importing.framesByName.find(one.target);
                if (found == importing.framesByName.end())
                {
                    continue;
                }
                auto content = std::make_shared<AnimationContent>();
                content->setNameProperty(object.name);
                content->setDurationProperty(duration);
                content->getChannelsProperty().Add(one.target, channelFor(one));
                found->second->getAnimationsProperty().Add(object.name, content);
            }
        }
    }

    XImporter::~XImporter() { Dispose(false); }

    void XImporter::Dispose() { Dispose(true); }

    void XImporter::Dispose(const bool disposing)
    {
        (void)disposing;
        // Nothing native is held open: the file is read whole and closed inside Import. The
        // pattern is here because XNA declares it, and calling it twice is accepted.
        disposed_ = true;
    }

    std::shared_ptr<Graphics::NodeContent> XImporter::Import(const std::string& filename,
                                                             ContentImporterContext& context)
    {
        (void)context;
        std::error_code error;
        if (!std::filesystem::exists(filename, error) || error)
        {
            throw System::IO::FileNotFoundException("Could not locate model file \"" + filename + "\".");
        }
        std::vector<std::uint8_t> bytes;
        {
            std::ifstream file(filename, std::ios::binary);
            const std::vector<char> read((std::istreambuf_iterator<char>(file)),
                                         std::istreambuf_iterator<char>());
            bytes.assign(read.begin(), read.end());
        }
        Canon::DirectXFile parsed;
        try
        {
            parsed = Canon::ReadDirectXFile(bytes);
        }
        catch (const Canon::DirectXFileException& failure)
        {
            throw InvalidContentException(Unreadable(failure.CodeName()));
        }

        Importing importing;
        importing.directory = std::filesystem::path(filename).parent_path();
        for (const Canon::DirectXFileObject& object : parsed.objects)
        {
            if (object.TypeIs("AnimTicksPerSecond") && !object.numbers.empty())
            {
                importing.ticksPerSecond = object.numbers.front();
            }
        }
        // Every named object a `{Name}` reference can reach, wherever it is declared.
        const auto index = [&importing](const Canon::DirectXFileObject& object, auto&& self) -> void
        {
            if (object.TypeIs("Material") && !object.name.empty())
            {
                importing.materialsByName.emplace(object.name, &object);
            }
            for (const Canon::DirectXFileObject& child : object.children) { self(child, self); }
        };
        for (const Canon::DirectXFileObject& object : parsed.objects) { index(object, index); }

        // A file whose single top-level object is a Frame answers that frame as the root; any
        // other shape answers an unnamed root holding the objects (measured, x/quad_textured.x
        // against x/bare_mesh.x).
        std::size_t topLevelFrames = 0u;
        std::size_t topLevelData = 0u;
        for (const Canon::DirectXFileObject& object : parsed.objects)
        {
            if (object.TypeIs("Frame")) { ++topLevelFrames; }
            if (object.TypeIs("Frame") || object.TypeIs("Mesh")) { ++topLevelData; }
        }
        auto root = std::make_shared<NodeContent>();
        if (topLevelFrames == 1u && topLevelData == 1u)
        {
            for (const Canon::DirectXFileObject& object : parsed.objects)
            {
                if (object.TypeIs("Frame"))
                {
                    ReadFrame(object, root, importing);
                }
            }
        }
        else
        {
            for (const Canon::DirectXFileObject& object : parsed.objects)
            {
                if (object.TypeIs("Frame"))
                {
                    auto child = std::make_shared<NodeContent>();
                    ReadFrame(object, child, importing);
                    root->getChildrenProperty().Add(child);
                }
                else if (object.TypeIs("Mesh"))
                {
                    if (std::shared_ptr<MeshContent> mesh = ReadMesh(object, importing); mesh != nullptr)
                    {
                        root->getChildrenProperty().Add(std::move(mesh));
                    }
                }
            }
        }
        PromoteBones(root, importing);
        if (importing.hasSkeleton && std::dynamic_pointer_cast<BoneContent>(root) == nullptr &&
            importing.boneNames.count(root->getNameProperty()) != 0)
        {
            // A skeleton whose root is the file's own root frame stays a NodeContent, because a
            // root cannot be replaced in place; nothing measured shows XNA doing otherwise.
        }
        for (const Canon::DirectXFileObject& object : parsed.objects)
        {
            if (object.TypeIs("AnimationSet"))
            {
                ReadAnimationSet(object, root, importing);
            }
        }
        return root;
    }

    ContentImporterAttribute XImporter::Attribute()
    {
        ContentImporterAttribute attribute(".x");
        attribute.setDefaultProcessorProperty("ModelProcessor");
        attribute.setDisplayNameProperty("X File - XNA Framework");
        attribute.setCacheImportedDataProperty(true);
        return attribute;
    }

    const std::string& XImporter::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
