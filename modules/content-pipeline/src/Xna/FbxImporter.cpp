// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/ModelImporters.hpp"

#include <array>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "CNA/Content/Pipeline/FbxFileReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/StockMaterials.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannelNames.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "System/IO/FileNotFoundException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    namespace
    {
        namespace Canon = CNA::Content::Pipeline;

        /**
         * @brief One of FBX's transform values, kept in the precision the file writes it in.
         *
         * FBX writes every transform term as a `double` and XNA's importer composes them as
         * doubles; rounding each to `float` first is visible wherever two of them cancel.
         * SAMPLE-138's `photograph.fbx` is the corpus's case: a `ScalingOffset` of
         * 1.05205948463126 against a `ScalingPivot` of -1.06268632411957 under a scaling of 0.01
         * leaves 2.35e-08, and a `float` pivot leaves three times that
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-162`).
         */
        using Triple = std::array<double, 3>;

        /**
         * @brief The corner triples XNA's FBX path triangulates an `n`-corner polygon into.
         *
         * Not a fan. The FBX SDK inside XNA's importer answers a strip, and what it answers is a
         * function of the corner count alone: a concave quad and a convex one give the same
         * triangles, and so does the same octagon walked from a different corner or backwards.
         * Measured for 3 to 20 corners on `tests/assets/xna40/model/fbx_polygon*.fbx`
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-164`). The winding is reversed, which is
         * the one thing FBX and `.x` differ on that changes what a triangle faces.
         */
        std::vector<std::array<std::size_t, 3>> TrianglesOfPolygon(const std::size_t cornerCount)
        {
            std::vector<std::array<std::size_t, 3>> triangles;
            if (cornerCount < 3u)
            {
                return triangles;
            }
            triangles.reserve(cornerCount - 2u);
            triangles.push_back({2u, 1u, 0u});
            if (cornerCount >= 4u)
            {
                triangles.push_back({0u, 3u, 2u});
                std::size_t low = 3u;
                std::size_t high = cornerCount - 1u;
                bool firstStep = true;
                while (low < high)
                {
                    triangles.push_back({high, low, firstStep ? 0u : high + 1u});
                    firstStep = false;
                    if (low + 1u >= high)
                    {
                        break;
                    }
                    triangles.push_back({high, low + 1u, low});
                    ++low;
                    --high;
                }
            }
            return triangles;
        }

        /**
         * @brief The order that triangulation introduces each of a polygon's corners.
         *
         * The corners are numbered in this order rather than the order the polygon lists them:
         * XNA answers a hexagon's control points as 0,1,2,3,5,4 and a twelve-gon's as
         * 0,1,2,3,11,4,10,5,9,6,8,7, which is the strip's two pointers walking in from the ends.
         */
        std::vector<std::size_t> CornerVisitOrder(const std::size_t cornerCount)
        {
            std::vector<std::size_t> order;
            order.reserve(cornerCount);
            for (std::size_t c = 0; c < cornerCount && c < 4u; ++c)
            {
                order.push_back(c);
            }
            if (cornerCount > 4u)
            {
                std::size_t low = 4u;
                std::size_t high = cornerCount - 1u;
                bool takeHigh = true;
                while (order.size() < cornerCount)
                {
                    order.push_back(takeHigh ? high-- : low++);
                    takeHigh = !takeHigh;
                }
            }
            return order;
        }
        using Graphics::BasicMaterialContent;
        using Graphics::GeometryContent;
        using Graphics::MeshContent;
        using Graphics::NodeContent;
        using Graphics::VertexChannelNames;

        /** @brief The two sentences XNA gives for an FBX it could not read. */
        [[nodiscard]] std::string CannotInitialize()
        {
            return "Error code: 0 encountered when initializing FBX file loader. The file is either "
                   "corrupted or it is not a valid FBX file.";
        }

        [[nodiscard]] std::string NotAnFbx()
        {
            return "Could not detect file format. The file is either corrupted or it is not a valid "
                   "FBX file.";
        }

        [[nodiscard]] std::string CannotImport()
        {
            return "Error code: 0 encountered when importing the scene. The file is either corrupted "
                   "or it is not a valid FBX file.";
        }

        /** @brief The bare name out of an FBX `Model::Name` or `Name\\x00\\x01Model` spelling. */
        [[nodiscard]] std::string BareName(const std::string& raw)
        {
            // FBX 6 writes `Model::Name`; FBX 7 writes `Name` followed by a null, then the class.
            const std::size_t nul = raw.find('\0');
            if (nul != std::string::npos)
            {
                return raw.substr(0, nul);
            }
            const std::size_t colons = raw.find("::");
            return colons == std::string::npos ? raw : raw.substr(colons + 2u);
        }

        /** @brief What one FBX object is, before the graph is built out of the connections. */
        struct Object
        {
            std::string name;
            std::string kind;                 // "Mesh", "Null", "LimbNode", "Material", ...
            const Canon::FbxNode* node = nullptr;
            std::vector<std::int64_t> children;
            std::vector<std::int64_t> materials;
            /** @brief `Texture` objects a `Connect` names against this one, in file order. */
            std::vector<std::int64_t> textures;
            Triple translation{0.0, 0.0, 0.0};
            Triple rotation{0.0, 0.0, 0.0};
            Triple preRotation{0.0, 0.0, 0.0};
            Triple postRotation{0.0, 0.0, 0.0};
            Triple rotationOffset{0.0, 0.0, 0.0};
            Triple rotationPivot{0.0, 0.0, 0.0};
            Triple scalingOffset{0.0, 0.0, 0.0};
            Triple scalingPivot{0.0, 0.0, 0.0};
            Triple scaling{1.0, 1.0, 1.0};
            /**
             * @brief The scene's `UnitScaleFactor`, on the top-level nodes it reaches.
             *
             * It multiplies the *composed* local transform and not the `Lcl Scaling` and
             * `Lcl Translation` it is built from, which is only the same thing when the node has no
             * scaling pivot: `G100S01Piv.fbx` is a scale of 0.01 under a unit of 100 with a
             * `ScalingPivot` of (4, 5, 6), and folding the unit into the scaling first makes the
             * combined scale exactly 1 and the pivot term `(1 - 1) * Sp` vanish, where XNA answers
             * `100 * (1 - 0.01) * Sp = (396, 495, 594)` (XNASWEEP-162).
             */
            double unitScale = 1.0;
            /** @brief `RotationActive`, which is what decides whether `PreRotation` counts. */
            bool rotationActive = false;
            /**
             * @brief Whether this object is an FBX 7 `Geometry`, the mesh data of the model it is
             *        connected to rather than a node of its own.
             *
             * FBX 6 writes a mesh's `Vertices` inside the `Model`; FBX 7 splits them into a
             * `Geometry` object connected to it. SAMPLE-061's `marble.FBX` is the second shape, and
             * reading the `Geometry` as a node of its own answers two bones -- `marble` and a
             * nameless child -- where XNA answers one, `marble`, carrying the mesh
             * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-151`).
             */
            bool isGeometryData = false;
            /** @brief The geometry's own offset from the node, FBX's `Geometric*` properties. */
            Triple geometricTranslation{0.0, 0.0, 0.0};
            Triple geometricRotation{0.0, 0.0, 0.0};
            Triple geometricScaling{1.0, 1.0, 1.0};
            bool attached = false;
            /** @brief Whether a `Connect` names this object as a child of the scene root. */
            bool inScene = false;
        };

        /**
         * @brief Whether an FBX object of this class becomes a node in the imported graph.
         *
         * A camera is not one, and neither is the camera switcher. Every FBX an exporter writes
         * carries the producer camera set -- 833 `Camera` models and 106 `CameraSwitcher` ones
         * over the 132 FBX sources of the public XNA sample corpus -- and none of them ever
         * appears in what the genuine importer answers: `Cone.fbx` has nine models, eight of them
         * cameras, and XNA answers a single `MeshContent` node. CNA answered ten bones for it
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-111`).
         *
         * `Light` and `Marker` are not nodes either, which was measured rather than assumed:
         * `co_light_then_null` and `co_marker_then_null` hand the genuine importer a scene of two
         * top-level models, one of each class beside a `Null`, and it answers a single node
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-161`).
         *
         * @param kind The object's FBX class.
         * @return Whether it is part of the node graph.
         */
        [[nodiscard]] bool IsSceneNode(const std::string& kind)
        {
            return kind != "Material" && kind != "Texture" && kind != "Camera" &&
                   kind != "CameraSwitcher" && kind != "Light" && kind != "Marker";
        }

        /**
         * @brief Whether an FBX object of this class becomes a `BoneContent` rather than a node.
         *
         * `LimbNode` is the class every skeleton joint an exporter writes carries, and `Root` is
         * the one 3ds Max writes for a biped's root. Both answer `BoneContent` from the genuine
         * importer, measured on `co_limbnode_then_null` and `co_root_then_null`
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-161`).
         *
         * @param kind The object's FBX class.
         * @return Whether it is a bone.
         */
        [[nodiscard]] bool IsBoneNode(const std::string& kind)
        {
            return kind == "LimbNode" || kind == "Root";
        }

        /** @brief The first bone a depth-first walk reaches, or null where there is none. */
        [[nodiscard]] std::shared_ptr<Graphics::BoneContent> FirstBone(
            const std::shared_ptr<NodeContent>& node)
        {
            if (auto bone = std::dynamic_pointer_cast<Graphics::BoneContent>(node))
            {
                return bone;
            }
            for (const std::shared_ptr<NodeContent>& child : node->getChildrenProperty())
            {
                if (std::shared_ptr<Graphics::BoneContent> found = FirstBone(child))
                {
                    return found;
                }
            }
            return nullptr;
        }

        /** @brief Counts the maximal bone subtrees a scene holds -- its skeletons. */
        void CountSkeletons(const std::shared_ptr<NodeContent>& node, bool insideSkeleton,
                            std::vector<std::string>& roots)
        {
            const bool isBone = std::dynamic_pointer_cast<Graphics::BoneContent>(node) != nullptr;
            if (isBone && !insideSkeleton)
            {
                roots.push_back(node->getNameProperty());
            }
            for (const std::shared_ptr<NodeContent>& child : node->getChildrenProperty())
            {
                CountSkeletons(child, isBone, roots);
            }
        }

        /**
         * @brief Moves the scene's first bone to the root, keeping where it stands in the world.
         *
         * The genuine importer promotes the skeleton's root: the first bone a depth-first walk
         * reaches leaves whatever node it was connected under and becomes the *last* child of the
         * scene's root node, with its own transform re-expressed against that root -- so a bone
         * two levels down comes back carrying its absolute transform, and one that was already a
         * child of the root comes back unchanged but at the end of the list. Measured on
         * `cd_deep_bone` (a bone under a node with a transform answers that node's transform
         * composed into its own), `cb_four_mixed` (only the first bone moves) and
         * `co_hammer_shape` (`SAMPLE-142`'s own shape: the `Root`-class bone comes back after the
         * `Null` the file connects second).
         *
         * Where the bone *is* the root -- a scene whose single top-level object is the skeleton --
         * the same expression makes its transform relative to itself, which is the identity: that
         * is why every RobotGame mech answers an identity root where its file gives it a quarter
         * turn (`cs_bone_dusk`, and `plans/plan_xna_sample_xnb_sweep.md` `XNASWEEP-150`).
         *
         * @param root The scene's root node.
         * @return The same root.
         */
        [[nodiscard]] std::shared_ptr<NodeContent> PromoteSkeletonRoot(
            const std::shared_ptr<NodeContent>& root, ContentImporterContext& context)
        {
            const std::shared_ptr<Graphics::BoneContent> bone = FirstBone(root);
            if (bone == nullptr)
            {
                return root;
            }
            // A second skeleton is warned about rather than merged, and XNA's own text leaves the
            // two names unformatted -- `{0}` and `{1}` reach the log verbatim, which is what the
            // genuine importer answers for `fbx_bone_first_only.fbx`.
            std::vector<std::string> skeletons;
            CountSkeletons(root, false, skeletons);
            if (skeletons.size() > 1u)
            {
                context.getLoggerProperty().LogWarning(
                    std::string(), ContentIdentity(),
                    "Multiple skeletons were found in the file. The first skeleton, named "
                    "\"{0}\" has been moved to be a child of the scene root. The other, "
                    "\"{1}\", will be ignored.");
            }
            const Matrix absolute = bone->getAbsoluteTransformProperty();
            const Matrix into = Matrix::Invert(root->getAbsoluteTransformProperty());
            if (bone != root)
            {
                if (NodeContent* parent = bone->getParentProperty(); parent != nullptr)
                {
                    parent->getChildrenProperty().Remove(bone);
                }
                root->getChildrenProperty().Add(bone);
            }
            bone->setTransformProperty(absolute * into);
            return root;
        }

        /** @brief One `Properties60`/`Properties70` entry, by its name. */
        [[nodiscard]] const Canon::FbxNode* FindProperty(const Canon::FbxNode& object,
                                                         const std::string& name)
        {
            for (const std::string& block : {"Properties70", "Properties60"})
            {
                if (const Canon::FbxNode* properties = object.Find(block); properties != nullptr)
                {
                    for (const Canon::FbxNode& property : properties->children)
                    {
                        if (property.Text(0) == name)
                        {
                            return &property;
                        }
                    }
                }
            }
            return nullptr;
        }

        /** @brief The one number a scalar property carries, or a fallback. */
        [[nodiscard]] double PropertyNumber(const Canon::FbxNode& object, const std::string& name,
                                            const double fallback)
        {
            const Canon::FbxNode* property = FindProperty(object, name);
            if (property == nullptr) { return fallback; }
            for (const Canon::FbxProperty& one : property->properties)
            {
                if (const double* value = std::get_if<double>(&one); value != nullptr)
                {
                    return *value;
                }
            }
            return fallback;
        }

        /** @brief The text a string property carries, or a fallback. */
        [[nodiscard]] std::string PropertyText(const Canon::FbxNode& object, const std::string& name,
                                               const std::string& fallback)
        {
            const Canon::FbxNode* property = FindProperty(object, name);
            if (property == nullptr) { return fallback; }
            // The value is the last string of the property row: `Property: "ShadingModel",
            // "KString", "", "Lambert"` names the type and the flags before it.
            std::string found = fallback;
            bool sawName = false;
            for (const Canon::FbxProperty& one : property->properties)
            {
                if (const std::string* value = std::get_if<std::string>(&one); value != nullptr)
                {
                    if (!sawName) { sawName = true; continue; }
                    if (!value->empty()) { found = *value; }
                }
            }
            return found;
        }

        /** @brief The three numbers a transform property carries, or a fallback. */
        [[nodiscard]] Vector3 PropertyVector(const Canon::FbxNode& object, const std::string& name,
                                             const Vector3 fallback)
        {
            const Canon::FbxNode* property = FindProperty(object, name);
            if (property == nullptr)
            {
                return fallback;
            }
            std::vector<double> numbers;
            for (const Canon::FbxProperty& one : property->properties)
            {
                if (const double* value = std::get_if<double>(&one); value != nullptr)
                {
                    numbers.push_back(*value);
                }
            }
            if (numbers.size() < 3u)
            {
                return fallback;
            }
            return Vector3(static_cast<float>(numbers[numbers.size() - 3u]),
                           static_cast<float>(numbers[numbers.size() - 2u]),
                           static_cast<float>(numbers[numbers.size() - 1u]));
        }

        /** @brief The same property, kept in the `double` the file writes it in. */
        [[nodiscard]] Triple PropertyTriple(const Canon::FbxNode& object, const std::string& name,
                                            const Triple fallback)
        {
            const Canon::FbxNode* property = FindProperty(object, name);
            if (property == nullptr)
            {
                return fallback;
            }
            std::vector<double> numbers;
            for (const Canon::FbxProperty& one : property->properties)
            {
                if (const double* value = std::get_if<double>(&one); value != nullptr)
                {
                    numbers.push_back(*value);
                }
            }
            if (numbers.size() < 3u)
            {
                return fallback;
            }
            return Triple{numbers[numbers.size() - 3u], numbers[numbers.size() - 2u],
                          numbers[numbers.size() - 1u]};
        }

        /**
         * @brief One node's local transform: scaling, then `PreRotation`, then `Lcl Rotation`.
         *
         * `PreRotation` is part of FBX's own transform formula and every model an exporter writes
         * from a Z-up tool carries one: the public XNA sample corpus's `Cone.fbx`, `Handgun.FBX`
         * and `AircraftCarrier.FBX` all name (-90, 0, 0) on their top-level nodes, and the genuine
         * importer answers a basis of `[1 0 0][0 0 -1][0 1 0]` for exactly that. Leaving it out
         * gave every such model a near-identity transform and stood it on the wrong axis
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-113`).
         *
         * @param object The FBX object.
         * @return The local transform.
         */
        /** @brief A 4x4 in double, row-vector convention, the way the genuine importer composes. */
        using Rows = std::array<std::array<double, 4>, 4>;

        /** @brief The identity. */
        [[nodiscard]] Rows IdentityRows()
        {
            return Rows{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
        }

        /** @brief `left` times `right`, row-vector order. */
        [[nodiscard]] Rows Multiply(const Rows& left, const Rows& right)
        {
            Rows product{};
            for (std::size_t row = 0; row < 4; ++row)
            {
                for (std::size_t column = 0; column < 4; ++column)
                {
                    double sum = 0.0;
                    for (std::size_t k = 0; k < 4; ++k) { sum += left[row][k] * right[k][column]; }
                    product[row][column] = sum;
                }
            }
            return product;
        }

        /**
         * @brief The three axis rotations XNA's own `Matrix::CreateRotation*` write, in double.
         *
         * Composed in double and narrowed once at the end, because the genuine importer's is. A
         * quarter turn is the case that shows it: `cos` of a float pi/2 is -4.371e-08, of a double
         * pi/2 it is 6.123e-17, and `Cube.fbx`'s `PreRotation -90` reaches XNA's own `Cube.xnb` as
         * 2.54 * 6.123e-17 = 1.5553e-16 -- so a float rotation lands seven orders of magnitude away
         * from XNA's on the two entries a quarter turn zeroes
         * (plans/plan_xna_sample_xnb_sweep.md XNASWEEP-121).
         */
        [[nodiscard]] Rows EulerRows(const Triple& degrees)
        {
            const double toRadians = 0.017453292519943295;
            const auto rotationX = [](double radians)
            {
                Rows m = IdentityRows();
                m[1][1] = std::cos(radians);
                m[1][2] = std::sin(radians);
                m[2][1] = -m[1][2];
                m[2][2] = m[1][1];
                return m;
            };
            const auto rotationY = [](double radians)
            {
                Rows m = IdentityRows();
                m[0][0] = std::cos(radians);
                m[0][2] = -std::sin(radians);
                m[2][0] = -m[0][2];
                m[2][2] = m[0][0];
                return m;
            };
            const auto rotationZ = [](double radians)
            {
                Rows m = IdentityRows();
                m[0][0] = std::cos(radians);
                m[0][1] = std::sin(radians);
                m[1][0] = -m[0][1];
                m[1][1] = m[0][0];
                return m;
            };
            return Multiply(Multiply(rotationX(degrees[0] * toRadians),
                                     rotationY(degrees[1] * toRadians)),
                            rotationZ(degrees[2] * toRadians));
        }

        /** @brief A scaling. */
        [[nodiscard]] Rows ScaleRows(const Triple& scale)
        {
            Rows m = IdentityRows();
            m[0][0] = scale[0];
            m[1][1] = scale[1];
            m[2][2] = scale[2];
            return m;
        }

        /** @brief A translation, in the last row. */
        [[nodiscard]] Rows TranslationRows(const Triple& offset)
        {
            Rows m = IdentityRows();
            m[3][0] = offset[0];
            m[3][1] = offset[1];
            m[3][2] = offset[2];
            return m;
        }

        /**
         * @brief The geometry's own offset from its node: `GeometricScaling`, then
         *        `GeometricRotation`, then `GeometricTranslation`.
         *
         * FBX calls this the geometric transform, and it belongs to the geometry rather than to the
         * node: a child does not inherit it. The genuine importer folds it into the node's own
         * `Transform` and undoes it again on every child, which is what `LocalTransform` does with
         * the inverse (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-146`).
         */
        [[nodiscard]] Rows GeometricRows(const Object& object)
        {
            return Multiply(Multiply(ScaleRows(object.geometricScaling),
                                     EulerRows(object.geometricRotation)),
                            TranslationRows(object.geometricTranslation));
        }

        /** @brief `GeometricRows` undone: `T(-t)`, then the rotation transposed, then `1/s`. */
        [[nodiscard]] Rows InverseGeometricRows(const Object& object)
        {
            const Triple& scale = object.geometricScaling;
            const Triple reciprocal{scale[0] == 0.0 ? 0.0 : 1.0 / scale[0],
                                    scale[1] == 0.0 ? 0.0 : 1.0 / scale[1],
                                    scale[2] == 0.0 ? 0.0 : 1.0 / scale[2]};
            const Rows rotation = EulerRows(object.geometricRotation);
            Rows transposed = IdentityRows();
            for (std::size_t row = 0; row < 3; ++row)
            {
                for (std::size_t column = 0; column < 3; ++column)
                {
                    transposed[row][column] = rotation[column][row];
                }
            }
            const Triple back{-object.geometricTranslation[0], -object.geometricTranslation[1],
                              -object.geometricTranslation[2]};
            return Multiply(Multiply(TranslationRows(back), transposed), ScaleRows(reciprocal));
        }

        /** @brief Narrow a composed row matrix to the XNA one, once, at the end. */
        [[nodiscard]] Matrix ToMatrix(const Rows& composed)
        {
            Matrix result;
            result.M11 = static_cast<float>(composed[0][0]);
            result.M12 = static_cast<float>(composed[0][1]);
            result.M13 = static_cast<float>(composed[0][2]);
            result.M14 = static_cast<float>(composed[0][3]);
            result.M21 = static_cast<float>(composed[1][0]);
            result.M22 = static_cast<float>(composed[1][1]);
            result.M23 = static_cast<float>(composed[1][2]);
            result.M24 = static_cast<float>(composed[1][3]);
            result.M31 = static_cast<float>(composed[2][0]);
            result.M32 = static_cast<float>(composed[2][1]);
            result.M33 = static_cast<float>(composed[2][2]);
            result.M34 = static_cast<float>(composed[2][3]);
            result.M41 = static_cast<float>(composed[3][0]);
            result.M42 = static_cast<float>(composed[3][1]);
            result.M43 = static_cast<float>(composed[3][2]);
            result.M44 = static_cast<float>(composed[3][3]);
            return result;
        }

        /**
         * @brief One node's local transform: the geometry's offset, then scaling, `PreRotation`,
         *        `Lcl Rotation` and the translation, with the parent's geometric offset undone.
         *
         * `PreRotation` counts only where `RotationActive` is set, which is FBX's own rule and
         * measurable: `fbx_prerotation_units.fbx` sets it and the quarter turn is in XNA's answer,
         * `fbx_geometric_offset.fbx` does not and the same `PreRotation -90` is not
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-146`).
         */
        [[nodiscard]] Matrix LocalTransform(const Object& object, const Rows& parentGeometricInverse)
        {
            // FBX's own transform formula, all ten terms of it. Written the way a row vector meets
            // them, which is the reverse of the order the SDK's documentation lists:
            //
            //   Sp^-1 . S . Sp . Soff . Rp^-1 . Rpost^-1 . R . Rpre . Rp . Roff . T
            //
            // A scaling, a rotation and a translation reach four of those ten, which is all CNA
            // composed. SAMPLE-138's `photograph.fbx` needs the other six: its mesh carries a
            // `ScalingPivot` of about -(1.06, 6.89, 6.43) against a `ScalingOffset` that nearly
            // cancels it, and XNA's own build holds what is left over -- (2.35e-06, 1.53e-05,
            // -1.42e-05) -- where CNA held a clean zero. Measured on `fbx_pivots.fbx` and
            // `fbx_postrotation.fbx` (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-152`); the
            // second of those also settles that `R` comes *before* `Rpre` here, which no fixture
            // that sets only one of them could say.
            const Triple negate[4] = {
                {-object.scalingPivot[0], -object.scalingPivot[1], -object.scalingPivot[2]},
                {-object.rotationPivot[0], -object.rotationPivot[1], -object.rotationPivot[2]},
                {-object.postRotation[0], -object.postRotation[1], -object.postRotation[2]},
                {0.0, 0.0, 0.0}};
            const Rows rotations = Multiply(
                Multiply(object.rotationActive ? EulerRows(negate[2]) : IdentityRows(),
                         EulerRows(object.rotation)),
                object.rotationActive ? EulerRows(object.preRotation) : IdentityRows());
            const Rows local = Multiply(
                Multiply(Multiply(Multiply(TranslationRows(negate[0]),
                                           Multiply(ScaleRows(object.scaling),
                                                    TranslationRows(object.scalingPivot))),
                                  Multiply(TranslationRows(object.scalingOffset),
                                           TranslationRows(negate[1]))),
                         Multiply(rotations, Multiply(TranslationRows(object.rotationPivot),
                                                      TranslationRows(object.rotationOffset)))),
                TranslationRows(object.translation));
            const Rows placed =
                Multiply(Multiply(GeometricRows(object), local), parentGeometricInverse);
            // The scene's unit multiplies the whole composed transform -- basis and translation
            // both -- and it reaches only the nodes the scene connects.
            return ToMatrix(object.unitScale == 1.0
                                ? placed
                                : Multiply(placed, ScaleRows(Triple{object.unitScale,
                                                                    object.unitScale,
                                                                    object.unitScale})));
        }

        /** @brief Whatever a layer element holds, resolved through its mapping and reference. */
        struct Layer
        {
            std::vector<double> values;
            std::vector<int> indices;
            std::string mapping;
            std::string reference;
            std::size_t stride = 0u;

            /** @brief The value tuple for a polygon vertex, or an empty vector when there is none. */
            [[nodiscard]] std::vector<double> At(const std::size_t polygonVertex,
                                                 const std::size_t controlPoint,
                                                 const std::size_t polygon) const
            {
                if (stride == 0u || values.empty())
                {
                    return {};
                }
                std::size_t index = polygonVertex;
                if (mapping == "ByVertice" || mapping == "ByVertex" || mapping == "ByControlPoint")
                {
                    index = controlPoint;
                }
                else if (mapping == "ByPolygon")
                {
                    index = polygon;
                }
                else if (mapping == "AllSame")
                {
                    index = 0u;
                }
                if (reference == "IndexToDirect" || reference == "Index")
                {
                    if (index >= indices.size() || indices[index] < 0)
                    {
                        return {};
                    }
                    index = static_cast<std::size_t>(indices[index]);
                }
                const std::size_t at = index * stride;
                if (at + stride > values.size())
                {
                    return {};
                }
                return std::vector<double>(values.begin() + static_cast<std::ptrdiff_t>(at),
                                           values.begin() + static_cast<std::ptrdiff_t>(at + stride));
            }
        };

        /** @brief The layer element of a name and a `TypedIndex`, or the first of that name. */
        [[nodiscard]] const Canon::FbxNode* FindLayerElement(const Canon::FbxNode& mesh,
                                                             const std::string& element,
                                                             const std::size_t typedIndex)
        {
            const Canon::FbxNode* first = nullptr;
            for (const Canon::FbxNode& child : mesh.children)
            {
                if (child.name != element)
                {
                    continue;
                }
                if (first == nullptr)
                {
                    first = &child;
                }
                if (static_cast<std::size_t>(std::max(0.0, child.Number(0, 0.0))) == typedIndex)
                {
                    return &child;
                }
            }
            return first;
        }

        [[nodiscard]] Layer ReadLayerNode(const Canon::FbxNode* node,
                                          const std::string& valuesName,
                                          const std::string& indexName, const std::size_t stride)
        {
            Layer layer;
            if (node == nullptr)
            {
                return layer;
            }
            layer.stride = stride;
            layer.mapping = node->Find("MappingInformationType") != nullptr
                                ? node->Find("MappingInformationType")->Text(0)
                                : std::string("ByVertice");
            layer.reference = node->Find("ReferenceInformationType") != nullptr
                                  ? node->Find("ReferenceInformationType")->Text(0)
                                  : std::string("Direct");
            if (const Canon::FbxNode* values = node->Find(valuesName); values != nullptr)
            {
                layer.values = values->Numbers();
            }
            if (!indexName.empty())
            {
                if (const Canon::FbxNode* indices = node->Find(indexName); indices != nullptr)
                {
                    for (const double one : indices->Numbers())
                    {
                        layer.indices.push_back(static_cast<int>(one));
                    }
                }
            }
            return layer;
        }

        [[nodiscard]] Layer ReadLayer(const Canon::FbxNode& mesh, const std::string& element,
                                      const std::string& valuesName, const std::string& indexName,
                                      const std::size_t stride)
        {
            return ReadLayerNode(mesh.Find(element), valuesName, indexName, stride);
        }
    }

    std::shared_ptr<Graphics::NodeContent> FbxImporter::Import(const std::string& filename,
                                                               ContentImporterContext& context)
    {
        std::error_code error;
        if (!std::filesystem::exists(filename, error) || error)
        {
            throw System::IO::FileNotFoundException("Cannot import the specified mesh. The file \"" +
                                                    filename + "\" could not be found.");
        }
        std::vector<std::uint8_t> bytes;
        {
            std::ifstream file(filename, std::ios::binary);
            const std::vector<char> read((std::istreambuf_iterator<char>(file)),
                                         std::istreambuf_iterator<char>());
            bytes.assign(read.begin(), read.end());
        }
        Canon::FbxFile parsed;
        try
        {
            parsed = Canon::ReadFbxFile(bytes);
        }
        catch (const Canon::FbxFileException& failure)
        {
            switch (failure.Error())
            {
                case Canon::FbxFileError::CannotInitialize:
                    throw InvalidContentException(CannotInitialize());
                case Canon::FbxFileError::NotFbx:
                    throw InvalidContentException(NotAnFbx());
                case Canon::FbxFileError::Unsupported:
                    throw InvalidContentException(std::string(failure.what()));
                case Canon::FbxFileError::ParseError:
                    break;
            }
            throw InvalidContentException(CannotImport());
        }

        // Gather the objects, then the connections that make them a tree. FBX 6 names an object by
        // its `Model::Name` string; FBX 7 gives it a 64-bit identity and connects by that, so both
        // are keyed the same way here -- by identity where there is one, by name otherwise.
        std::map<std::int64_t, Object> objects;
        // An FBX 6 file has no identities, so a connection names an object by the string it was
        // declared under -- `"Model::TableTop"`, prefix and all. The prefix is part of the name and
        // not decoration: SAMPLE-047's `table.FBX` declares `Model::TableTop` and
        // `Material::TableTop`, and keying on the bare `TableTop` lets the material answer for the
        // model, which loses the model from the scene entirely
        // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-145`). The bare name stays as a fallback
        // for a file that connects without the prefix, first declaration winning so a later object
        // cannot shadow an earlier one.
        std::map<std::string, std::int64_t> byFullName;
        std::map<std::string, std::int64_t> byName;
        // Objects are keyed by a descending synthetic identity, so the map's own order is the
        // reverse of the file's. The declaration order is the fallback for a file whose objects no
        // `Connect` line names at all.
        std::vector<std::int64_t> declarationOrder;
        // The order the `Connect` lines put objects in the scene, which is the one XNA answers.
        // SAMPLE-142's `France.FBX` separates the two: it declares `Sky` before `Object04` and
        // connects `Object04` to the scene first, and XNA's own build answers `Object04` as the
        // first bone under the root (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-154`).
        std::vector<std::int64_t> sceneOrder;
        std::int64_t nextSynthetic = -1;
        if (const Canon::FbxNode* block = parsed.Find("Objects"); block != nullptr)
        {
            for (const Canon::FbxNode& node : block->children)
            {
                if (node.name != "Model" && node.name != "Geometry" && node.name != "Material" &&
                    node.name != "Texture")
                {
                    continue;
                }
                Object object;
                object.node = &node;
                std::int64_t identity = 0;
                if (parsed.version >= 7000u && node.properties.size() >= 3u)
                {
                    identity = static_cast<std::int64_t>(node.Number(0));
                    object.name = BareName(node.Text(1));
                    object.kind = node.Text(2);
                }
                else
                {
                    object.name = BareName(node.Text(0));
                    object.kind = node.Text(1);
                    identity = nextSynthetic--;
                }
                object.isGeometryData = node.name == "Geometry";
                if (node.name == "Material")
                {
                    object.kind = "Material";
                }
                if (node.name == "Texture")
                {
                    object.kind = "Texture";
                }
                object.translation = PropertyTriple(node, "Lcl Translation", Triple{0.0, 0.0, 0.0});
                object.rotation = PropertyTriple(node, "Lcl Rotation", Triple{0.0, 0.0, 0.0});
                object.preRotation = PropertyTriple(node, "PreRotation", Triple{0.0, 0.0, 0.0});
                object.postRotation = PropertyTriple(node, "PostRotation", Triple{0.0, 0.0, 0.0});
                object.rotationOffset =
                    PropertyTriple(node, "RotationOffset", Triple{0.0, 0.0, 0.0});
                object.rotationPivot =
                    PropertyTriple(node, "RotationPivot", Triple{0.0, 0.0, 0.0});
                object.scalingOffset =
                    PropertyTriple(node, "ScalingOffset", Triple{0.0, 0.0, 0.0});
                object.scalingPivot =
                    PropertyTriple(node, "ScalingPivot", Triple{0.0, 0.0, 0.0});
                object.rotationActive = PropertyNumber(node, "RotationActive", 0.0) != 0.0;
                object.geometricTranslation =
                    PropertyTriple(node, "GeometricTranslation", Triple{0.0, 0.0, 0.0});
                object.geometricRotation =
                    PropertyTriple(node, "GeometricRotation", Triple{0.0, 0.0, 0.0});
                object.geometricScaling =
                    PropertyTriple(node, "GeometricScaling", Triple{1.0, 1.0, 1.0});
                object.scaling = PropertyTriple(node, "Lcl Scaling", Triple{1.0, 1.0, 1.0});
                byFullName[node.Text(0)] = identity;
                byName.emplace(object.name, identity);
                declarationOrder.push_back(identity);
                objects.emplace(identity, std::move(object));
            }
        }
        if (const Canon::FbxNode* block = parsed.Find("Connections"); block != nullptr)
        {
            for (const Canon::FbxNode& connection : block->children)
            {
                std::int64_t child = 0;
                std::int64_t parent = 0;
                if (parsed.version >= 7000u)
                {
                    child = static_cast<std::int64_t>(connection.Number(1));
                    parent = static_cast<std::int64_t>(connection.Number(2));
                }
                else
                {
                    const auto resolve = [&byFullName, &byName](const std::string& text) {
                        if (const auto full = byFullName.find(text); full != byFullName.end())
                        {
                            return full->second;
                        }
                        const auto bare = byName.find(BareName(text));
                        return bare == byName.end() ? static_cast<std::int64_t>(0) : bare->second;
                    };
                    child = resolve(connection.Text(1));
                    parent = resolve(connection.Text(2));
                }
                const auto childObject = objects.find(child);
                if (childObject == objects.end())
                {
                    continue;
                }
                const auto parentObject = objects.find(parent);
                if (parentObject == objects.end())
                {
                    // Connected to the scene root, which is not one of the objects. An object no
                    // `Connect` names at all is in the file but not in the scene, and the two are
                    // not the same thing: an exporter writes the producer cameras as objects and
                    // connects none of them, which is why `Cone.fbx` answers its mesh as the root
                    // where `fbx_cameras.fbx`, whose cameras *are* connected, answers a
                    // `RootNode` (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-111`).
                    if (!childObject->second.inScene) { sceneOrder.push_back(child); }
                    childObject->second.inScene = true;
                    continue;
                }
                if (childObject->second.kind == "Material")
                {
                    parentObject->second.materials.push_back(child);
                    childObject->second.attached = true;
                    continue;
                }
                if (childObject->second.kind == "Texture")
                {
                    // A texture hangs off the *model*, beside the material, not off the material
                    // (measured on SAMPLE-030's `tank.fbx`, whose connections read
                    // `Texture::steamroller_tank61_file3 -> Model::r_engine_geo`; the genuine
                    // importer answers `materialTexture Texture=engine_diff_tex.tga` on that
                    // model's material). A repeat is dropped: an exporter names the same texture
                    // once per layer element (plans/plan_xna_sample_xnb_sweep.md XNASWEEP-127).
                    std::vector<std::int64_t>& list = parentObject->second.textures;
                    if (std::find(list.begin(), list.end(), child) == list.end())
                    {
                        list.push_back(child);
                    }
                    childObject->second.attached = true;
                    continue;
                }
                parentObject->second.children.push_back(child);
                childObject->second.attached = true;
            }
        }

        // The materials, built once and shared by whatever names them.
        std::map<std::int64_t, std::shared_ptr<BasicMaterialContent>> materials;
        for (const auto& [identity, object] : objects)
        {
            if (object.kind != "Material" || object.node == nullptr)
            {
                continue;
            }
            auto material = std::make_shared<BasicMaterialContent>();
            material->setNameProperty(object.name);
            // The measured answer is the SDK's defaults for everything but the diffuse colour: an
            // Opacity of 0.5 and a Shininess of 2 both come back as 1 and 20, so those two are not
            // read from a 6.1 material at all.
            //
            // A colour is its `<Name>Color` times its `<Name>Factor`, which is how the SDK's own
            // `Diffuse`, `Specular` and `Emissive` compatibility properties are written beside
            // them. SAMPLE-030's `tank.fbx` is the case that shows it: `DiffuseColor` is (1,1,1)
            // with a `DiffuseFactor` of 0.8, the file's own `Diffuse` is (0.8,0.8,0.8), and the
            // genuine importer answers 0.8 (plans/plan_xna_sample_xnb_sweep.md XNASWEEP-127).
            //
            // Two property tables exist and a material carries one or the other. The newer one
            // names `DiffuseColor` beside a `DiffuseFactor`, and its shininess is
            // `ShininessExponent`; the older one names a bare `Diffuse` and its shininess is
            // `Shininess`. Which table a file uses is what decides, not which properties happen to
            // be present: `fbx_two_materials.fbx` is a newer material that *also* carries
            // `Shininess: 2`, and the genuine importer answers 20 for it -- the newer table's
            // default -- while SAMPLE-033's `Ship.fbx`, an older one, answers its `Shininess` of
            // 29.54 exactly.
            const bool newTable = FindProperty(*object.node, "DiffuseColor") != nullptr;
            const auto colour = [&object, newTable](const std::string& name)
            {
                if (!newTable)
                {
                    return PropertyVector(*object.node, name, Vector3(0.0f, 0.0f, 0.0f));
                }
                const Vector3 base = PropertyVector(*object.node, name + "Color",
                                                    Vector3(0.0f, 0.0f, 0.0f));
                const auto factor =
                    static_cast<float>(PropertyNumber(*object.node, name + "Factor", 1.0));
                return Vector3(base.X * factor, base.Y * factor, base.Z * factor);
            };
            material->setDiffuseColorProperty(colour("Diffuse"));
            material->setEmissiveColorProperty(colour("Emissive"));
            material->setAlphaProperty(1.0f);
            material->setSpecularColorProperty(colour("Specular"));
            // A Lambert material has no specular power at all, and XNA leaves it unset rather
            // than defaulting it: the genuine importer's answer for SAMPLE-035's `SphereLowPoly.fbx`
            // -- `ShadingModel: "lambert"`, no shininess property of either spelling -- carries a
            // diffuse, an emissive, an alpha and a specular *colour* and no `SpecularPower`, and
            // the model XNA builds from it holds the `BasicEffect` default of 16 where CNA wrote
            // 20. Every material measured that does answer one is a Phong
            // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-138`).
            const std::string shading = PropertyText(*object.node, "ShadingModel", "");
            bool lambert = shading.size() == 7u;
            for (std::size_t at = 0u; at < shading.size() && lambert; ++at)
            {
                lambert = static_cast<char>(std::tolower(static_cast<unsigned char>(shading[at]))) ==
                          "lambert"[at];
            }
            if (!lambert)
            {
                material->setSpecularPowerProperty(static_cast<float>(PropertyNumber(
                    *object.node, newTable ? "ShininessExponent" : "Shininess", 20.0)));
            }
            materials.emplace(identity, std::move(material));
        }

        // A model's texture, attached to the material that model uses. The pair is what varies:
        // `tank.fbx` shares `engine_phong` across nine models and every one of them names
        // `engine_diff_tex.tga`, so one instance per (material, texture) is both what XNA writes --
        // two effects for twelve meshes -- and what keeps two models that name different textures
        // apart (plans/plan_xna_sample_xnb_sweep.md XNASWEEP-127).
        const std::filesystem::path sourceDirectory = std::filesystem::path(filename).parent_path();
        // A `Texture` names its file twice and the two do not have to agree. The SDK resolves the
        // absolute `FileName` first and falls back to `RelativeFilename` when that names nothing,
        // and the reference XNA writes is whichever one *resolved*. Three files in the sample
        // corpus settle it, and they disagree with each other: Spacewar's `bfg_proj.fbx` names
        // `../textures/bfg_proj.tga` and `../texture/bfg_proj.tga` -- one letter apart, and only
        // the first is a directory that exists -- and XNA's build writes `..\textures\bfg_proj_0`;
        // `p1_rocket_proj.fbx` names `../textures/p1_rocket.tga` against
        // `../../textures/player_1_weapons/p1_rocket.tga`, and XNA writes the first; and
        // SAMPLE-005's `saucer.fbx` names `saucer_p1_diff_v1.tga` against `saucer_texture.tga`,
        // where only the *second* is beside it, and XNA writes `saucer_texture_0`. Preferring
        // either field outright is wrong for one of the three; preferring the one that exists is
        // right for all three (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-140`). A path is
        // matched the way Windows matches one, which `XNASWEEP-115` already needed.
        const auto textureFile = [&objects, &sourceDirectory](const std::int64_t identity) -> std::string
        {
            const auto found = objects.find(identity);
            if (found == objects.end() || found->second.node == nullptr) { return {}; }
            std::vector<std::string> spellings;
            for (const char* field : {"FileName", "Filename", "RelativeFilename"})
            {
                const Canon::FbxNode* named = found->second.node->Find(field);
                if (named == nullptr) { continue; }
                std::string text = named->Text(0);
                if (!text.empty() &&
                    std::find(spellings.begin(), spellings.end(), text) == spellings.end())
                {
                    spellings.push_back(std::move(text));
                }
            }
            for (const std::string& spelling : spellings)
            {
                std::error_code error;
                if (std::filesystem::is_regular_file(
                        ResolveNamedSourceFileEXT(sourceDirectory, spelling), error))
                {
                    return spelling;
                }
            }
            // None of them names a file that is here. The reference is still written -- XNA does
            // not require the texture to exist to record it -- and the spelling kept is the one
            // this importer has always kept.
            for (const char* field : {"RelativeFilename", "FileName", "Filename"})
            {
                const Canon::FbxNode* named = found->second.node->Find(field);
                if (named != nullptr)
                {
                    std::string text = named->Text(0);
                    if (!text.empty()) { return text; }
                }
            }
            return {};
        };
        std::map<std::string, std::shared_ptr<BasicMaterialContent>> textured;
        // `channels` is (dictionary key, texture object identity), in XNA's own channel order; the
        // first entry, when it is `Texture`, is what `BasicMaterialContent.Texture` answers.
        const auto withTexture =
            [&](const std::shared_ptr<BasicMaterialContent>& base, const std::int64_t materialIdentity,
                const std::vector<std::pair<std::string, std::int64_t>>& channels)
            -> std::shared_ptr<BasicMaterialContent>
        {
            std::vector<std::pair<std::string, std::string>> resolved;
            for (const auto& [name, identity] : channels)
            {
                if (identity == 0) { continue; }
                std::string named = textureFile(identity);
                if (named.empty()) { continue; }
                // Spelled the way the tool that wrote the file spells a path, as the `.x` route does.
                std::replace(named.begin(), named.end(), '\\', '/');
                resolved.emplace_back(name, (sourceDirectory / named).lexically_normal().string());
            }
            if (resolved.empty()) { return base; }
            std::string key = std::to_string(materialIdentity);
            for (const auto& [name, path] : resolved) { key += "\x1f" + name + "\x1f" + path; }
            const auto found = textured.find(key);
            if (found != textured.end()) { return found->second; }
            auto copy = std::make_shared<BasicMaterialContent>();
            copy->setNameProperty(base->getNameProperty());
            copy->setDiffuseColorProperty(base->getDiffuseColorProperty().value_or(Vector3(0, 0, 0)));
            copy->setEmissiveColorProperty(base->getEmissiveColorProperty().value_or(Vector3(0, 0, 0)));
            // Set in the order the material was first filled: the description a game or an oracle
            // reads back is the dictionary's insertion order, and Alpha comes before the specular
            // pair (measured, fbx/fbx_material_factor_texture.fbx).
            copy->setAlphaProperty(base->getAlphaProperty().value_or(1.0f));
            copy->setSpecularColorProperty(base->getSpecularColorProperty().value_or(Vector3(0, 0, 0)));
            copy->setSpecularPowerProperty(base->getSpecularPowerProperty().value_or(0.0f));
            for (const auto& [name, path] : resolved)
            {
                if (name == "Texture")
                {
                    copy->setTextureProperty(
                        std::make_shared<ExternalReference<Graphics::TextureContent>>(path));
                }
                else
                {
                    copy->getTexturesProperty().Add(
                        name, std::make_shared<ExternalReference<Graphics::TextureContent>>(path));
                }
            }
            textured.emplace(key, copy);
            return copy;
        };

        const auto build = [&](const std::int64_t identity, const Rows& parentGeometricInverse,
                               auto&& self) -> std::shared_ptr<NodeContent>
        {
            const Object& object = objects.at(identity);
            std::shared_ptr<NodeContent> node;
            const Canon::FbxNode* geometry = object.node;
            for (const std::int64_t child : object.children)
            {
                const auto found = objects.find(child);
                if (found != objects.end() && found->second.isGeometryData &&
                    found->second.node != nullptr)
                {
                    geometry = found->second.node;
                    break;
                }
            }
            const bool isMesh = object.kind == "Mesh" && geometry != nullptr &&
                                geometry->Find("Vertices") != nullptr;
            if (isMesh)
            {
                auto mesh = std::make_shared<MeshContent>();
                const std::vector<double> flat = geometry->Find("Vertices")->Numbers();
                for (std::size_t i = 0; i + 2u < flat.size(); i += 3u)
                {
                    // FBX is right-handed already: nothing is converted (measured, fbx_oblique).
                    mesh->getPositionsProperty().Add(Vector3(static_cast<float>(flat[i]),
                                                             static_cast<float>(flat[i + 1u]),
                                                             static_cast<float>(flat[i + 2u])));
                }
                std::vector<int> polygonIndices;
                if (const Canon::FbxNode* indices = geometry->Find("PolygonVertexIndex");
                    indices != nullptr)
                {
                    for (const double one : indices->Numbers())
                    {
                        polygonIndices.push_back(static_cast<int>(one));
                    }
                }
                const Layer normals = ReadLayer(*geometry, "LayerElementNormal", "Normals",
                                                "NormalsIndex", 3u);
                // Every UV set the mesh declares, and the channel index each one takes.
                //
                // A UV set is a `LayerElementUV` -- or a `LayerElementReflectionUV`, which is what
                // an older Maya exporter writes and which the SDK reads as one too. SAMPLE-131's
                // `p1_piece.fbx` declares only the second, and XNA's build carries a
                // `TextureCoordinate0` for it where CNA carried none: a 32-byte vertex against
                // CNA's 24, and 237 vertices against 194 because the missing channel merged
                // corners XNA keeps apart. Its sibling `p1_piece_tile.fbx` declares *both*, in one
                // `Layer` block, and XNA answers `TextureCoordinate0` and `TextureCoordinate1`.
                //
                // The index is the **`Layer` block's own number**, and where two sets share a
                // block the second takes the next free index. SAMPLE-035's `SphereHighPoly.fbx` is
                // what says the first half: its single UV set is declared in `Layer: 1` rather
                // than `Layer: 0`, and XNA's vertex declaration carries `TextureCoordinate`
                // usage index **1** (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-143`).
                struct UvSet
                {
                    Layer layer;
                    std::size_t index = 0u;
                };
                // Every `LayerElement` type some `Layer` block of this mesh names. Both the UV
                // sets and the material's texture channels turn on it: a layer element the
                // geometry declares but no `Layer` names is not read at all.
                std::set<std::string> namedElements;
                std::vector<UvSet> uvSets;
                {
                    struct Declared
                    {
                        std::size_t layer = 0u;
                        std::string element;
                        std::size_t typedIndex = 0u;
                    };
                    std::vector<Declared> declared;
                    bool named = false;
                    namedElements.clear();
                    for (const Canon::FbxNode& child : geometry->children)
                    {
                        if (child.name != "Layer") { continue; }
                        const std::size_t layerNumber =
                            child.properties.empty()
                                ? 0u
                                : static_cast<std::size_t>(std::max(0.0, child.Number(0, 0.0)));
                        // A layer's own entries, so that "does this layer name a texture element"
                        // is asked of the layer and not of the file.
                        std::vector<Declared> here;
                        bool hasTexture = false;
                        for (const Canon::FbxNode& element : child.children)
                        {
                            if (element.name != "LayerElement") { continue; }
                            const Canon::FbxNode* type = element.Find("Type");
                            if (type == nullptr) { continue; }
                            const std::string spelled = type->Text(0);
                            namedElements.insert(spelled);
                            const std::size_t typedIndex = element.Find("TypedIndex") != nullptr
                                ? static_cast<std::size_t>(std::max(
                                      0.0, element.Find("TypedIndex")->Number(0, 0.0)))
                                : 0u;
                            if (spelled.size() > 2u && spelled.compare(spelled.size() - 2u, 2u, "UV") == 0)
                            {
                                named = true;
                                // The same UV element type twice in one layer is one channel, and
                                // the last entry wins (measured, `uv2d_two_transpuv`).
                                const auto same = std::find_if(here.begin(), here.end(),
                                    [&spelled](const Declared& row) { return row.element == spelled; });
                                if (same != here.end())
                                {
                                    same->typedIndex = typedIndex;
                                }
                                else
                                {
                                    here.push_back(Declared{layerNumber, spelled, typedIndex});
                                }
                            }
                            else if (spelled.find("Texture") != std::string::npos)
                            {
                                hasTexture = true;
                            }
                        }
                        for (Declared& row : here)
                        {
                            // `LayerElementUV` is always read. A UV set of any other texture
                            // channel -- `LayerElementTransparentUV`, `...SpecularUV`,
                            // `...BumpUV`, `...EmissiveUV`, `...ReflectionUV` -- is read only when
                            // the same `Layer` block also names a texture element, whichever one
                            // and whether or not a texture is connected to the mesh. Measured on
                            // the genuine importer over 29 probes
                            // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-166`): the same file
                            // with the texture entry removed from the `Layer` answers one channel
                            // and with it answers two, and a `LayerElementTexture` naming a
                            // texture that is not connected is enough.
                            if (row.element == "LayerElementUV" || hasTexture)
                            {
                                declared.push_back(row);
                            }
                        }
                    }
                    if (!named)
                    {
                        // No `Layer` block names one: read whichever element is present, at zero.
                        for (const char* element : {"LayerElementUV", "LayerElementReflectionUV"})
                        {
                            Layer read = ReadLayer(*geometry, element, "UV", "UVIndex", 2u);
                            if (read.stride != 0u && !read.values.empty())
                            {
                                uvSets.push_back(UvSet{std::move(read), uvSets.size()});
                            }
                        }
                    }
                    else
                    {
                        std::vector<std::size_t> taken;
                        for (const Declared& row : declared)
                        {
                            Layer read = ReadLayerNode(
                                FindLayerElement(*geometry, row.element, row.typedIndex),
                                "UV", "UVIndex", 2u);
                            if (read.stride == 0u || read.values.empty()) { continue; }
                            std::size_t index = row.layer;
                            while (std::find(taken.begin(), taken.end(), index) != taken.end())
                            {
                                ++index;
                            }
                            taken.push_back(index);
                            uvSets.push_back(UvSet{std::move(read), index});
                        }
                    }
                }
                const Layer& uvs = uvSets.empty() ? normals : uvSets.front().layer;
                (void)uvs;
                const Layer colors = ReadLayer(*geometry, "LayerElementColor", "Colors",
                                               "ColorIndex", 4u);
                // A material layer's `Materials` array IS the per-polygon index, whatever its
                // ReferenceInformationType says; reading it as a value list that then needs a
                // second index array is what leaves every polygon on material zero.
                Layer materialLayer = ReadLayer(*geometry, "LayerElementMaterial", "Materials",
                                                "", 1u);
                materialLayer.reference = "Direct";
                // Which texture a polygon uses, where the mesh says so. `TextureId` is a per
                // polygon index into the textures connected to the model, and -1 is *no texture*
                // -- the layer's `IndexToDirect` names the same array `Materials` does, not a
                // second one (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-141`).
                Layer textureLayer = ReadLayer(*geometry, "LayerElementTexture", "TextureId",
                                               "", 1u);
                if (textureLayer.stride == 0u)
                {
                    textureLayer = ReadLayer(*geometry, "LayerElementReflectionTextures",
                                             "TextureId", "", 1u);
                }
                textureLayer.reference = "Direct";

                // Every texture channel the mesh's `Layer` blocks name, and what the genuine
                // importer calls it in the material's `Textures` dictionary. The order is XNA's
                // own and not the `Layer` block's -- a file naming them in the reverse order comes
                // back in this one -- and three element types the FBX 6 format defines are simply
                // not read: `Emissive`, `Diffuse` and `Displacement` produce nothing. A texture
                // element the geometry declares but no `Layer` names produces nothing either
                // (measured over eleven probes, plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-166`).
                struct TextureChannel
                {
                    const char* element;
                    const char* key;
                };
                //
                // Only the diffuse channel is produced. The others are measured and deliberately
                // not built: on `head.fbx` (SAMPLE-037) the specular channel names a
                // `Head_Spec.TGA` that the sample does not ship, and the genuine build answers one
                // texture -- `Head_Diff_0.xnb` -- and no error, so whatever XNA resolves that
                // channel against, it is narrower than "the texture the polygon's `TextureId`
                // names among those connected to the mesh". Producing it here makes
                // `MaterialProcessor` build a file that is not there. The channel names stay in
                // the table so the next attempt starts from what was measured rather than from
                // nothing.
                static constexpr TextureChannel kTextureChannels[] = {
                    {"LayerElementTexture", "Texture"},
                };
                struct NamedTextureLayer
                {
                    std::string key;
                    Layer layer;
                };
                std::vector<NamedTextureLayer> namedTextures;
                for (const TextureChannel& channel : kTextureChannels)
                {
                    if (geometry->Find(channel.element) == nullptr)
                    {
                        continue;
                    }
                    Layer read = ReadLayer(*geometry, channel.element, "TextureId", "", 1u);
                    read.reference = "Direct";
                    namedTextures.push_back(NamedTextureLayer{channel.key, std::move(read)});
                }
                if (namedTextures.empty() &&
                    geometry->Find("LayerElementReflectionTextures") != nullptr)
                {
                    // An older Maya exporter writes the diffuse channel under this name and no
                    // `LayerElementTexture` at all (SAMPLE-131's `p1_piece.fbx`).
                    Layer read = ReadLayer(*geometry, "LayerElementReflectionTextures", "TextureId",
                                           "", 1u);
                    read.reference = "Direct";
                    namedTextures.push_back(NamedTextureLayer{"Texture", std::move(read)});
                }

                // Walk the polygons, gathering each one's control points and which material it uses.
                struct Polygon
                {
                    std::vector<std::size_t> corners;      // indices into the polygon-vertex stream
                    std::vector<std::size_t> controlPoints;
                    std::size_t material = 0u;
                    int texture = -1;
                };
                std::vector<Polygon> polygons;
                Polygon current;
                for (std::size_t i = 0; i < polygonIndices.size(); ++i)
                {
                    const int raw = polygonIndices[i];
                    const std::size_t controlPoint =
                        static_cast<std::size_t>(raw < 0 ? (-raw - 1) : raw);
                    if (controlPoint >= static_cast<std::size_t>(
                                            mesh->getPositionsProperty().getCountProperty()))
                    {
                        throw InvalidContentException(CannotImport());
                    }
                    current.corners.push_back(i);
                    current.controlPoints.push_back(controlPoint);
                    if (raw < 0)
                    {
                        const std::vector<double> assigned =
                            materialLayer.At(i, controlPoint, polygons.size());
                        current.material = assigned.empty() ? 0u
                                                            : static_cast<std::size_t>(assigned.front());
                        const std::vector<double> textured =
                            textureLayer.At(i, controlPoint, polygons.size());
                        current.texture = textured.empty() ? -1 : static_cast<int>(textured.front());
                        polygons.push_back(std::move(current));
                        current = Polygon{};
                    }
                }

                // A mesh that declares no normals gets them computed: each *triangle's* unit
                // normal, summed onto the three control points it names, normalized once at the
                // end. Per triangle rather than per polygon, because that is what the corpus can
                // tell: `fbx_polygon_concave6.fbx` is a hexagon whose triangulation gives one
                // control point two triangles wound against each other, and XNA answers the zero
                // vector there -- which a polygon normal taken from three corners cannot produce
                // and which no substitute may replace (XNASWEEP-164).
                // Not weighted by area -- `fbx_generated_normals.fbx` and
                // `fbx_generated_normals_area.fbx` are the same fold with one face four times the
                // other's area, and XNA answers `(0, -0.707107, 0.707107)` on the shared edge for
                // both, which is the normalized sum of the two *unit* face normals
                // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-156`). CNA used to write a constant
                // `(0, 0, 1)`, which is right for a mesh in the XY plane and for nothing else.
                std::vector<Vector3> generatedNormals(
                    static_cast<std::size_t>(mesh->getPositionsProperty().getCountProperty()),
                    Vector3(0.0f, 0.0f, 0.0f));
                {
                    const auto& positions = mesh->getPositionsProperty();
                    for (const Polygon& polygon : polygons)
                    {
                        if (polygon.controlPoints.size() < 3u) { continue; }
                        for (const std::array<std::size_t, 3>& triangle :
                             TrianglesOfPolygon(polygon.controlPoints.size()))
                        {
                            const std::size_t p0 = polygon.controlPoints[triangle[0]];
                            const std::size_t p1 = polygon.controlPoints[triangle[1]];
                            const std::size_t p2 = polygon.controlPoints[triangle[2]];
                            const Vector3 a = positions[static_cast<SharpRuntime::intcs>(p0)];
                            const Vector3 b = positions[static_cast<SharpRuntime::intcs>(p1)];
                            const Vector3 c = positions[static_cast<SharpRuntime::intcs>(p2)];
                            // The triangulation is emitted with the winding reversed, and the
                            // generated normal is not: `fbx_polygon*.fbx` are counter-clockwise
                            // rings in the XY plane and XNA answers +Z on every one of them.
                            const Vector3 face = Vector3::Cross(c - a, b - a);
                            const float length = face.Length();
                            if (!(length > 0.0f)) { continue; }
                            const Vector3 unit(face.X / length, face.Y / length, face.Z / length);
                            for (const std::size_t point : {p0, p1, p2})
                            {
                                if (point < generatedNormals.size())
                                {
                                    generatedNormals[point] = generatedNormals[point] + unit;
                                }
                            }
                        }
                    }
                    for (Vector3& normal : generatedNormals)
                    {
                        const float length = normal.Length();
                        if (length > 0.0f)
                        {
                            normal = Vector3(normal.X / length, normal.Y / length, normal.Z / length);
                        }
                    }
                }

                std::vector<std::int64_t> batchMaterials = object.materials;
                if (batchMaterials.empty() || materialLayer.stride == 0u)
                {
                    // A material reaches a batch only through a LayerElementMaterial (measured:
                    // fbx_quad_textured connects one and answers material=null).
                    batchMaterials.clear();
                }
                // A mesh's batches come out in the order its polygons *first name* each material,
                // not in the order the materials are connected. SAMPLE-142's `France.FBX` settles
                // it: `Object04` carries 25 materials and 3,354 polygons over 23 of them, and XNA's
                // own build answers its batches with 840, 28, 28, 252, 56, 81, 179, 863, ...
                // triangles -- which is the first-use order exactly, where the connection order
                // would answer 200, 8, 863, 81, 131, 179, 466, ...
                // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-155`).
                //
                // A polygon whose index names no connected material is *not* dropped: XNA answers
                // every one of them in a single batch carrying no material at all, whatever index
                // each named. `fbx_material_gap.fbx` names 0, 1 and 2 with one material connected
                // and answers two batches -- `Only`, then one null-material batch holding both the
                // 1 and the 2; `fbx_material_gap_negative.fbx` does the same for -1; and
                // `fbx_material_gap_skip.fbx`, whose polygons name only out-of-range indices,
                // answers a single null batch. SAMPLE-138's `photograph.fbx` is the corpus's own
                // case: its `Materials` array names 0, 1 and 2 with two materials connected, and
                // dropping the 86 polygons that named 2 lost a whole mesh part and 154 vertices
                // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-165`).
                constexpr std::size_t NoMaterial = static_cast<std::size_t>(-1);
                const auto batchOf = [&](const Polygon& polygon) -> std::size_t
                {
                    if (batchMaterials.empty())
                    {
                        return NoMaterial;
                    }
                    const auto which = static_cast<std::size_t>(polygon.material);
                    return (polygon.material >= 0 && which < batchMaterials.size()) ? which
                                                                                    : NoMaterial;
                };
                std::vector<std::size_t> batchOrder;
                {
                    std::vector<bool> seen(batchMaterials.size(), false);
                    bool seenNone = false;
                    for (const Polygon& polygon : polygons)
                    {
                        const std::size_t which = batchOf(polygon);
                        if (which == NoMaterial)
                        {
                            if (!seenNone)
                            {
                                seenNone = true;
                                batchOrder.push_back(NoMaterial);
                            }
                        }
                        else if (!seen[which])
                        {
                            seen[which] = true;
                            batchOrder.push_back(which);
                        }
                    }
                    if (batchOrder.empty())
                    {
                        batchOrder.push_back(NoMaterial);
                    }
                }
                for (const std::size_t batch : batchOrder)
                {
                    std::vector<std::size_t> used;
                    // A vertex is a control point *and* the channel values the corner carries, not
                    // a control point alone. FBX writes normals, texture coordinates and colours
                    // per polygon vertex, so a cube's eight corners are three vertices each: the
                    // genuine importer answers 8 positions and 24 vertices for `Cube.fbx`, 122 and
                    // 168 for `Cone.fbx`, 362 and 387 for `marble.FBX`. Keying on the control
                    // point alone gave one vertex per position and kept whichever corner happened
                    // to be seen first, which is a hard edge drawn with a neighbour's normal
                    // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-112`).
                    using VertexKey = std::tuple<std::size_t, std::vector<double>,
                                                 std::vector<double>, std::vector<double>>;
                    std::map<VertexKey, SharpRuntime::intcs> local;
                    std::vector<SharpRuntime::intcs> indices;
                    std::vector<std::size_t> cornerOf;      // the polygon-vertex each local vertex came from
                    for (const Polygon& polygon : polygons)
                    {
                        if (batchOf(polygon) != batch)
                        {
                            continue;
                        }
                        // The corners are numbered in the order the *triangulation* introduces
                        // them, not in the order the polygon lists them: XNA answers a hexagon's
                        // control points as 0,1,2,3,5,4 and a twelve-gon's as
                        // 0,1,2,3,11,4,10,5,9,6,8,7, which is the strip's two pointers walking in
                        // from the ends (XNASWEEP-164).
                        const std::size_t cornerCount = polygon.controlPoints.size();
                        std::vector<SharpRuntime::intcs> corners(cornerCount, 0);
                        for (const std::size_t c : CornerVisitOrder(cornerCount))
                        {
                            const std::size_t controlPoint = polygon.controlPoints[c];
                            const std::size_t corner = polygon.corners[c];
                            VertexKey key{controlPoint, normals.At(corner, controlPoint, 0u),
                                          uvs.At(corner, controlPoint, 0u),
                                          colors.At(corner, controlPoint, 0u)};
                            const auto found = local.find(key);
                            if (found == local.end())
                            {
                                const auto assigned = static_cast<SharpRuntime::intcs>(used.size());
                                local.emplace(std::move(key), assigned);
                                used.push_back(controlPoint);
                                cornerOf.push_back(corner);
                                corners[c] = assigned;
                            }
                            else
                            {
                                corners[c] = found->second;
                            }
                        }
                        // The winding is reversed, which is the one thing FBX and .x differ on
                        // that changes what a triangle faces (measured: 0,1,2 answers 2,1,0).
                        //
                        // A polygon with more than three corners is not fanned. The FBX SDK inside
                        // XNA's importer answers a *strip*, and what it answers is a function of
                        // the corner count alone: a concave quad and a convex one give the same
                        // triangles, and so does the same octagon walked from a different corner or
                        // walked backwards. Measured for 3 to 20 corners on
                        // tests/assets/xna40/model/fbx_polygon*.fbx (XNASWEEP-164). A fan and this
                        // agree on triangles up to five corners and disagree from six, and they
                        // disagree on a quad's *corner order* already.
                        for (const std::array<std::size_t, 3>& triangle :
                             TrianglesOfPolygon(cornerCount))
                        {
                            indices.push_back(corners[triangle[0]]);
                            indices.push_back(corners[triangle[1]]);
                            indices.push_back(corners[triangle[2]]);
                        }
                    }
                    if (used.empty())
                    {
                        continue;
                    }
                    auto batchContent = std::make_shared<GeometryContent>();
                    mesh->getGeometryProperty().Add(batchContent);
                    std::vector<SharpRuntime::intcs> positionIndices;
                    positionIndices.reserve(used.size());
                    for (const std::size_t controlPoint : used)
                    {
                        positionIndices.push_back(static_cast<SharpRuntime::intcs>(controlPoint));
                    }
                    batchContent->getVerticesProperty().AddRange(positionIndices);
                    batchContent->getIndicesProperty().AddRange(indices);
                    if (batch < batchMaterials.size())
                    {
                        const auto material = materials.find(batchMaterials[batch]);
                        if (material != materials.end())
                        {
                            // A texture reaches the material the way a material reaches a batch:
                            // through a layer element. The same fixture built without a
                            // `LayerElementTexture` answers a material with no texture at all
                            // (measured, fbx/fbx_material_factor_texture.fbx, XNASWEEP-127).
                            //
                            // *Which* texture is the polygons' own `TextureId`, not the batch's
                            // ordinal. The two agree on SAMPLE-033's `Ship.fbx`, where material 0's
                            // 5,942 polygons carry `TextureId` 0 and the other two materials' 2,228
                            // carry -1, so "texture i belongs to batch i" also put the one texture
                            // on the first batch. They disagree on Spacewar's `p2_pencil.fbx`,
                            // whose *second* material's polygons are the textured ones and whose
                            // first material's are all -1: XNA leaves that first batch without a
                            // texture and CNA gave it one (`XNASWEEP-141`).
                            std::vector<std::pair<std::string, std::int64_t>> channels;
                            for (const NamedTextureLayer& named : namedTextures)
                            {
                                int identifier = -1;
                                for (const Polygon& polygon : polygons)
                                {
                                    if (!batchMaterials.empty() && polygon.material != batch)
                                    {
                                        continue;
                                    }
                                    // The batch's *first* polygon decides, not the first one that
                                    // has a texture: `fbx_texture_second_batch.fbx` is one batch
                                    // whose two polygons carry -1 and 0, and the genuine importer
                                    // answers a material with no texture at all.
                                    const std::vector<double> value = named.layer.At(
                                        polygon.corners.empty() ? 0u : polygon.corners.front(),
                                        polygon.controlPoints.empty() ? 0u
                                                                      : polygon.controlPoints.front(),
                                        static_cast<std::size_t>(&polygon - polygons.data()));
                                    identifier = value.empty() ? -1 : static_cast<int>(value.front());
                                    break;
                                }
                                if (identifier < 0 && named.layer.stride == 0u)
                                {
                                    // No per-polygon list at all: the ordinal is all there is.
                                    identifier = static_cast<int>(batch);
                                }
                                if (identifier >= 0 &&
                                    static_cast<std::size_t>(identifier) < object.textures.size())
                                {
                                    channels.emplace_back(
                                        named.key,
                                        object.textures[static_cast<std::size_t>(identifier)]);
                                }
                            }
                            batchContent->setMaterialProperty(
                                withTexture(material->second, batchMaterials[batch], channels));
                        }
                    }
                    // Normals, then texture coordinates, then colours: the order the genuine
                    // importer answers, which is not the .x route's. A mesh that declares *no*
                    // normals is the exception -- the SDK generates them and appends them after
                    // the channels the file did declare, so they come last there (measured,
                    // fbx/fbx_material_factor_texture.fbx, whose only declared channel is UV;
                    // plans/plan_xna_sample_xnb_sweep.md XNASWEEP-127).
                    const bool declaresNormals = normals.stride != 0u && !normals.values.empty();
                    const auto addNormals = [&]
                    {
                        std::vector<Vector3> channel;
                        for (std::size_t v = 0; v < used.size(); ++v)
                        {
                            const std::vector<double> value =
                                normals.At(cornerOf[v], used[v], 0u);
                            channel.push_back(value.size() >= 3u
                                                  ? Vector3(static_cast<float>(value[0]),
                                                            static_cast<float>(value[1]),
                                                            static_cast<float>(value[2]))
                                                  : (used[v] < generatedNormals.size()
                                                         ? generatedNormals[used[v]]
                                                         : Vector3(0.0f, 0.0f, 1.0f)));
                        }
                        batchContent->getVerticesProperty().getChannelsProperty().Add<Vector3>(
                            VertexChannelNames::Normal(), channel);
                    };
                    if (declaresNormals) { addNormals(); }
                    for (const UvSet& set : uvSets)
                    {
                        std::vector<Vector2> channel;
                        for (std::size_t v = 0; v < used.size(); ++v)
                        {
                            const std::vector<double> value = set.layer.At(cornerOf[v], used[v], 0u);
                            // V is flipped: 0.2 answers 0.8 (measured, fbx_oblique).
                            channel.push_back(value.size() >= 2u
                                                  ? Vector2(static_cast<float>(value[0]),
                                                            1.0f - static_cast<float>(value[1]))
                                                  : Vector2(0.0f, 0.0f));
                        }
                        batchContent->getVerticesProperty().getChannelsProperty().Add<Vector2>(
                            VertexChannelNames::TextureCoordinate(
                                static_cast<SharpRuntime::intcs>(set.index)),
                            channel);
                    }
                    if (colors.stride != 0u && !colors.values.empty())
                    {
                        std::vector<Vector4> channel;
                        for (std::size_t v = 0; v < used.size(); ++v)
                        {
                            const std::vector<double> value = colors.At(cornerOf[v], used[v], 0u);
                            // Not quantized, where the .x route's colours are.
                            channel.push_back(value.size() >= 4u
                                                  ? Vector4(static_cast<float>(value[0]),
                                                            static_cast<float>(value[1]),
                                                            static_cast<float>(value[2]),
                                                            static_cast<float>(value[3]))
                                                  : Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                        }
                        batchContent->getVerticesProperty().getChannelsProperty().Add<Vector4>(
                            VertexChannelNames::Color(0), channel);
                    }
                    if (!declaresNormals) { addNormals(); }
                }
                node = mesh;
            }
            else if (IsBoneNode(object.kind))
            {
                node = std::make_shared<Graphics::BoneContent>();
            }
            else
            {
                node = std::make_shared<NodeContent>();
            }
            node->setNameProperty(object.name);
            node->setTransformProperty(LocalTransform(object, parentGeometricInverse));
            const Rows geometricInverse = InverseGeometricRows(object);
            for (const std::int64_t child : object.children)
            {
                if (objects.count(child) == 0 || objects.at(child).isGeometryData ||
                    !IsSceneNode(objects.at(child).kind))
                {
                    continue;
                }
                node->getChildrenProperty().Add(self(child, geometricInverse, self));
            }
            return node;
        };

        // The scene's own unit, applied where the FBX SDK applies it: a scene declaring
        // `UnitScaleFactor` 2.54 is authored in inches and reaches the pipeline in centimetres.
        // The corpus settles the value and the place it lands: `Cone.fbx` declares 2.54, and the
        // genuine importer answers a basis of 2.54 with the node's own translation multiplied by
        // it too -- -0.000101717 becoming -0.000258362. Every other model in the corpus declares
        // 1, where this changes nothing (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-113`).
        double unitScale = 1.0;
        // FBX 6 nests `GlobalSettings` inside `Objects`; FBX 7 has it at the top level.
        const Canon::FbxNode* settings = parsed.Find("GlobalSettings");
        if (settings == nullptr)
        {
            if (const Canon::FbxNode* block = parsed.Find("Objects"); block != nullptr)
            {
                settings = block->Find("GlobalSettings");
            }
        }
        // ...and reads it only where `Definitions` declares the object type, which is how a 6.1
        // reader decides what is in a file at all. Measured both ways on one fixture: the same
        // GlobalSettings changes nothing undeclared and scales the scene by 2.54 declared.
        bool globalsDeclared = false;
        if (const Canon::FbxNode* definitions = parsed.Find("Definitions"); definitions != nullptr)
        {
            for (const Canon::FbxNode& kind : definitions->children)
            {
                if (kind.name == "ObjectType" && kind.Text(0) == "GlobalSettings")
                {
                    globalsDeclared = true;
                }
            }
        }
        if (settings != nullptr && globalsDeclared)
        {
            const double factor = PropertyNumber(*settings, "UnitScaleFactor", 1.0);
            if (factor > 0.0) { unitScale = factor; }
        }

        // Whether the scene answers its single child directly or a synthesized `RootNode` is
        // decided by how many top-level objects there are, cameras included -- a mesh alone
        // answers itself, and the same mesh beside a producer camera and a camera switcher
        // answers `/RootNode/Tri` (measured, `fbx_cameras.fbx`). Only the scene nodes are then
        // converted (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-111`).
        // A file with no `Connections` at all has no scene; then every unattached object is one,
        // which is what this used to assume for every file.
        bool anyInScene = false;
        for (const auto& [identity, object] : objects)
        {
            if (object.inScene) { anyInScene = true; }
        }
        std::vector<std::int64_t> roots;
        std::size_t topLevel = 0u;
        for (const std::int64_t identity : (anyInScene ? sceneOrder : declarationOrder))
        {
            const Object& object = objects.at(identity);
            const bool top = anyInScene ? object.inScene : !object.attached;
            if (!top || object.kind == "Material" || object.isGeometryData) { continue; }
            ++topLevel;
            if (IsSceneNode(object.kind)) { roots.push_back(identity); }
        }
        if (unitScale != 1.0f)
        {
            // Only the top-level nodes: a child's transform is already expressed in its parent's
            // space, and scaling it again would compound the conversion down the chain. `Hier.fbx`
            // measures exactly that -- a parent under a unit of 100 comes back scaled and its child
            // comes back with its own scale and its own pivot-composed translation, untouched.
            for (const std::int64_t identity : roots)
            {
                objects.at(identity).unitScale = unitScale;
            }
        }
        if (roots.size() == 1u && topLevel == 1u)
        {
            // One top-level model answers as the root itself, as the .x route's single frame does.
            return PromoteSkeletonRoot(build(roots.front(), IdentityRows(), build), context);
        }
        auto root = std::make_shared<NodeContent>();
        root->setNameProperty("RootNode");
        for (const std::int64_t identity : roots)
        {
            root->getChildrenProperty().Add(build(identity, IdentityRows(), build));
        }
        return PromoteSkeletonRoot(root, context);
    }

    ContentImporterAttribute FbxImporter::Attribute()
    {
        ContentImporterAttribute attribute(".fbx");
        attribute.setDefaultProcessorProperty("ModelProcessor");
        attribute.setDisplayNameProperty("Autodesk FBX - XNA Framework");
        attribute.setCacheImportedDataProperty(true);
        return attribute;
    }

    const std::string& FbxImporter::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
