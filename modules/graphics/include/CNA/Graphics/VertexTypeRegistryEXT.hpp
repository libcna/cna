// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <type_traits>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/IVertexType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "System/Type.hpp"

namespace CNA::Graphics
{
    /**
     * @brief Resolves a `System::Type` to the VertexDeclaration of the vertex structure it names.
     *
     * This is the C++ counterpart of XNA's internal `VertexDeclaration.FromType(Type)`, which the
     * documented `VertexBuffer(GraphicsDevice, Type, Int32, BufferUsage)` and
     * `DynamicVertexBuffer(GraphicsDevice, Type, Int32, BufferUsage)` constructors use. XNA gets
     * there by reflection -- it calls `Activator.CreateInstance(vertexType)`, casts the result to
     * `IVertexType`, and reads its `VertexDeclaration`. C++ has no reflection, so a vertex type
     * makes itself resolvable by registering: `Register<TVertex>()` performs exactly the same three
     * steps at the point where the concrete type is still known.
     *
     * CNA's own vertex structures are registered automatically, so a game that uses them needs no
     * setup. A game's own `IVertexType` structure must call `Register<TVertex>()` once -- typically
     * during startup -- before naming it in one of those constructors. This is `CNAEXT`: XNA
     * exposes no such registry, because it does not need one.
     */
    class CNAEXT VertexTypeRegistryEXT
    {
    public:
        /**
         * @brief Makes @p TVertex resolvable from its `System::Type`.
         *
         * Stores the declaration an instance of @p TVertex reports, so a later lookup needs no
         * instance. Registering the same type twice is harmless.
         *
         * @tparam TVertex A default-constructible structure deriving from
         *         `Microsoft::Xna::Framework::Graphics::IVertexType`.
         */
        template <typename TVertex>
        static void Register()
        {
            static_assert(std::is_base_of_v<Microsoft::Xna::Framework::Graphics::IVertexType, TVertex>,
                          "a registered vertex type must derive from IVertexType, which is what "
                          "XNA's FromType casts to");
            static_assert(std::is_default_constructible_v<TVertex>,
                          "a registered vertex type must be default-constructible, because XNA's "
                          "FromType reaches its declaration through Activator.CreateInstance");
            const TVertex probe{};
            RegisterResolved(System::Type::From<TVertex>(), probe.getVertexDeclarationProperty());
        }

        /**
         * @brief Returns the declaration registered for @p vertexType, or nullptr if there is none.
         *
         * @param vertexType The type to look up.
         * @return The registered declaration, or @c nullptr when the type was never registered.
         */
        [[nodiscard]] static const Microsoft::Xna::Framework::Graphics::VertexDeclaration* TryResolve(
            const System::Type& vertexType);

        /**
         * @brief Returns the declaration registered for @p vertexType, or throws.
         *
         * A type that is not a registered `IVertexType` structure is an `ArgumentException`, which
         * covers both of XNA's refusals -- not a value type, and not an `IVertexType` -- because a
         * C++ type satisfying neither could not have been registered.
         *
         * XNA's third check, `Marshal.SizeOf(vertexType) != vertexDeclaration._vertexStride`, has
         * no counterpart here and is deliberately absent. CNA's vertex structures derive from
         * `IVertexType`, a polymorphic base, so each one carries a vtable pointer and
         * `sizeof(TVertex)` is larger than the vertex data the declaration describes --
         * `VertexPositionColor` is 16 bytes of vertex data in a 24-byte C++ object. The C++ size is
         * therefore not comparable with the stride, and a check against it would reject every
         * correct vertex type rather than catching an incorrect one.
         *
         * @param vertexType The type to look up.
         * @return The registered declaration.
         * @throws System::ArgumentException if @p vertexType is not a registered vertex type.
         */
        [[nodiscard]] static const Microsoft::Xna::Framework::Graphics::VertexDeclaration& Resolve(
            const System::Type& vertexType);

    private:
        static void RegisterResolved(
            const System::Type& vertexType,
            const Microsoft::Xna::Framework::Graphics::VertexDeclaration& declaration);
    };
}
