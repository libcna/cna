// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const std::string& VertexDeclaration::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Graphics.VertexDeclaration";
        return typeName;
    }

    namespace
    {
        int GetTypeSize(VertexElementFormat fmt)
        {
            switch (fmt) {
                case VertexElementFormat::Single:           return 4;
                case VertexElementFormat::Vector2:          return 8;
                case VertexElementFormat::Vector3:          return 12;
                case VertexElementFormat::Vector4:          return 16;
                case VertexElementFormat::Color:            return 4;
                case VertexElementFormat::Byte4:            return 4;
                case VertexElementFormat::Short2:           return 4;
                case VertexElementFormat::Short4:           return 8;
                case VertexElementFormat::NormalizedShort2: return 4;
                case VertexElementFormat::NormalizedShort4: return 8;
                case VertexElementFormat::HalfVector2:      return 4;
                case VertexElementFormat::HalfVector4:      return 8;
                default:                                    return 0;
            }
        }

        int GetVertexStride(const std::vector<VertexElement>& elements)
        {
            int vertexStride = 0;
            for (const VertexElement& element : elements)
            {
                vertexStride = std::max(
                    vertexStride,
                    element.getOffsetProperty() +
                        GetTypeSize(element.getVertexElementFormatProperty()));
            }
            return vertexStride;
        }

        void Validate(int vertexStride, const std::vector<VertexElement>& elements)
        {
            if (vertexStride <= 0)
                throw System::ArgumentOutOfRangeException("vertexStride");
            if ((vertexStride & 3) != 0)
                throw System::ArgumentException(
                    "The vertex stride must be a multiple of four bytes.");

            std::vector<int> byteOwners(static_cast<std::size_t>(vertexStride), -1);
            for (std::size_t i = 0; i < elements.size(); ++i)
            {
                const VertexElement& element = elements[i];
                const int offset = element.getOffsetProperty();
                const int typeSize = GetTypeSize(element.getVertexElementFormatProperty());
                const int usage = static_cast<int>(element.getVertexElementUsageProperty());

                if (usage < static_cast<int>(VertexElementUsage::Position) ||
                    usage > static_cast<int>(VertexElementUsage::TessellateFactor))
                {
                    throw System::ArgumentException(
                        "The vertex declaration contains an invalid vertex element usage.");
                }
                if (offset < 0 || offset > vertexStride - typeSize)
                {
                    throw System::ArgumentException(
                        "A vertex element lies outside the declared vertex stride.");
                }
                if ((offset & 3) != 0)
                {
                    throw System::ArgumentException(
                        "Vertex element offsets must be multiples of four bytes.");
                }

                for (std::size_t previous = 0; previous < i; ++previous)
                {
                    if (element.getVertexElementUsageProperty() ==
                            elements[previous].getVertexElementUsageProperty() &&
                        element.getUsageIndexProperty() ==
                            elements[previous].getUsageIndexProperty())
                    {
                        throw System::ArgumentException(
                            "The vertex declaration contains a duplicate usage and usage index.");
                    }
                }

                for (int byte = offset; byte < offset + typeSize; ++byte)
                {
                    if (byteOwners[static_cast<std::size_t>(byte)] >= 0)
                    {
                        throw System::ArgumentException(
                            "The vertex declaration contains overlapping elements.");
                    }
                    byteOwners[static_cast<std::size_t>(byte)] = static_cast<int>(i);
                }
            }
        }
    }

    VertexDeclaration::VertexDeclaration(std::initializer_list<VertexElement> elements)
        : elements_(elements)
    {
        if (elements_.empty())
            throw System::ArgumentNullException("elements");

        vertexStride_ = GetVertexStride(elements_);
        Validate(vertexStride_, elements_);
    }

    VertexDeclaration::VertexDeclaration(std::vector<VertexElement> elements)
        : elements_(std::move(elements))
    {
        if (elements_.empty())
            throw System::ArgumentNullException("elements");

        vertexStride_ = GetVertexStride(elements_);
        Validate(vertexStride_, elements_);
    }

    VertexDeclaration::VertexDeclaration(const VertexDeclaration& other)
        : GraphicsResource(other)
        , vertexStride_(other.vertexStride_)
        , elements_(other.elements_)
    {
        ShareResourceIdentityWith(other);
    }

    VertexDeclaration& VertexDeclaration::operator=(const VertexDeclaration& other)
    {
        if (this != &other)
        {
            GraphicsResource::operator=(other);
            vertexStride_ = other.vertexStride_;
            elements_ = other.elements_;
            ShareResourceIdentityWith(other);
        }
        return *this;
    }

    VertexDeclaration::VertexDeclaration(
        int vertexStride,
        std::initializer_list<VertexElement> elements)
        : vertexStride_(vertexStride)
        , elements_(elements)
    {
        if (elements_.empty())
            throw System::ArgumentNullException("elements");
        Validate(vertexStride_, elements_);
    }

    VertexDeclaration::VertexDeclaration(
        int vertexStride,
        std::vector<VertexElement> elements)
        : vertexStride_(vertexStride)
        , elements_(std::move(elements))
    {
        if (elements_.empty())
            throw System::ArgumentNullException("elements");
        Validate(vertexStride_, elements_);
    }

    void VertexDeclaration::ValidateForProfile(GraphicsProfile graphicsProfile) const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexDeclaration");

        // The public XNA constructors cannot create an empty declaration. CNA's marked extension
        // default constructor exists solely for legacy extension buffers whose renderer-side
        // declaration is supplied later, so preserve that deliberately separate path.
        if (vertexStride_ == 0 && elements_.empty())
            return;

        if (vertexStride_ > 255)
        {
            throw System::NotSupportedException(
                "The vertex stride exceeds the active graphics profile limit of 255 bytes.");
        }
        if (elements_.size() > 16)
        {
            throw System::NotSupportedException(
                "The vertex declaration exceeds the active graphics profile limit of 16 elements.");
        }

        const int maximumFormat = graphicsProfile == GraphicsProfile::HiDef
            ? static_cast<int>(VertexElementFormat::HalfVector4)
            : static_cast<int>(VertexElementFormat::NormalizedShort4);
        for (const VertexElement& element : elements_)
        {
            const int format = static_cast<int>(element.getVertexElementFormatProperty());
            if (format < static_cast<int>(VertexElementFormat::Single) ||
                format > maximumFormat)
            {
                throw System::NotSupportedException(
                    "The vertex element format is not supported by the active graphics profile.");
            }

            const int usageIndex = element.getUsageIndexProperty();
            if (usageIndex < 0 || usageIndex >= 16)
            {
                throw System::ArgumentException(
                    "Vertex element usage indices must be between zero and fifteen.",
                    "vertexElements");
            }
        }
    }

    void VertexDeclaration::BindToDevice(GraphicsDevice& device)
    {
        BindSharedResourceIdentityToDevice(&device);
    }
}
