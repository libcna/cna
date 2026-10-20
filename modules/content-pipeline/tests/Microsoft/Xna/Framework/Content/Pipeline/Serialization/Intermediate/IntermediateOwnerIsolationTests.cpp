// SPDX-License-Identifier: MS-PL
// ISSUE-089: stable callback identity must preserve CNA's per-operation state.
#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <barrier>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"

namespace I = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate;
using Microsoft::Xna::Framework::Content::ContentSerializerAttribute;
namespace
{
    struct NestedOwnerPayload
    {
        static constexpr std::string_view XnaTypeName = "Repair.NestedOwnerPayload";
        std::string text, path;
    };
    struct ParallelOwnerPayload
    {
        static constexpr std::string_view XnaTypeName = "Repair.ParallelOwnerPayload";
        std::string text, path;
        int slot = -1;
    };
    struct ColdOwnerPayload
    {
        static constexpr std::string_view XnaTypeName = "Repair.ColdOwnerPayload";
        std::string text, path;
    };
    struct LateOwnerPayload
    {
        static constexpr std::string_view XnaTypeName = "Repair.LateOwnerPayload";
        std::string text, path;
    };
    struct RegisteringOwnerPayload
    {
        static constexpr std::string_view XnaTypeName = "Repair.RegisteringOwnerPayload";
    };
    template <typename T>
    std::string Write(const T& value, const std::string& path)
    {
        std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString());
        I::IntermediateSerializer::Serialize<T>(*writer, value, path);
        return writer->ToString();
    }
    template <typename T>
    class IsolationSerializer final : public I::ContentTypeSerializer<T>
    {
    public:
        std::atomic<I::IntermediateSerializer*> owner{nullptr};
        std::atomic<int> initializations{0};
        std::barrier<>* gate = nullptr;
        std::array<I::IntermediateSerializer*, 2> observed{};
        std::array<std::atomic<bool>, 2> reached{};
    protected:
        void Initialize(I::IntermediateSerializer& context) override
        {
            ++initializations;
            owner.store(&context);
        }
        void Serialize(I::IntermediateWriter& output, const T& value,
                       const ContentSerializerAttribute&) override
        {
            auto& context = output.getSerializerProperty();
            if constexpr (std::is_same_v<T, ParallelOwnerPayload>)
            {
                if (gate != nullptr)
                {
                    const auto slot = static_cast<std::size_t>(value.slot);
                    observed[slot] = &context;
                    reached[slot].store(true);
                    gate->arrive_and_wait();
                }
            }
            EXPECT_EQ(owner.load(), &context);
            EXPECT_EQ(context.getReferenceRelocationPathProperty(), value.path);
            if constexpr (std::is_same_v<T, NestedOwnerPayload>)
            {
                const auto aliases = context.NamespaceAliases();
                EXPECT_FALSE(Write<std::string>("nested", "nested/inner.xml").empty());
                EXPECT_EQ(context.getReferenceRelocationPathProperty(), value.path);
                EXPECT_EQ(context.NamespaceAliases(), aliases);
            }
            output.getXmlProperty().WriteString(value.text);
        }
        T Deserialize(I::IntermediateReader& input, const ContentSerializerAttribute& format, T) override
        {
            T value;
            value.text = input.ReadText(format);
            return value;
        }
    };

    class RegisteringSerializer final : public I::ContentTypeSerializer<RegisteringOwnerPayload>
    {
    public:
        IsolationSerializer<LateOwnerPayload>* late = nullptr;
    protected:
        void Serialize(I::IntermediateWriter& output, const RegisteringOwnerPayload&,
                       const ContentSerializerAttribute&) override
        {
            auto candidate = std::make_unique<IsolationSerializer<LateOwnerPayload>>();
            late = candidate.get();
            (void)I::IntermediateSerializer::RegisterTypeSerializer(std::move(candidate));
            ContentSerializerAttribute format;
            format.setElementNameProperty("Late");
            output.WriteObject<LateOwnerPayload>({"late", "late.xml"}, format);
        }
        RegisteringOwnerPayload Deserialize(I::IntermediateReader&, const ContentSerializerAttribute&,
                                           RegisteringOwnerPayload) override { return {}; }
    };

TEST(IntermediateOwnerIsolationTest, WorkerRegisteredDuringWriteIsInitializedBeforeItsPayload)
{
    using S = I::IntermediateSerializer;
    auto candidate = std::make_unique<RegisteringSerializer>();
    auto* serializer = candidate.get();
    (void)S::RegisterTypeSerializer(std::move(candidate));
    const auto xml = Write(RegisteringOwnerPayload{}, "late.xml");
    EXPECT_NE(xml.find("<Late>late</Late>"), std::string::npos);
    ASSERT_NE(serializer->late, nullptr);
    EXPECT_EQ(serializer->late->initializations.load(), 1);
    ASSERT_NE(serializer->late->owner.load(), nullptr);
    EXPECT_EQ(&serializer->late->owner.load()->GetTypeSerializer(System::Type::From<LateOwnerPayload>()), serializer->late);
}

TEST(IntermediateOwnerIsolationTest, SimultaneousFirstOperationsInitializeNewWorkerOnce)
{
    using S = I::IntermediateSerializer;
    auto candidate = std::make_unique<IsolationSerializer<ColdOwnerPayload>>();
    auto* serializer = candidate.get();
    (void)S::RegisterTypeSerializer(std::move(candidate));
    std::barrier gate(3);
    std::array<std::string, 2> output;
    std::array<std::exception_ptr, 2> errors;
    auto run = [&](int slot)
    {
        gate.arrive_and_wait();
        try
        {
            const auto path = "cold/" + std::to_string(slot) + ".xml";
            output[slot] = Write(ColdOwnerPayload{"same", path}, path);
        }
        catch (...) { errors[slot] = std::current_exception(); }
    };
    std::thread first(run, 0), second(run, 1);
    gate.arrive_and_wait();
    first.join();
    second.join();
    EXPECT_EQ(errors[0], nullptr);
    EXPECT_EQ(errors[1], nullptr);
    EXPECT_FALSE(output[0].empty());
    EXPECT_EQ(output[0], output[1]);
    EXPECT_EQ(serializer->initializations.load(), 1);
    ASSERT_NE(serializer->owner.load(), nullptr);
    EXPECT_EQ(&serializer->owner.load()->GetTypeSerializer(System::Type::From<ColdOwnerPayload>()), serializer);
}

TEST(IntermediateOwnerIsolationTest, NestedOperationRestoresOuterContextAndKeepsOwner)
{
    using S = I::IntermediateSerializer;
    (void)S::TypeSerializerFor<std::string>();
    auto candidate = std::make_unique<IsolationSerializer<NestedOwnerPayload>>();
    auto* serializer = candidate.get();
    (void)S::RegisterTypeSerializer(std::move(candidate));
    const auto first = Write(NestedOwnerPayload{"same", "outer/first.xml"}, "outer/first.xml");
    const auto second = Write(NestedOwnerPayload{"same", "outer/second.xml"}, "outer/second.xml");
    EXPECT_EQ(first, second);
    EXPECT_EQ(serializer->initializations.load(), 1);
    ASSERT_NE(serializer->owner.load(), nullptr);
    EXPECT_EQ(&serializer->owner.load()->GetTypeSerializer(System::Type::From<NestedOwnerPayload>()), serializer);
}

TEST(IntermediateOwnerIsolationTest, ParallelOperationsShareOwnerButNotRelocationState)
{
    using S = I::IntermediateSerializer;
    (void)S::TypeSerializerFor<std::string>();
    auto candidate = std::make_unique<IsolationSerializer<ParallelOwnerPayload>>();
    auto* serializer = candidate.get();
    (void)S::RegisterTypeSerializer(std::move(candidate));
    // Finish discovery/initialization before testing simultaneous operations.
    (void)Write(ParallelOwnerPayload{"warmup", "warmup.xml", -1}, "warmup.xml");
    std::barrier gate(2);
    serializer->gate = &gate;
    std::array<std::string, 2> output;
    std::array<std::exception_ptr, 2> errors;
    auto run = [&](int slot)
    {
        try
        {
            const auto path = "parallel/" + std::to_string(slot) + ".xml";
            output[slot] = Write(ParallelOwnerPayload{"same", path, slot}, path);
        }
        catch (...)
        {
            errors[slot] = std::current_exception();
            if (!serializer->reached[slot].load()) gate.arrive_and_drop();
        }
    };
    std::thread first(run, 0), second(run, 1);
    first.join();
    second.join();
    serializer->gate = nullptr;
    EXPECT_EQ(errors[0], nullptr);
    EXPECT_EQ(errors[1], nullptr);
    EXPECT_FALSE(output[0].empty());
    EXPECT_EQ(output[0], output[1]);
    EXPECT_EQ(serializer->observed[0], serializer->observed[1]);
    EXPECT_EQ(serializer->initializations.load(), 1);
    EXPECT_NE(serializer->owner.load(), nullptr);
}
}
