// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/VertexTypeRegistryEXT.hpp"

#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <typeindex>
#if defined(__GNUG__)
#include <cxxabi.h>
#endif

#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/ArgumentException.hpp"

namespace CNA::Graphics
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::VertexDeclaration;

        /// One table for the process. Guarded because a game may register its own vertex types from
        /// whatever thread it initialises on, while the buffer constructors read from the thread
        /// that creates resources.
        std::mutex& RegistryMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        // Hashed, not ordered: libstdc++ orders std::type_index by pointer for types local to a
        // translation unit and by name otherwise where type-info names are not merged (MinGW), which
        // is not a strict weak ordering, so a std::map can miss a key it holds.
        std::unordered_map<std::type_index, VertexDeclaration>& Registry()
        {
            static std::unordered_map<std::type_index, VertexDeclaration> registry;
            return registry;
        }

        /// The type as its source spells it. System::Type's name is the raw RTTI one, which GCC
        /// and Clang mangle, so a refusal named `N12_GLOBAL__N_118UnregisteredVertexE`.
        std::string ReadableTypeName(const System::Type& type)
        {
            const std::type_info* info = type.getTypeInfo();
            if (info == nullptr)
            {
                return type.getNameProperty();
            }
#if defined(__GNUG__)
            int status = 0;
            const std::unique_ptr<char, void (*)(void*)> demangled(
                abi::__cxa_demangle(info->name(), nullptr, nullptr, &status), std::free);
            if (status == 0 && demangled != nullptr)
            {
                return demangled.get();
            }
#endif
            return info->name();
        }

        /// CNA's own vertex structures, registered on first use rather than by static initialisers,
        /// so there is no initialisation-order question and a build that never names a Type-based
        /// buffer constructor pays nothing.
        void EnsureStockTypesRegistered()
        {
            using namespace Microsoft::Xna::Framework::Graphics;
            static const bool once = []() {
                VertexTypeRegistryEXT::Register<VertexPositionColor>();
                VertexTypeRegistryEXT::Register<VertexPositionColorTexture>();
                VertexTypeRegistryEXT::Register<VertexPositionNormalTexture>();
                VertexTypeRegistryEXT::Register<VertexPositionTexture>();
                VertexTypeRegistryEXT::Register<VertexPositionNormalTangentTexture>();
                VertexTypeRegistryEXT::Register<VertexPositionNormalTextureSkinned>();
                VertexTypeRegistryEXT::Register<VertexPositionNormalTangentTextureSkinned>();
                return true;
            }();
            (void)once;
        }

        const VertexDeclaration* Find(const System::Type& vertexType)
        {
            EnsureStockTypesRegistered();
            const std::type_info* info = vertexType.getTypeInfo();
            if (info == nullptr)
            {
                return nullptr;
            }
            const std::lock_guard<std::mutex> lock(RegistryMutex());
            auto& registry = Registry();
            const auto found = registry.find(std::type_index(*info));
            return found == registry.end() ? nullptr : &found->second;
        }
    }

    void VertexTypeRegistryEXT::RegisterResolved(
        const System::Type& vertexType,
        const Microsoft::Xna::Framework::Graphics::VertexDeclaration& declaration)
    {
        const std::type_info* info = vertexType.getTypeInfo();
        if (info == nullptr)
        {
            return;
        }
        const std::lock_guard<std::mutex> lock(RegistryMutex());
        Registry().insert_or_assign(std::type_index(*info), declaration);
    }

    const Microsoft::Xna::Framework::Graphics::VertexDeclaration* VertexTypeRegistryEXT::TryResolve(
        const System::Type& vertexType)
    {
        return Find(vertexType);
    }

    const Microsoft::Xna::Framework::Graphics::VertexDeclaration& VertexTypeRegistryEXT::Resolve(
        const System::Type& vertexType)
    {
        const VertexDeclaration* registered = Find(vertexType);
        if (registered == nullptr)
        {
            // XNA's two ArgumentException cases -- not a value type, and not an IVertexType -- are
            // one case here: a C++ type satisfying neither could not have been registered.
            throw System::ArgumentException(
                "The vertex type '" + ReadableTypeName(vertexType) +
                "' is not a registered IVertexType structure. CNA's own vertex structures are "
                "registered automatically; a game's own structure must call "
                "CNA::Graphics::VertexTypeRegistryEXT::Register<TVertex>() once before naming it "
                "here, because C++ cannot reach its declaration by reflection.",
                "vertexType");
        }
        // XNA's third check -- Marshal.SizeOf(vertexType) != declaration._vertexStride -- is
        // deliberately absent; see the header for why sizeof(TVertex) is not the stride here.
        return *registered;
    }
}
