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
#include <string>
#include <utility>
#include <vector>

#include "CNA/Content/Pipeline/FbxFileReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"
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
            Vector3 translation{0.0f, 0.0f, 0.0f};
            Vector3 rotation{0.0f, 0.0f, 0.0f};
            Vector3 preRotation{0.0f, 0.0f, 0.0f};
            Vector3 scaling{1.0f, 1.0f, 1.0f};
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
         * `Light` is deliberately not in this list: no source in the corpus carries one, so what
         * XNA does with it has not been measured here and is left alone rather than guessed at.
         *
         * @param kind The object's FBX class.
         * @return Whether it is part of the node graph.
         */
        [[nodiscard]] bool IsSceneNode(const std::string& kind)
        {
            return kind != "Material" && kind != "Texture" && kind != "Camera" &&
                   kind != "CameraSwitcher";
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
        [[nodiscard]] Matrix LocalTransform(const Object& object)
        {
            // Composed in double and narrowed once at the end, because the genuine importer's is.
            // A quarter turn is the case that shows it: `cos` of a float pi/2 is -4.371e-08, of a
            // double pi/2 it is 6.123e-17, and `Cube.fbx`'s `PreRotation -90` reaches XNA's own
            // `Cube.xnb` as 2.54 * 6.123e-17 = 1.5553e-16 -- so a float rotation lands seven orders
            // of magnitude away from XNA's on the two entries a quarter turn zeroes
            // (plans/plan_xna_sample_xnb_sweep.md XNASWEEP-121).
            using Rows = std::array<std::array<double, 4>, 4>;
            const auto multiply = [](const Rows& left, const Rows& right)
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
            };
            const Rows identity{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
            // The three axis rotations XNA's own `Matrix::CreateRotation*` write, in double.
            const auto rotationX = [&identity](double radians)
            {
                Rows m = identity;
                m[1][1] = std::cos(radians);
                m[1][2] = std::sin(radians);
                m[2][1] = -m[1][2];
                m[2][2] = m[1][1];
                return m;
            };
            const auto rotationY = [&identity](double radians)
            {
                Rows m = identity;
                m[0][0] = std::cos(radians);
                m[0][2] = -std::sin(radians);
                m[2][0] = -m[0][2];
                m[2][2] = m[0][0];
                return m;
            };
            const auto rotationZ = [&identity](double radians)
            {
                Rows m = identity;
                m[0][0] = std::cos(radians);
                m[0][1] = std::sin(radians);
                m[1][0] = -m[0][1];
                m[1][1] = m[0][0];
                return m;
            };
            const double toRadians = 0.017453292519943295;
            const auto euler = [&](const Vector3& degrees)
            {
                return multiply(multiply(rotationX(static_cast<double>(degrees.X) * toRadians),
                                         rotationY(static_cast<double>(degrees.Y) * toRadians)),
                                rotationZ(static_cast<double>(degrees.Z) * toRadians));
            };
            Rows scale = identity;
            scale[0][0] = object.scaling.X;
            scale[1][1] = object.scaling.Y;
            scale[2][2] = object.scaling.Z;
            Rows translation = identity;
            translation[3][0] = object.translation.X;
            translation[3][1] = object.translation.Y;
            translation[3][2] = object.translation.Z;

            const Rows composed = multiply(
                multiply(multiply(scale, euler(object.preRotation)), euler(object.rotation)),
                translation);

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

        [[nodiscard]] Layer ReadLayer(const Canon::FbxNode& mesh, const std::string& element,
                                      const std::string& valuesName, const std::string& indexName,
                                      const std::size_t stride)
        {
            Layer layer;
            const Canon::FbxNode* node = mesh.Find(element);
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
    }

    std::shared_ptr<Graphics::NodeContent> FbxImporter::Import(const std::string& filename,
                                                               ContentImporterContext& context)
    {
        (void)context;
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
        std::map<std::string, std::int64_t> byName;
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
                if (node.name == "Material")
                {
                    object.kind = "Material";
                }
                if (node.name == "Texture")
                {
                    object.kind = "Texture";
                }
                object.translation = PropertyVector(node, "Lcl Translation", Vector3(0.0f, 0.0f, 0.0f));
                object.rotation = PropertyVector(node, "Lcl Rotation", Vector3(0.0f, 0.0f, 0.0f));
                object.preRotation = PropertyVector(node, "PreRotation", Vector3(0.0f, 0.0f, 0.0f));
                object.scaling = PropertyVector(node, "Lcl Scaling", Vector3(1.0f, 1.0f, 1.0f));
                byName[object.name] = identity;
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
                    const auto childName = byName.find(BareName(connection.Text(1)));
                    const auto parentName = byName.find(BareName(connection.Text(2)));
                    child = childName == byName.end() ? 0 : childName->second;
                    parent = parentName == byName.end() ? 0 : parentName->second;
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
        std::map<std::pair<std::int64_t, std::int64_t>, std::shared_ptr<BasicMaterialContent>> textured;
        const auto withTexture =
            [&](const std::shared_ptr<BasicMaterialContent>& base, const std::int64_t materialIdentity,
                const std::int64_t textureIdentity) -> std::shared_ptr<BasicMaterialContent>
        {
            if (textureIdentity == 0) { return base; }
            std::string named = textureFile(textureIdentity);
            if (named.empty()) { return base; }
            const std::pair<std::int64_t, std::int64_t> key{materialIdentity, textureIdentity};
            const auto found = textured.find(key);
            if (found != textured.end()) { return found->second; }
            // Spelled the way the tool that wrote the file spells a path, as the `.x` route does.
            std::replace(named.begin(), named.end(), '\\', '/');
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
            copy->setTextureProperty(std::make_shared<ExternalReference<Graphics::TextureContent>>(
                (sourceDirectory / named).lexically_normal().string()));
            textured.emplace(key, copy);
            return copy;
        };

        const auto build = [&](const std::int64_t identity, auto&& self) -> std::shared_ptr<NodeContent>
        {
            const Object& object = objects.at(identity);
            std::shared_ptr<NodeContent> node;
            const Canon::FbxNode* geometry = object.node;
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
                const Layer uvs = ReadLayer(*geometry, "LayerElementUV", "UV", "UVIndex", 2u);
                const Layer colors = ReadLayer(*geometry, "LayerElementColor", "Colors",
                                               "ColorIndex", 4u);
                // A material layer's `Materials` array IS the per-polygon index, whatever its
                // ReferenceInformationType says; reading it as a value list that then needs a
                // second index array is what leaves every polygon on material zero.
                Layer materialLayer = ReadLayer(*geometry, "LayerElementMaterial", "Materials",
                                                "", 1u);
                materialLayer.reference = "Direct";

                // Walk the polygons, gathering each one's control points and which material it uses.
                struct Polygon
                {
                    std::vector<std::size_t> corners;      // indices into the polygon-vertex stream
                    std::vector<std::size_t> controlPoints;
                    std::size_t material = 0u;
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
                        polygons.push_back(std::move(current));
                        current = Polygon{};
                    }
                }

                std::vector<std::int64_t> batchMaterials = object.materials;
                if (batchMaterials.empty() || materialLayer.stride == 0u)
                {
                    // A material reaches a batch only through a LayerElementMaterial (measured:
                    // fbx_quad_textured connects one and answers material=null).
                    batchMaterials.clear();
                }
                const std::size_t batches = batchMaterials.empty() ? 1u : batchMaterials.size();
                for (std::size_t batch = 0; batch < batches; ++batch)
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
                        if (!batchMaterials.empty() && polygon.material != batch)
                        {
                            continue;
                        }
                        std::vector<SharpRuntime::intcs> corners;
                        for (std::size_t c = 0; c < polygon.controlPoints.size(); ++c)
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
                                corners.push_back(assigned);
                            }
                            else
                            {
                                corners.push_back(found->second);
                            }
                        }
                        // The winding is reversed, which is the one thing FBX and .x differ on
                        // that changes what a triangle faces (measured: 0,1,2 answers 2,1,0).
                        for (std::size_t c = 2; c < corners.size(); ++c)
                        {
                            indices.push_back(corners[c]);
                            indices.push_back(corners[c - 1u]);
                            indices.push_back(corners[0]);
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
                            // Texture i belongs to batch i, and a batch past the last texture gets
                            // none: SAMPLE-033's `Ship.fbx` has three materials and one texture,
                            // and the genuine importer puts that texture on the first batch and
                            // leaves the other two without one.
                            const std::int64_t texture =
                                geometry->Find("LayerElementTexture") != nullptr &&
                                        batch < object.textures.size()
                                    ? object.textures[batch]
                                    : 0;
                            batchContent->setMaterialProperty(
                                withTexture(material->second, batchMaterials[batch], texture));
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
                                                  : Vector3(0.0f, 0.0f, 1.0f));
                        }
                        batchContent->getVerticesProperty().getChannelsProperty().Add<Vector3>(
                            VertexChannelNames::Normal(), channel);
                    };
                    if (declaresNormals) { addNormals(); }
                    if (uvs.stride != 0u && !uvs.values.empty())
                    {
                        std::vector<Vector2> channel;
                        for (std::size_t v = 0; v < used.size(); ++v)
                        {
                            const std::vector<double> value = uvs.At(cornerOf[v], used[v], 0u);
                            // V is flipped: 0.2 answers 0.8 (measured, fbx_oblique).
                            channel.push_back(value.size() >= 2u
                                                  ? Vector2(static_cast<float>(value[0]),
                                                            1.0f - static_cast<float>(value[1]))
                                                  : Vector2(0.0f, 0.0f));
                        }
                        batchContent->getVerticesProperty().getChannelsProperty().Add<Vector2>(
                            VertexChannelNames::TextureCoordinate(0), channel);
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
            else
            {
                node = std::make_shared<NodeContent>();
            }
            node->setNameProperty(object.name);
            node->setTransformProperty(LocalTransform(object));
            for (const std::int64_t child : object.children)
            {
                if (objects.count(child) == 0 || !IsSceneNode(objects.at(child).kind))
                {
                    continue;
                }
                node->getChildrenProperty().Add(self(child, self));
            }
            return node;
        };

        // The scene's own unit, applied where the FBX SDK applies it: a scene declaring
        // `UnitScaleFactor` 2.54 is authored in inches and reaches the pipeline in centimetres.
        // The corpus settles the value and the place it lands: `Cone.fbx` declares 2.54, and the
        // genuine importer answers a basis of 2.54 with the node's own translation multiplied by
        // it too -- -0.000101717 becoming -0.000258362. Every other model in the corpus declares
        // 1, where this changes nothing (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-113`).
        float unitScale = 1.0f;
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
            if (factor > 0.0) { unitScale = static_cast<float>(factor); }
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
        for (const auto& [identity, object] : objects)
        {
            const bool top = anyInScene ? object.inScene : !object.attached;
            if (!top || object.kind == "Material") { continue; }
            ++topLevel;
            if (IsSceneNode(object.kind)) { roots.push_back(identity); }
        }
        if (unitScale != 1.0f)
        {
            // Only the top-level nodes: a child's transform is already expressed in its parent's
            // space, and scaling it again would compound the conversion down the chain.
            for (const std::int64_t identity : roots)
            {
                Object& object = objects.at(identity);
                object.scaling = Vector3(object.scaling.X * unitScale, object.scaling.Y * unitScale,
                                         object.scaling.Z * unitScale);
                object.translation =
                    Vector3(object.translation.X * unitScale, object.translation.Y * unitScale,
                            object.translation.Z * unitScale);
            }
        }
        if (roots.size() == 1u && topLevel == 1u)
        {
            // One top-level model answers as the root itself, as the .x route's single frame does.
            return build(roots.front(), build);
        }
        auto root = std::make_shared<NodeContent>();
        root->setNameProperty("RootNode");
        for (const std::int64_t identity : roots)
        {
            root->getChildrenProperty().Add(build(identity, build));
        }
        return root;
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
