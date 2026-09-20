// SPDX-License-Identifier: MS-PL
#include <any>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "System/IndexOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NullReferenceException.hpp"

using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    template<class Collection, class Element>
    void CheckArrayEnumerator(const Collection& empty, const Collection& one,
                              const Collection& many, Element* first, Element* second)
    {
        using Enumerator = typename Collection::Enumerator;
        static_assert(std::is_copy_constructible_v<Enumerator>);
        static_assert(std::is_default_constructible_v<Enumerator>);
        static_assert(std::is_same_v<decltype(std::declval<const Collection&>().GetEnumerator()),
                                     Enumerator>);
        static_assert(std::is_base_of_v<System::Collections::Generic::IEnumerator<Element*>,
                                        Enumerator>);
        static_assert(std::is_base_of_v<System::IDisposable, Enumerator>);

        Enumerator defaultValue;
        EXPECT_THROW((void)defaultValue.Current(), System::NullReferenceException);
        EXPECT_THROW((void)defaultValue.MoveNext(), System::NullReferenceException);
        defaultValue.Reset();
        defaultValue.Dispose();

        auto emptyCursor = empty.GetEnumerator();
        EXPECT_THROW((void)emptyCursor.Current(), System::IndexOutOfRangeException);
        EXPECT_FALSE(emptyCursor.MoveNext());
        EXPECT_FALSE(emptyCursor.MoveNext());
        EXPECT_THROW((void)emptyCursor.Current(), System::IndexOutOfRangeException);
        emptyCursor.Reset();
        EXPECT_FALSE(emptyCursor.MoveNext());

        auto single = one.GetEnumerator();
        EXPECT_THROW((void)single.Current(), System::IndexOutOfRangeException);
        ASSERT_TRUE(single.MoveNext());
        EXPECT_EQ(single.Current(), first);
        EXPECT_EQ(std::any_cast<Element*>(single.getCurrentProperty()), first);
        EXPECT_FALSE(single.MoveNext());
        EXPECT_THROW((void)single.Current(), System::IndexOutOfRangeException);
        single.Reset();
        ASSERT_TRUE(single.MoveNext());
        EXPECT_EQ(single.Current(), first);
        single.Dispose();
        EXPECT_EQ(single.Current(), first);
        auto polymorphicCursor = one.GetEnumerator();
        System::Collections::IEnumerator& erased = polymorphicCursor;
        EXPECT_THROW((void)erased.getCurrentProperty(), System::IndexOutOfRangeException);
        ASSERT_TRUE(erased.MoveNext());
        EXPECT_EQ(std::any_cast<Element*>(erased.getCurrentProperty()), first);
        erased.Reset();
        EXPECT_THROW((void)erased.getCurrentProperty(), System::IndexOutOfRangeException);

        auto a = many.GetEnumerator();
        auto b = many.GetEnumerator();
        ASSERT_TRUE(a.MoveNext());
        EXPECT_EQ(a.Current(), first);
        auto copied = a;
        ASSERT_TRUE(a.MoveNext());
        EXPECT_EQ(a.Current(), second);
        EXPECT_EQ(copied.Current(), first);
        ASSERT_TRUE(copied.MoveNext());
        EXPECT_EQ(copied.Current(), second);
        ASSERT_TRUE(b.MoveNext());
        EXPECT_EQ(b.Current(), first);
        ASSERT_TRUE(b.MoveNext());
        EXPECT_EQ(b.Current(), second);
        EXPECT_FALSE(a.MoveNext());
        EXPECT_FALSE(a.MoveNext());
        a.Reset();
        ASSERT_TRUE(a.MoveNext());
        EXPECT_EQ(a.Current(), first);

        ASSERT_EQ(many.begin(), many.begin());
        EXPECT_EQ(*many.begin(), first);
        EXPECT_EQ(*(many.end() - 1), second);
        std::vector<Element*> viaRange;
        for (Element* item : many)
        {
            viaRange.push_back(item);
        }
        EXPECT_EQ(viaRange, (std::vector<Element*>{first, second}));
    }

    Effect* FakeEffect(int id)
    {
        return reinterpret_cast<Effect*>(static_cast<std::uintptr_t>(id));
    }
}

TEST(ModelBoneCollectionEnumeratorTest, EmptyOneManyCopyResetAndCppIteration)
{
    ModelBoneCollection empty;
    ModelBone root{0, "root"};
    ModelBone first{1, "first"};
    ModelBone second{2, "second"};
    root.AddChild(&first);
    const ModelBoneCollection one = root.getChildrenProperty();
    root.AddChild(&second);
    CheckArrayEnumerator(empty, one, root.getChildrenProperty(), &first, &second);
}

TEST(ModelBoneCollectionEnumeratorTest, LaterAddedChildDoesNotExtendArraySnapshot)
{
    ModelBone root{0, "root"};
    ModelBone first{1, "first"};
    ModelBone second{2, "second"};
    root.AddChild(&first);
    auto cursor = root.getChildrenProperty().GetEnumerator();
    root.AddChild(&second);
    ASSERT_TRUE(cursor.MoveNext());
    EXPECT_EQ(cursor.Current(), &first);
    EXPECT_FALSE(cursor.MoveNext());
}

TEST(ModelMeshCollectionEnumeratorTest, EmptyOneManyCopyResetAndCppIteration)
{
    ModelMeshCollection empty;
    ModelMesh first{nullptr, "first", {}};
    ModelMesh second{nullptr, "second", {}};
    Model one{nullptr, {}, {&first}};
    Model many{nullptr, {}, {&first, &second}};
    CheckArrayEnumerator(empty, one.getMeshesProperty(), many.getMeshesProperty(), &first, &second);
}

TEST(ModelMeshPartCollectionEnumeratorTest, EmptyOneManyCopyResetAndCppIteration)
{
    ModelMeshPart first;
    ModelMeshPart second;
    ModelMesh empty{nullptr, {}};
    ModelMesh one{nullptr, {&first}};
    ModelMesh many{nullptr, {&first, &second}};
    CheckArrayEnumerator(empty.getMeshPartsProperty(), one.getMeshPartsProperty(),
                         many.getMeshPartsProperty(), &first, &second);
}

TEST(ModelEffectCollectionEnumeratorTest, EmptyOneManyCopyResetAndCppIteration)
{
    static_assert(std::is_same_v<
        decltype(std::declval<const ModelEffectCollection&>().GetEnumerator()),
        ModelEffectCollection::Enumerator>);
    ModelEffectCollection::Enumerator defaultValue;
    EXPECT_EQ(defaultValue.Current(), nullptr);
    EXPECT_THROW((void)defaultValue.MoveNext(), System::NullReferenceException);
    EXPECT_THROW(defaultValue.Reset(), System::NullReferenceException);
    defaultValue.Dispose();

    ModelEffectCollection empty;
    ModelEffectCollection one;
    ModelEffectCollection many;
    Effect* first = FakeEffect(1);
    Effect* second = FakeEffect(2);
    one.Add(first);
    many.Add(first);
    many.Add(second);

    auto emptyCursor = empty.GetEnumerator();
    EXPECT_EQ(emptyCursor.Current(), nullptr);
    EXPECT_THROW((void)emptyCursor.getCurrentProperty(), System::InvalidOperationException);
    EXPECT_FALSE(emptyCursor.MoveNext());
    EXPECT_EQ(emptyCursor.Current(), nullptr);
    emptyCursor.Reset();
    EXPECT_FALSE(emptyCursor.MoveNext());

    auto single = one.GetEnumerator();
    EXPECT_EQ(single.Current(), nullptr);
    ASSERT_TRUE(single.MoveNext());
    EXPECT_EQ(single.Current(), first);
    EXPECT_EQ(std::any_cast<Effect*>(single.getCurrentProperty()), first);
    EXPECT_FALSE(single.MoveNext());
    EXPECT_EQ(single.Current(), nullptr);
    EXPECT_THROW((void)single.getCurrentProperty(), System::InvalidOperationException);
    single.Reset();
    ASSERT_TRUE(single.MoveNext());
    EXPECT_EQ(single.Current(), first);
    single.Dispose();
    EXPECT_EQ(single.Current(), first);
    auto polymorphicCursor = one.GetEnumerator();
    System::Collections::IEnumerator& erased = polymorphicCursor;
    EXPECT_THROW((void)erased.getCurrentProperty(), System::InvalidOperationException);
    ASSERT_TRUE(erased.MoveNext());
    EXPECT_EQ(std::any_cast<Effect*>(erased.getCurrentProperty()), first);
    erased.Reset();
    EXPECT_THROW((void)erased.getCurrentProperty(), System::InvalidOperationException);

    auto a = many.GetEnumerator();
    auto b = many.GetEnumerator();
    ASSERT_TRUE(a.MoveNext());
    auto copied = a;
    ASSERT_TRUE(a.MoveNext());
    EXPECT_EQ(a.Current(), second);
    EXPECT_EQ(copied.Current(), first);
    ASSERT_TRUE(copied.MoveNext());
    EXPECT_EQ(copied.Current(), second);
    ASSERT_TRUE(b.MoveNext());
    EXPECT_EQ(b.Current(), first);
    ASSERT_TRUE(b.MoveNext());
    EXPECT_EQ(b.Current(), second);
    EXPECT_FALSE(a.MoveNext());
    EXPECT_FALSE(a.MoveNext());
    a.Reset();
    ASSERT_TRUE(a.MoveNext());
    EXPECT_EQ(a.Current(), first);

    EXPECT_EQ(*many.begin(), first);
    EXPECT_EQ(*(many.end() - 1), second);
    std::vector<Effect*> viaRange;
    for (Effect* item : many)
    {
        viaRange.push_back(item);
    }
    EXPECT_EQ(viaRange, (std::vector<Effect*>{first, second}));
}

TEST(ModelEffectCollectionEnumeratorTest, MutationInvalidatesMoveNextAndReset)
{
    ModelEffectCollection effects;
    effects.Add(FakeEffect(1));
    auto cursor = effects.GetEnumerator();
    ASSERT_TRUE(cursor.MoveNext());
    effects.Add(FakeEffect(2));
    EXPECT_THROW((void)cursor.MoveNext(), System::InvalidOperationException);
    EXPECT_THROW(cursor.Reset(), System::InvalidOperationException);
    EXPECT_EQ(cursor.Current(), FakeEffect(1));
    auto fresh = effects.GetEnumerator();
    ASSERT_TRUE(fresh.MoveNext());
    ASSERT_TRUE(fresh.MoveNext());
    EXPECT_EQ(fresh.Current(), FakeEffect(2));
}
