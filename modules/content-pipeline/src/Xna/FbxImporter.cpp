// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/ModelImporters.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/FbxFileReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"
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
            Vector3 translation{0.0f, 0.0f, 0.0f};
            Vector3 rotation{0.0f, 0.0f, 0.0f};
            Vector3 scaling{1.0f, 1.0f, 1.0f};
            bool attached = false;
        };

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

        [[nodiscard]] Matrix LocalTransform(const Object& object)
        {
            const float toRadians = 0.017453292519943295f;
            return Matrix::CreateScale(object.scaling) *
                   Matrix::CreateRotationX(object.rotation.X * toRadians) *
                   Matrix::CreateRotationY(object.rotation.Y * toRadians) *
                   Matrix::CreateRotationZ(object.rotation.Z * toRadians) *
                   Matrix::CreateTranslation(object.translation);
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
                if (node.name != "Model" && node.name != "Geometry" && node.name != "Material")
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
                object.translation = PropertyVector(node, "Lcl Translation", Vector3(0.0f, 0.0f, 0.0f));
                object.rotation = PropertyVector(node, "Lcl Rotation", Vector3(0.0f, 0.0f, 0.0f));
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
                    continue;                        // connected to the scene root
                }
                if (childObject->second.kind == "Material")
                {
                    parentObject->second.materials.push_back(child);
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
            material->setDiffuseColorProperty(
                PropertyVector(*object.node, "DiffuseColor", Vector3(0.0f, 0.0f, 0.0f)));
            material->setEmissiveColorProperty(
                PropertyVector(*object.node, "EmissiveColor", Vector3(0.0f, 0.0f, 0.0f)));
            material->setAlphaProperty(1.0f);
            material->setSpecularColorProperty(
                PropertyVector(*object.node, "SpecularColor", Vector3(0.0f, 0.0f, 0.0f)));
            material->setSpecularPowerProperty(20.0f);
            materials.emplace(identity, std::move(material));
        }

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
                    std::map<std::size_t, SharpRuntime::intcs> local;
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
                            const auto found = local.find(controlPoint);
                            if (found == local.end())
                            {
                                const auto assigned = static_cast<SharpRuntime::intcs>(used.size());
                                local.emplace(controlPoint, assigned);
                                used.push_back(controlPoint);
                                cornerOf.push_back(polygon.corners[c]);
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
                            batchContent->setMaterialProperty(material->second);
                        }
                    }
                    // Normals, then texture coordinates, then colours: the order the genuine
                    // importer answers, which is not the .x route's.
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
                    }
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
                if (objects.count(child) == 0 || objects.at(child).kind == "Material")
                {
                    continue;
                }
                node->getChildrenProperty().Add(self(child, self));
            }
            return node;
        };

        std::vector<std::int64_t> roots;
        for (const auto& [identity, object] : objects)
        {
            if (!object.attached && object.kind != "Material")
            {
                roots.push_back(identity);
            }
        }
        if (roots.size() == 1u)
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
