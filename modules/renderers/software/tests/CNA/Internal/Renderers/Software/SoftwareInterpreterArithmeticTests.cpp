// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_SOFTWARE) && defined(CNA_SOFTWARE_COMPILED_EFFECTS)
#include "CNA/Internal/Renderers/Software/SoftwareCompiledEffect.hpp"
#include <gtest/gtest.h>
#include <array>
#include <stdexcept>
using namespace CNA::Internal::Renderers::Software;
namespace {
struct ArithmeticCase { std::uint16_t opcode; int operands; std::array<float,4> expected; };
// Direct3D arithmetic instruction definitions, evaluated by hand for
// c0=(2,-3,.5,4), c1=(5,2,-4,.25), c2=(1,-2,3,-4).
// E.g. DP3=2*5-3*2+.5*(-4)=2; DP4 adds 4*.25 -> 3.
const ArithmeticCase cases[]={
    {1,1,{2,-3,.5f,4}}, {2,2,{7,-1,-3.5f,4.25f}}, {3,2,{-3,-5,4.5f,3.75f}},
    {4,3,{11,-8,1,-3}}, {5,2,{10,-6,-2,1}}, {6,1,{.5f,.5f,.5f,.5f}},
    {8,2,{2,2,2,2}}, {9,2,{3,3,3,3}}, {10,2,{2,-3,-4,.25f}},
    {11,2,{5,2,.5f,4}}, {12,2,{1,1,0,0}}, {13,2,{0,0,1,1}},
    {15,1,{1,1,1,1}}, {17,2,{1,-6,.5f,.25f}}, {18,3,{9,-14,-.5f,13}},
    {19,1,{0,0,.5f,0}}, {32,2,{32,32,32,32}}, {33,2,{11,10.5f,19,1}},
    {35,1,{2,3,.5f,4}}
};
const std::array<float,12> constants{2,-3,.5f,4, 5,2,-4,.25f, 1,-2,3,-4};
SoftwareShaderProgramEXT Program(const ArithmeticCase& test, bool pixel) {
    SoftwareShaderProgramEXT program;
    program.stage=pixel?SoftwareShaderStageEXT::Pixel:SoftwareShaderStageEXT::Vertex;
    program.majorVersion=2;
    SoftwareShaderInstructionEXT instruction;
    instruction.opcode=test.opcode;
    // D3D9 register token: bit31, type split across bits28..30 and bits11..12,
    // destination mask xyzw=0xf; source identity swizzle=0xe4. oC0=type8, oPos=type4.
    instruction.tokens={static_cast<std::uint32_t>(test.opcode),pixel?0x800f0800u:0xc00f0000u};
    for(int i=0;i<test.operands;++i) instruction.tokens.push_back(0xa0e40000u+i);
    program.instructions.push_back(instruction);
    return program;
}
}
TEST(SoftwareInterpreterArithmetic, VertexArithmeticHasIndependentFloat4Results) {
    for(const auto& test:cases) {
        SCOPED_TRACE(test.opcode);
        const auto result=ExecuteSoftwareVertexShaderEXT(Program(test,false),constants,{},{},{});
        for(int c=0;c<4;++c) EXPECT_FLOAT_EQ(result.position[c],test.expected[c]);
    }
}
TEST(SoftwareInterpreterArithmetic, PixelArithmeticWritesOnlyColorZeroWithoutDiscardOrDepth) {
    for(const auto& test:cases) {
        SCOPED_TRACE(test.opcode);
        const auto result=ExecuteSoftwarePixelShaderEXT(Program(test,true),constants,{},{},{});
        EXPECT_EQ(result.colorWriteMask,1); EXPECT_FALSE(result.discarded); EXPECT_FALSE(result.depthWritten);
        for(int c=0;c<4;++c) EXPECT_FLOAT_EQ(result.colors[0][c],test.expected[c]);
    }
}
TEST(SoftwareInterpreterArithmetic, UnknownOpcodeRefusesByStageAndNumber) {
    // Interpreter API refuses unsupported operations rather than yielding a zero colour/position.
    const ArithmeticCase unknown{1234,1,{}};
    for(bool pixel:{false,true}) {
        try {
            if(pixel) (void)ExecuteSoftwarePixelShaderEXT(Program(unknown,true),constants,{},{},{});
            else (void)ExecuteSoftwareVertexShaderEXT(Program(unknown,false),constants,{},{},{});
            FAIL()<<"unsupported opcode accepted";
        } catch(const std::runtime_error& error) {
            EXPECT_EQ(std::string(error.what()),pixel?"Software pixel shader: unsupported opcode 1234.":
                "Software vertex shader: unsupported opcode 1234.");
        }
    }
}
#endif
