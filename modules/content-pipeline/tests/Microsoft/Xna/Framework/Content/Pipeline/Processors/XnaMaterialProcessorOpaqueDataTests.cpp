// SPDX-License-Identifier: MS-PL
//
// TQ-034: MaterialProcessor.ValidateOpaqueData. An EffectMaterialContent's opaque data becomes
// effect parameters, so XNA refuses any entry an EffectParameter cannot be set to -- after the
// effect is built (Process IL_00fb-010f), under the material's identity (ValidateOpaqueData
// IL_0086-008d), skipping Effect and CompiledEffect (IL_0018-003c), and accepting int, bool, string,
// float, Vector2, Vector3, Vector4 and Matrix (IsValidTypeForEffectParameter IL_0028-00bc).
// Messages and outcomes: TQ-034 lane oracle, cases material/*. CNA validated nothing.
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "XnaProcessorTestContext.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/StockMaterials.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/MaterialProcessor.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

namespace
{
    namespace Pipeline = Microsoft::Xna::Framework::Content::Pipeline;
    namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
    using CNA::Tests::XnaProcessors::RecordingContext;
    using CNA::Tests::XnaProcessors::Refusal;
    using CNA::Tests::XnaProcessors::RefusalOf;
    using CNA::Tests::XnaProcessors::Spell;

    struct Outcome
    {
        Refusal refusal;
        std::vector<std::string> requests;
    };

    template<typename T>
    Outcome ProcessWith(const T& value)
    {
        auto material = std::make_shared<Graphics::EffectMaterialContent>();
        material->setNameProperty("Mat");
        material->setIdentityProperty(Pipeline::ContentIdentity("model.fbx", "FbxImporter", "Mat"));
        material->setEffectProperty(std::make_shared<Pipeline::ExternalReference<Graphics::EffectContent>>("custom.fx"));
        material->getOpaqueDataProperty().SetValue<T>("Value", value);
        RecordingContext context;
        Outcome outcome;
        outcome.refusal = RefusalOf([&]
        {
            Pipeline::Processors::MaterialProcessor processor;
            (void)processor.Process(material, context);
        });
        outcome.requests = context.requests;
        return outcome;
    }

    std::string Refused(const std::string& type)
    {
        return "The EffectMaterialContent named \"Mat\" has an invalid parameter in its OpaqueData. The parameter is "
               "named \"Value\", and is of the type " + type + ", which is invalid. All types must be valid input for "
               "EffectParameter.SetValue or EffectParameter.SetValueArray. Valid types are int, bool, float, Vector2, "
               "Vector3, Vector4, Matrix, arrays of those types, and strings.";
    }
}

TEST(XnaMaterialProcessorOpaqueData, AnEntryNoEffectParameterCanTakeIsRefusedUnderTheMaterialsIdentity)
{
    const Outcome colour = ProcessWith<Microsoft::Xna::Framework::Color>(Microsoft::Xna::Framework::Color(1, 2, 3, 4));
    EXPECT_EQ(colour.refusal.message, Refused("Microsoft.Xna.Framework.Color"));  // material/opaque_color
    EXPECT_EQ(Spell(colour.refusal.identity), "{model.fbx|FbxImporter|Mat}");
    // The effect was already built when the refusal came.
    EXPECT_EQ(colour.requests, std::vector<std::string>{"custom.fx->EffectProcessor"});

    EXPECT_EQ(ProcessWith<Microsoft::Xna::Framework::Quaternion>(Microsoft::Xna::Framework::Quaternion(0, 0, 0, 1))
                  .refusal.message,
              Refused("Microsoft.Xna.Framework.Quaternion"));  // material/opaque_quaternion
    EXPECT_EQ(ProcessWith<double>(1.5).refusal.message, Refused("System.Double"));  // material/opaque_double
}

TEST(XnaMaterialProcessorOpaqueData, EveryEffectParameterTypeIsAccepted)
{
    // material/opaque_single, _int, _bool, _string, _vector2, _vector3, _vector4, _matrix: built.
    EXPECT_EQ(ProcessWith<float>(1.5f).refusal.message, "built");
    EXPECT_EQ(ProcessWith<SharpRuntime::intcs>(7).refusal.message, "built");
    EXPECT_EQ(ProcessWith<bool>(true).refusal.message, "built");
    EXPECT_EQ(ProcessWith<std::string>("text").refusal.message, "built");
    EXPECT_EQ(ProcessWith<Microsoft::Xna::Framework::Vector2>(Microsoft::Xna::Framework::Vector2(1, 1)).refusal.message, "built");
    EXPECT_EQ(ProcessWith<Microsoft::Xna::Framework::Vector3>(Microsoft::Xna::Framework::Vector3(1, 1, 1)).refusal.message, "built");
    EXPECT_EQ(ProcessWith<Microsoft::Xna::Framework::Vector4>(Microsoft::Xna::Framework::Vector4(1, 1, 1, 1)).refusal.message,
              "built");
    EXPECT_EQ(ProcessWith<Microsoft::Xna::Framework::Matrix>(Microsoft::Xna::Framework::Matrix::getIdentityProperty())
                  .refusal.message,
              "built");
}

TEST(XnaMaterialProcessorOpaqueData, OnlyAnEffectMaterialIsValidated)
{
    // material/basic_opaque_color_not_validated: a stock material's opaque data is the processor's
    // own business, and a Color there is built.
    auto material = std::make_shared<Graphics::BasicMaterialContent>();
    material->setNameProperty("Mat");
    material->getOpaqueDataProperty().SetValue<Microsoft::Xna::Framework::Color>("Value",
                                                                                   Microsoft::Xna::Framework::Color(1, 2, 3, 4));
    RecordingContext context;
    const Refusal refusal = RefusalOf([&]
    {
        Pipeline::Processors::MaterialProcessor processor;
        (void)processor.Process(material, context);
    });
    EXPECT_EQ(refusal.message, "built");
}
