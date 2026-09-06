// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/VertexBufferContent.hpp"

#include <map>
#include <mutex>
#include <utility>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    namespace
    {
        /** @brief The byte size of each vertex element type, keyed by its .NET name. */
        const std::map<std::string, SharpRuntime::intcs>& ElementSizes()
        {
            static const std::map<std::string, SharpRuntime::intcs> sizes = []
            {
                std::map<std::string, SharpRuntime::intcs> map;
                const auto add = [&map]<typename T>()
                {
                    map[System::Type::From<T>().getFullNameProperty()] =
                        static_cast<SharpRuntime::intcs>(Graphics::detail::PixelTraits<T>::Bytes);
                };
                add.template operator()<float>();
                add.template operator()<Vector2>();
                add.template operator()<Vector3>();
                add.template operator()<Vector4>();
                add.template operator()<Color>();
                // Every packed vector type, at the size its own traits give: XNA accepts all of
                // them and answers exactly that (measured, modelprocessor/vertex_buffer_sizeof_packed
                // -- Byte4=4, Short2=4, HalfSingle=2, Alpha8=1, Rgba64=8 and the rest). Byte4 is
                // the one that matters most: a model's BlendIndices are Byte4, so without it a
                // skinned vertex buffer cannot be built at all
                // (plans/plan_xnapipeline_parity.md XNAPP-266).
                {
                    using namespace Microsoft::Xna::Framework::Graphics::PackedVector;
                    add.template operator()<Alpha8>();
                    add.template operator()<Bgr565>();
                    add.template operator()<Bgra4444>();
                    add.template operator()<Bgra5551>();
                    add.template operator()<Byte4>();
                    add.template operator()<HalfSingle>();
                    add.template operator()<HalfVector2>();
                    add.template operator()<HalfVector4>();
                    add.template operator()<NormalizedByte2>();
                    add.template operator()<NormalizedByte4>();
                    add.template operator()<NormalizedShort2>();
                    add.template operator()<NormalizedShort4>();
                    add.template operator()<Rg32>();
                    add.template operator()<Rgba1010102>();
                    add.template operator()<Rgba64>();
                    add.template operator()<Short2>();
                    add.template operator()<Short4>();
                }
                // The rest are what XNA answers for the primitives and the larger value types
                // (measured, modelprocessor/vertex_buffer_sizeof_refusals). Two of them are the
                // sizes .NET marshals a value to rather than the size C++ gives it: a `Boolean`
                // is four bytes and a `Char` one, which is what XNA's own answer is.
                const auto set = [&map]<typename T>(SharpRuntime::intcs bytes)
                { map[System::Type::From<T>().getFullNameProperty()] = bytes; };
                set.template operator()<SharpRuntime::bytecs>(1);
                set.template operator()<SharpRuntime::charcs>(1);
                set.template operator()<SharpRuntime::shortcs>(2);
                set.template operator()<bool>(4);
                set.template operator()<SharpRuntime::intcs>(4);
                set.template operator()<double>(8);
                set.template operator()<Quaternion>(16);
                set.template operator()<Matrix>(64);
                return map;
            }();
            return sizes;
        }
    }

    System::Collections::ObjectModel::Collection<Microsoft::Xna::Framework::Graphics::VertexElement>&
    VertexDeclarationContent::getVertexElementsProperty() noexcept
    {
        return elements_;
    }

    const System::Collections::ObjectModel::Collection<Microsoft::Xna::Framework::Graphics::VertexElement>&
    VertexDeclarationContent::getVertexElementsProperty() const noexcept
    {
        return elements_;
    }

    std::optional<SharpRuntime::intcs> VertexDeclarationContent::getVertexStrideProperty() const noexcept
    {
        return stride_;
    }

    void VertexDeclarationContent::setVertexStrideProperty(std::optional<SharpRuntime::intcs> value) noexcept
    {
        stride_ = value;
    }

    const std::string& VertexDeclarationContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    VertexBufferContent::VertexBufferContent() : declaration_(std::make_shared<VertexDeclarationContent>()) {}

    VertexBufferContent::VertexBufferContent(SharpRuntime::intcs size)
        : data_(static_cast<std::size_t>(size < 0 ? 0 : size), SharpRuntime::bytecs{0}),
          declaration_(std::make_shared<VertexDeclarationContent>())
    {
    }

    const std::vector<SharpRuntime::bytecs>& VertexBufferContent::getVertexDataProperty() const noexcept
    {
        return data_;
    }

    std::vector<SharpRuntime::bytecs>& VertexBufferContent::getVertexDataProperty() noexcept { return data_; }

    const std::shared_ptr<VertexDeclarationContent>& VertexBufferContent::getVertexDeclarationProperty()
        const noexcept
    {
        return declaration_;
    }

    void VertexBufferContent::setVertexDeclarationProperty(std::shared_ptr<VertexDeclarationContent> value) noexcept
    {
        declaration_ = std::move(value);
    }

    SharpRuntime::intcs VertexBufferContent::SizeOf(System::Type type)
    {
        if (type == System::Type())
        {
            throw System::ArgumentNullException("type");
        }
        const auto found = ElementSizes().find(type.getFullNameProperty());
        if (found == ElementSizes().end())
        {
            // A type no vertex element can carry is refused as unsupported rather than as a bad
            // argument (measured, modelprocessor/vertex_buffer_sizeof_refusals).
            throw System::NotSupportedException("Type " + type.getFullNameProperty() +
                                                " cannot be used in a vertex buffer.");
        }
        return found->second;
    }

    void VertexBufferContent::WriteBytes(SharpRuntime::intcs offset, const std::vector<SharpRuntime::bytecs>& bytes)
    {
        if (offset < 0 || static_cast<std::size_t>(offset) + bytes.size() > data_.size())
        {
            throw System::ArgumentOutOfRangeException("offset");
        }
        std::copy(bytes.begin(), bytes.end(), data_.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    void VertexBufferContent::Write(SharpRuntime::intcs offset, SharpRuntime::intcs stride,
                                    System::Type dataType, const std::vector<ContentObject>& data)
    {
        // The element type is named rather than deduced, so the values are unboxed through the
        // same table the typed overload writes with.
        const auto write = [&]<typename T>()
        {
            if (dataType != System::Type::From<T>())
            {
                return false;
            }
            std::vector<T> values;
            values.reserve(data.size());
            for (const ContentObject& value : data)
            {
                if (!Holds<T>(value))
                {
                    throw System::ArgumentException(
                        "A value is not of type " + dataType.getFullNameProperty() + ".", "data");
                }
                values.push_back(Unbox<T>(value));
            }
            Write<T>(offset, stride, values);
            return true;
        };
        if (write.template operator()<float>() || write.template operator()<Vector2>() ||
            write.template operator()<Vector3>() || write.template operator()<Vector4>() ||
            write.template operator()<Color>())
        {
            return;
        }
        throw System::NotSupportedException("Type " + dataType.getFullNameProperty() +
                                            " cannot be used in a vertex buffer.");
    }

    const std::string& VertexBufferContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
