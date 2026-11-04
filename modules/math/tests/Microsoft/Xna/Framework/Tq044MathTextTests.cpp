// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
using namespace Microsoft::Xna::Framework;

#include "System/Globalization/CultureInfo.hpp"
namespace {
class Tq044CultureGuard {
    System::Globalization::CultureInfo saved=System::Globalization::CultureInfo::getCurrentCultureProperty();
public:
    explicit Tq044CultureGuard(const char* name) {
        System::Globalization::CultureInfo::setCurrentCultureProperty(System::Globalization::CultureInfo(name));
    }
    ~Tq044CultureGuard() {
        System::Globalization::CultureInfo::setCurrentCultureProperty(saved);
    }
};
}

TEST(Tq044MathTextTest, Vector2G7)
{
    // XNA Refresh Vector2.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Vector2(1234567.125f,0.0f).ToString(),"{X:1234567 Y:0}");
}

TEST(Tq044MathTextTest, Vector2Exponent)
{
    // XNA Refresh Vector2.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Vector2(1073741824.0f,0.0f).ToString(),"{X:1.073742E+09 Y:0}");
}

TEST(Tq044MathTextTest, Vector2Special)
{
    // XNA Refresh Vector2.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Vector2(std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()).ToString(),"{X:NaN Y:Infinity}");
}

TEST(Tq044MathTextTest, Vector2CurrentCulture)
{
    // XNA Refresh Vector2.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("de-DE");
    EXPECT_EQ(Vector2(1.5f,-2.25f).ToString(),"{X:1,5 Y:-2,25}");
}

TEST(Tq044MathTextTest, Vector3G7)
{
    // XNA Refresh Vector3.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Vector3(1234567.125f,0.0f,0.0f).ToString(),"{X:1234567 Y:0 Z:0}");
}

TEST(Tq044MathTextTest, Vector3Exponent)
{
    // XNA Refresh Vector3.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Vector3(1073741824.0f,0.0f,0.0f).ToString(),"{X:1.073742E+09 Y:0 Z:0}");
}

TEST(Tq044MathTextTest, Vector3Special)
{
    // XNA Refresh Vector3.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Vector3(std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()).ToString(),"{X:NaN Y:Infinity Z:-Infinity}");
}

TEST(Tq044MathTextTest, Vector3CurrentCulture)
{
    // XNA Refresh Vector3.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("de-DE");
    EXPECT_EQ(Vector3(1.5f,-2.25f,0.0f).ToString(),"{X:1,5 Y:-2,25 Z:0}");
}

TEST(Tq044MathTextTest, Vector4G7)
{
    // XNA Refresh Vector4.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Vector4(1234567.125f,0.0f,0.0f,0.0f).ToString(),"{X:1234567 Y:0 Z:0 W:0}");
}

TEST(Tq044MathTextTest, Vector4Exponent)
{
    // XNA Refresh Vector4.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Vector4(1073741824.0f,0.0f,0.0f,0.0f).ToString(),"{X:1.073742E+09 Y:0 Z:0 W:0}");
}

TEST(Tq044MathTextTest, Vector4Special)
{
    // XNA Refresh Vector4.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Vector4(std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),-0.0f).ToString(),"{X:NaN Y:Infinity Z:-Infinity W:0}");
}

TEST(Tq044MathTextTest, Vector4CurrentCulture)
{
    // XNA Refresh Vector4.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("de-DE");
    EXPECT_EQ(Vector4(1.5f,-2.25f,0.0f,0.0f).ToString(),"{X:1,5 Y:-2,25 Z:0 W:0}");
}

TEST(Tq044MathTextTest, QuaternionG7)
{
    // XNA Refresh Quaternion.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Quaternion(1234567.125f,0.0f,0.0f,0.0f).ToString(),"{X:1234567 Y:0 Z:0 W:0}");
}

TEST(Tq044MathTextTest, QuaternionExponent)
{
    // XNA Refresh Quaternion.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Quaternion(1073741824.0f,0.0f,0.0f,0.0f).ToString(),"{X:1.073742E+09 Y:0 Z:0 W:0}");
}

TEST(Tq044MathTextTest, QuaternionSpecial)
{
    // XNA Refresh Quaternion.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Quaternion(std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),-0.0f).ToString(),"{X:NaN Y:Infinity Z:-Infinity W:0}");
}

TEST(Tq044MathTextTest, QuaternionCurrentCulture)
{
    // XNA Refresh Quaternion.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("de-DE");
    EXPECT_EQ(Quaternion(1.5f,-2.25f,0.0f,0.0f).ToString(),"{X:1,5 Y:-2,25 Z:0 W:0}");
}

TEST(Tq044MathTextTest, MatrixG7)
{
    // XNA Refresh Matrix.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Matrix(1234567.125f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f).ToString(),"{ {M11:1234567 M12:0 M13:0 M14:0} {M21:0 M22:0 M23:0 M24:0} {M31:0 M32:0 M33:0 M34:0} {M41:0 M42:0 M43:0 M44:0} }");
}

TEST(Tq044MathTextTest, MatrixExponent)
{
    // XNA Refresh Matrix.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Matrix(1073741824.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f).ToString(),"{ {M11:1.073742E+09 M12:0 M13:0 M14:0} {M21:0 M22:0 M23:0 M24:0} {M31:0 M32:0 M33:0 M34:0} {M41:0 M42:0 M43:0 M44:0} }");
}

TEST(Tq044MathTextTest, MatrixSpecial)
{
    // XNA Refresh Matrix.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("");
    EXPECT_EQ(Matrix(std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),-0.0f,0,0,0,0,0,0,0,0,0,0,0,0).ToString(),"{ {M11:NaN M12:Infinity M13:-Infinity M14:0} {M21:0 M22:0 M23:0 M24:0} {M31:0 M32:0 M33:0 M34:0} {M41:0 M42:0 M43:0 M44:0} }");
}

TEST(Tq044MathTextTest, MatrixCurrentCulture)
{
    // XNA Refresh Matrix.ToString IL_0000: CurrentCulture; Single default uses Framework G7.
    Tq044CultureGuard culture("de-DE");
    EXPECT_EQ(Matrix(1.5f,-2.25f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f).ToString(),"{ {M11:1,5 M12:-2,25 M13:0 M14:0} {M21:0 M22:0 M23:0 M24:0} {M31:0 M32:0 M33:0 M34:0} {M41:0 M42:0 M43:0 M44:0} }");
}
