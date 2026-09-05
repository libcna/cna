// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MipmapChain.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MipmapChainCollection.hpp"

#include <utility>

#include "System/ArgumentNullException.hpp"
#include "System/NotSupportedException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    MipmapChain::MipmapChain() = default;

    MipmapChain::MipmapChain(std::shared_ptr<BitmapContent> bitmap)
    {
        Add(bitmap);
    }

    std::string MipmapChain::ToString() const { return std::string(XnaTypeName); }

    void MipmapChain::InsertItem(SharpRuntime::intcs index, const std::shared_ptr<BitmapContent>& item)
    {
        if (item == nullptr)
        {
            throw System::ArgumentNullException("item");
        }
        Collection::InsertItem(index, item);
    }

    void MipmapChain::SetItem(SharpRuntime::intcs index, const std::shared_ptr<BitmapContent>& item)
    {
        if (item == nullptr)
        {
            throw System::ArgumentNullException("item");
        }
        Collection::SetItem(index, item);
    }

    MipmapChainCollection::MipmapChainCollection(SharpRuntime::intcs initialSize, bool fixedSize) : fixedSize_(false)
    {
        for (SharpRuntime::intcs i = 0; i < initialSize; ++i)
        {
            Add(std::make_shared<MipmapChain>());
        }
        fixedSize_ = fixedSize;
    }

    bool MipmapChainCollection::IsFixedSize() const noexcept { return fixedSize_; }

    namespace
    {
        [[noreturn]] void ThrowFixedSize()
        {
            throw System::NotSupportedException(
                "Cannot resize MipmapChainCollection. This type of texture has a fixed number of faces.");
        }
    }

    void MipmapChainCollection::ClearItems()
    {
        if (fixedSize_)
        {
            ThrowFixedSize();
        }
        Collection::ClearItems();
    }

    void MipmapChainCollection::InsertItem(SharpRuntime::intcs index, const std::shared_ptr<MipmapChain>& item)
    {
        if (fixedSize_)
        {
            ThrowFixedSize();
        }
        if (item == nullptr)
        {
            throw System::ArgumentNullException("item");
        }
        Collection::InsertItem(index, item);
    }

    void MipmapChainCollection::RemoveItem(SharpRuntime::intcs index)
    {
        if (fixedSize_)
        {
            ThrowFixedSize();
        }
        Collection::RemoveItem(index);
    }

    void MipmapChainCollection::SetItem(SharpRuntime::intcs index, const std::shared_ptr<MipmapChain>& item)
    {
        if (item == nullptr)
        {
            throw System::ArgumentNullException("item");
        }
        Collection::SetItem(index, item);
    }
}
