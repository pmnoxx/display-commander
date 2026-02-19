#ifndef PMNOX_COLOR_HLSL
#define PMNOX_COLOR_HLSL



static const float3x3 BT709_TO_XYZ_MAT = float3x3(
    0.4123907993f, 0.3575843394f, 0.1804807884f,
    0.2126390059f, 0.7151686788f, 0.0721923154f,
    0.0193308187f, 0.1191947798f, 0.9505321522f);

static const float3x3 XYZ_TO_BT709_MAT = float3x3(
    3.2409699419f, -1.5373831776f, -0.4986107603f,
    -0.9692436363f, 1.8759675015f, 0.0415550574f,
    0.0556300797f, -0.2039769589f, 1.0569715142f);

static const float3x3 BT2020_TO_XYZ_MAT = float3x3(
    0.6369580483f, 0.1446169036f, 0.1688809752f,
    0.2627002120f, 0.6779980715f, 0.0593017165f,
    0.0000000000f, 0.0280726930f, 1.0609850577f);

static const float3x3 XYZ_TO_BT2020_MAT = float3x3(
    1.7166511880f, -0.3556707838f, -0.2533662814f,
    -0.6666843518f, 1.6164812366f, 0.0157685458f,
    0.0176398574f, -0.0427706133f, 0.9421031212f);

static const float3x3 AP0_TO_XYZ_MAT = float3x3(
    0.9525523959f, 0.0000000000f, 0.0000936786f,
    0.3439664498f, 0.7281660966f, -0.0721325464f,
    0.0000000000f, 0.0000000000f, 1.0088251844f);

static const float3x3 XYZ_TO_AP0_MAT = float3x3(
    1.0498110175f, 0.0000000000f, -0.0000974845f,
    -0.4959030231f, 1.3733130458f, 0.0982400361f,
    0.0000000000f, 0.0000000000f, 0.9912520182f);

static const float3x3 AP1_TO_XYZ_MAT = float3x3(
    0.6624541811f, 0.1340042065f, 0.1561876870f,
    0.2722287168f, 0.6740817658f, 0.0536895174f,
    -0.0055746495f, 0.0040607335f, 1.0103391003f);

static const float3x3 XYZ_TO_AP1_MAT = float3x3(
    1.6410233797f, -0.3248032942f, -0.2364246952f,
    -0.6636628587f, 1.6153315917f, 0.0167563477f,
    0.0117218943f, -0.0082844420f, 0.9883948585f);

static const float3x3 XYZ_TO_ICTCP_LMS_MAT = float3x3(
    0.359168797f, 0.697604775f, -0.0357883982f,
    -0.192186400f, 1.10039842f, 0.0755404010f,
    0.00695759989f, 0.0749168023f, 0.843357980f);

static const float3x3 ICTCP_LMS_TO_XYZ_MAT = float3x3(
    2.07036161f, -1.32659053f, 0.206681042f,
    0.364990383f, 0.680468797f, -0.0454616732f,
    -0.0495028905f, -0.0495028905f, 1.18806946f);

static const float3x3 BT709_TO_ICTCP_LMS_MAT = float3x3(
    0.295764088f, 0.623072445f, 0.0811667516f,
    0.156191974f, 0.727251648f, 0.116557933f,
    0.0351022854f, 0.156589955f, 0.808302998f);

static const float3x3 ICTCP_LMS_TO_BT709_MAT = float3x3(
    6.17353248f, -5.32089900f, 0.147354885f,
    -1.32403194f, 2.56026983f, -0.236238613f,
    -0.0115983877f, -0.264921456f, 1.27652633f);

static const float3x3 DISPLAYP3_TO_XYZ_MAT = float3x3(
    0.4865709486f, 0.2656676932f, 0.1982172852f,
    0.2289745641f, 0.6917385218f, 0.0792869141f,
    0.0000000000f, 0.0451133819f, 1.0439443689f);

static const float3x3 XYZ_TO_DISPLAYP3_MAT = float3x3(
    2.4934969119f, -0.9313836179f, -0.4027107845f,
    -0.8294889696f, 1.7626640603f, 0.0236246858f,
    0.0358458302f, -0.0761723893f, 0.9568845240);

static const float3x3 BT470_PAL_TO_BT709_MAT = float3x3(
    1.04404318f, -0.0440432094f, 0.f,
    0.f, 1.f, 0.f,
    0.f, 0.0117933787f, 0.988206624f);

static const float3x3 BT601_NTSC_U_TO_BT709_MAT = float3x3(
    0.939542055f, 0.0501813553f, 0.0102765792f,
    0.0177722238f, 0.965792834f, 0.0164349135f,
    -0.00162159989f, -0.00436974968f, 1.00599133f);

static const float3x3 NTSC_U_1953_TO_XYZ_MAT = float3x3(
    0.6068638093f, 0.1735072810f, 0.2003348814f,
    0.2989030703f, 0.5866198547f, 0.1144770751f,
    -0.0000000000f, 0.0660980118f, 1.1161514821f);

// chromatic adaptation method: vK20
// chromatic adaptation transform: CAT02
static const float3x3 ARIB_TR_B9_D93_TO_BT709_D65_MAT = float3x3(
    0.897676467f, -0.129552796f, 0.00210331683f,
    0.0400346256f, 0.970825016f, 0.00575808621f,
    0.00136304146f, 0.0323694758f, 1.48031127f);

// chromatic adaptation method: vK20
// chromatic adaptation transform: CAT02
static const float3x3 ARIB_TR_B9_9300K_8_MPCD_TO_BT709_D65_MAT = float3x3(
    0.898766815f, -0.129979714f, 0.00223334273f,
    0.0401015169f, 0.972650587f, 0.00570049602f,
    0.00123276025f, 0.0316823720f, 1.45910859f);

// chromatic adaptation method: vK20
// chromatic adaptation transform: CAT02
static const float3x3 ARIB_TR_B9_9300K_27_MPCD_TO_BT709_D65_MAT = float3x3(
    0.783664464f, -0.178418442f, 0.00223907502f,
    0.0380520112f, 1.03919935f, 0.00543892197f,
    0.000365949701f, 0.0269012674f, 1.31387364f);

// chromatic adaptation method: vK20
// chromatic adaptation transform: CAT02
static const float3x3 BT709_D93_TO_BT709_D65_MAT = float3x3(
    0.968665063f, -0.0445920750f, -0.00335013796f,
    0.00231231073f, 1.00339293f, 0.0000867190974f,
    0.00326244067f, 0.0161521788f, 1.11353743f);

// chromatic adaptation method: von Kries
// chromatic adaptation transform: Bradford
static const float3x3 D65_TO_D60_CAT = float3x3(
    1.01303493f, 0.00610525766f, -0.0149709433f,
    0.00769822997f, 0.998163342f, -0.00503203831f,
    -0.00284131732f, 0.00468515651f, 0.924506127f);

// chromatic adaptation method: von Kries
// chromatic adaptation transform: Bradford
static const float3x3 D60_TO_D65_MAT = float3x3(
    0.987223982f, -0.00611322838f, 0.0159532874f,
    -0.00759837171f, 1.00186145f, 0.00533003592f,
    0.00307257706f, -0.00509596150f, 1.08168065f);

static const float3x3 IDENTITY_MAT = float3x3(
    1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f);

//static const float3x3 BT709_TO_AP0_MAT = mul(XYZ_TO_AP0_MAT, mul(D65_TO_D60_CAT, BT709_TO_XYZ_MAT));

// With Bradford
static const float3x3 BT709_TO_AP1_MAT = float3x3(
    0.6130974024, 0.3395231462, 0.0473794514,
    0.0701937225, 0.9163538791, 0.0134523985,
    0.0206155929, 0.1095697729, 0.8698146342);

// With Bradford
static const float3x3 BT2020_TO_AP1_MAT = float3x3(
    0.9748949779f, 0.0195991086f, 0.0055059134f,
    0.0021795628f, 0.9955354689f, 0.0022849683f,
    0.0047972397f, 0.0245320166f, 0.9706707437f);

// BT2020 to BT709 conversion matrix
static const float3x3 BT2020_TO_BT709_MAT = float3x3(
    1.66049621914783, -0.587656444131135, -0.0728397750166941,
    -0.124547095586012, 1.13289510924730, -0.00834801366128445,
    -0.0181536813870718, -0.100597371685743, 1.11875105307281);

float3x3 BT709_TO_BT2020_MAT() {return mul(XYZ_TO_BT2020_MAT, BT709_TO_XYZ_MAT);}
/*
static const float3x3 BT709_TO_BT709D60_MAT = mul(XYZ_TO_BT709_MAT, mul(D65_TO_D60_CAT, BT709_TO_XYZ_MAT));
static const float3x3 BT709_TO_BT2020D60_MAT = mul(XYZ_TO_BT2020_MAT, mul(D65_TO_D60_CAT, BT709_TO_XYZ_MAT));*/
float3x3 BT709_TO_DISPLAYP3_MAT() { return mul(XYZ_TO_DISPLAYP3_MAT, BT709_TO_XYZ_MAT); }
/*static const float3x3 BT709_TO_DISPLAYP3D60_MAT = mul(XYZ_TO_DISPLAYP3_MAT, mul(D65_TO_D60_CAT, BT709_TO_XYZ_MAT));

static const float3x3 BT2020_TO_AP0_MAT = mul(XYZ_TO_AP0_MAT, mul(D65_TO_D60_CAT, BT2020_TO_XYZ_MAT));
static const float3x3 BT2020_TO_BT709_MAT = mul(XYZ_TO_BT709_MAT, BT2020_TO_XYZ_MAT);

static const float3x3 DISPLAYP3_TO_AP0_MAT = mul(XYZ_TO_AP0_MAT, mul(D65_TO_D60_CAT, DISPLAYP3_TO_XYZ_MAT));
*/
float3x3 DISPLAYP3_TO_BT709_MAT() { return mul(XYZ_TO_BT709_MAT, DISPLAYP3_TO_XYZ_MAT); }
/*
static const float3x3 AP0_TO_AP1_MAT = mul(XYZ_TO_AP1_MAT, AP0_TO_XYZ_MAT);

static const float3x3 AP1_TO_AP0_MAT = mul(XYZ_TO_AP0_MAT, AP1_TO_XYZ_MAT);
*/
// With Bradford
static const float3x3 AP1_TO_BT709_MAT = float3x3(
    1.7050509927, -0.6217921207, -0.0832588720,
    -0.1302564175, 1.1408047366, -0.0105483191,
    -0.0240033568, -0.1289689761, 1.1529723329);

// With Bradford
static const float3x3 AP1_TO_BT2020_MAT = float3x3(
    1.0258247477f, -0.0200531908f, -0.0057715568f,
    -0.0022343695f, 1.0045865019f, -0.0023521324f,
    -0.0050133515f, -0.0252900718f, 1.0303034233f);

/*
static const float3x3 AP1_TO_BT709D60_MAT = mul(XYZ_TO_BT709_MAT, AP1_TO_XYZ_MAT);
static const float3x3 AP1_TO_BT2020D60_MAT = mul(XYZ_TO_BT2020_MAT, AP1_TO_XYZ_MAT);
static const float3x3 AP1_TO_AP1D65_MAT = mul(XYZ_TO_AP1_MAT, mul(D60_TO_D65_MAT, AP1_TO_XYZ_MAT));
*/

// Color space constants (RenoDX-style)
static const uint COLOR_SPACE_BT709 = 0;
static const uint COLOR_SPACE_BT2020 = 1;
static const uint COLOR_SPACE_P3 = 2;
static const uint COLOR_SPACE_CUSTOM_BT709D93 = 3;
static const uint COLOR_SPACE_CUSTOM_NTSCU = 4;
static const uint COLOR_SPACE_CUSTOM_NTSCJ = 5;

namespace BT709 {
    namespace from {
        float3 XYZ(float3 XYZ) {
            return mul(XYZ_TO_BT709_MAT, XYZ);
        }

        float3 AP1(float3 ap1) {
            return mul(AP1_TO_BT709_MAT, ap1);
        }

        float3 BT2020(float3 bt2020) {
            return mul(BT2020_TO_BT709_MAT, bt2020);
           // return bt2020; // TODO
        }

        float3 BT601NTSCU(float3 bt601) {
            return mul(BT601_NTSC_U_TO_BT709_MAT, bt601);
        }

        float3 ARIBTRB9(float3 aribtrb9) {
            return mul(ARIB_TR_B9_D93_TO_BT709_D65_MAT, aribtrb9);
        }

        float3 ARIBTRB98MPCD(float3 aribtrb9) {
           return mul(ARIB_TR_B9_9300K_8_MPCD_TO_BT709_D65_MAT, aribtrb9);
        }

        float3 ARIBTRB927MPCD(float3 aribtrb9) {
            return mul(ARIB_TR_B9_9300K_27_MPCD_TO_BT709_D65_MAT, aribtrb9);
        }

        float3 BT709D93(float3 bt709d93) {
            return mul(BT709_D93_TO_BT709_D65_MAT, bt709d93);
        }

        float3 P3(float3 p3display) {
            return mul(DISPLAYP3_TO_BT709_MAT(), p3display);
        }
    }
}

namespace BT2020 {
    namespace from {
        float3 BT709(float3 color) {
            return mul(BT709_TO_BT2020_MAT(), color);
        }
    }
}

namespace P3 {
    namespace from {
        float3 BT709(float3 color) {
            return mul(BT709_TO_DISPLAYP3_MAT(), color);
        }
    }
}

namespace color {
    namespace pq {
        // PQ (Perceptual Quantizer) constants
        static const float M1 = 2610.f / 16384.f;          // 0.1593017578125f;
        static const float M2 = 128.f * (2523.f / 4096.f); // 78.84375f;
        static const float C1 = 3424.f / 4096.f;           // 0.8359375f;
        static const float C2 = 32.f * (2413.f / 4096.f);  // 18.8515625f;
        static const float C3 = 32.f * (2392.f / 4096.f);  // 18.6875f;

        // Standard PQ encode function
        float4 Encode4(float4 color, float scaling = 10000.f) {
            color *= (scaling / 10000.f);
            float4 y_m1 = pow(color, M1);
            return pow((C1 + C2 * y_m1) / (1.f + C3 * y_m1), M2);
        }

        // Standard PQ decode function
        float4 Decode4(float4 color, float scaling = 10000.f) {
            float4 e_m12 = pow(color, 1.f / M2);
            float4 out_color = pow(max(0, e_m12 - C1) / (C2 - C3 * e_m12), 1.f / M1);
            return out_color * (10000.f / scaling);
        }

        // Standard PQ encode function
        float3 Encode(float3 color, float scaling = 10000.f) {
            color *= (scaling / 10000.f);
            float3 y_m1 = pow(color, M1);
            return pow((C1 + C2 * y_m1) / (1.f + C3 * y_m1), M2);
        }

        // Standard PQ decode function
        float3 Decode(float3 color, float scaling = 10000.f) {
            float3 e_m12 = pow(color, 1.f / M2);
            float3 out_color = pow(max(0, e_m12 - C1) / (C2 - C3 * e_m12), 1.f / M1);
            return out_color * (10000.f / scaling);
        }

        // Safe encode function that handles edge cases
        float3 EncodeSafe(float3 color, float scaling = 10000.f) {
            // Clamp input to valid range
            color = max(0, color);
            
            // Handle very small values to avoid numerical issues
            float3 epsilon = 1e-10;
            color = max(color, epsilon);
            
            return Encode(color, scaling);
        }

        // Safe decode function that handles edge cases
        float3 DecodeSafe(float3 color, float scaling = 10000.f) {
            // Clamp input to valid PQ range [0, 1]
            color = saturate(color);
            
            // Handle edge cases where the denominator could be zero
            float3 e_m12 = pow(color, 1.f / M2);
            float3 denominator = C2 - C3 * e_m12;
            
            // Avoid division by zero
            float3 epsilon = 1e-10;
            denominator = max(denominator, epsilon);
            
            float3 numerator = max(0, e_m12 - C1);
            float3 out_color = pow(numerator / denominator, 1.f / M1);
            
            return out_color * (10000.f / scaling);
        }
    }

    namespace sRGB {
        // sRGB encoding function
        float3 encode(float3 color) {
            float3 result;
            result.r = color.r <= 0.0031308f ? 12.92f * color.r : 1.055f * pow(color.r, 1.f / 2.4f) - 0.055f;
            result.g = color.g <= 0.0031308f ? 12.92f * color.g : 1.055f * pow(color.g, 1.f / 2.4f) - 0.055f;
            result.b = color.b <= 0.0031308f ? 12.92f * color.b : 1.055f * pow(color.b, 1.f / 2.4f) - 0.055f;
            return result;
        }

        // sRGB decoding function
        float3 decode(float3 color) {
            float3 result;
            result.r = color.r <= 0.04045f ? color.r / 12.92f : pow((color.r + 0.055f) / 1.055f, 2.4f);
            result.g = color.g <= 0.04045f ? color.g / 12.92f : pow((color.g + 0.055f) / 1.055f, 2.4f);
            result.b = color.b <= 0.04045f ? color.b / 12.92f : pow((color.b + 0.055f) / 1.055f, 2.4f);
            return result;
        }

        // Safe sRGB encoding that preserves sign but doesn't saturate
        float3 encodeSafe(float3 color) {
            float3 sign = sign(color);
            float3 absColor = abs(color);
            
            float3 result;
            result.r = absColor.r <= 0.0031308f ? 12.92f * absColor.r : 1.055f * pow(absColor.r, 1.f / 2.4f) - 0.055f;
            result.g = absColor.g <= 0.0031308f ? 12.92f * absColor.g : 1.055f * pow(absColor.g, 1.f / 2.4f) - 0.055f;
            result.b = absColor.b <= 0.0031308f ? 12.92f * absColor.b : 1.055f * pow(absColor.b, 1.f / 2.4f) - 0.055f;
            
            return sign * result;
        }

        // Safe sRGB decoding that preserves sign but doesn't saturate
        float3 decodeSafe(float3 color) {
            float3 sign = sign(color);
            float3 absColor = abs(color);
            
            float3 result;
            result.r = absColor.r <= 0.04045f ? absColor.r / 12.92f : pow((absColor.r + 0.055f) / 1.055f, 2.4f);
            result.g = absColor.g <= 0.04045f ? absColor.g / 12.92f : pow((absColor.g + 0.055f) / 1.055f, 2.4f);
            result.b = absColor.b <= 0.04045f ? absColor.b / 12.92f : pow((absColor.b + 0.055f) / 1.055f, 2.4f);
            
            return sign * result;
        }
    }

    namespace gamma {
        // Gamma encoding function
        float3 encode(float3 color, float gamma = 2.2f) {
            return pow(color, 1.f / gamma);
        }

        // Gamma decoding function
        float3 decode(float3 color, float gamma = 2.2f) {
            return pow(color, gamma);
        }

        // Safe gamma encoding that preserves sign but doesn't saturate
        float3 encodeSafe(float3 color, float gamma = 2.2f) {
            float3 sign = sign(color);
            float3 absColor = abs(color);
            return sign * pow(absColor, 1.f / gamma);
        }

        // Safe gamma decoding that preserves sign but doesn't saturate
        float3 decodeSafe(float3 color, float gamma = 2.2f) {
            float3 sign = sign(color);
            float3 absColor = abs(color);
            return sign * pow(absColor, gamma);
        }
    }
    
    namespace oklab {
        namespace from {
            static const float3x3 BT709_2_OKLABLMS = float3x3(
                0.4122214708f, 0.5363325363f, 0.0514459929f,
                0.2119034982f, 0.6806995451f, 0.1073969566f,
                0.0883024619f, 0.2817188376f, 0.6299787005f
            );
            
            static const float3x3 OKLABLMS_2_OKLAB = float3x3(
                0.2104542553f,  0.7936177850f, -0.0040720468f,
                1.9779984951f, -2.4285922050f,  0.4505937099f,
                0.0259040371f,  0.7827717662f, -0.8086757660f
            );

            float3 BT709(float3 bt709) { 
                float3 lms = mul(BT709_2_OKLABLMS, bt709);
                lms = sign(lms) * pow(abs(lms), 1.f / 3.f);
                return mul(OKLABLMS_2_OKLAB, lms);
            }

            float3 BT2020(float3 bt2020) {
                return BT709(BT709::from::BT2020(bt2020));
            }
        }

        namespace to {
            static const float3x3 OKLAB_2_OKLABLMS = float3x3( 
                1.f,  0.3963377774f,  0.2158037573f,
                1.f, -0.1055613458f, -0.0638541728f,
                1.f, -0.0894841775f, -1.2914855480f
            );

            static const float3x3 OKLABLMS_2_BT709 = float3x3(
                4.0767416621f, -3.3077115913f,  0.2309699292f,
                -1.2684380046f,  2.6097574011f, -0.3413193965f,
                -0.0041960863f, -0.7034186147f,  1.7076147010f
            );

            float3 BT709(float3 oklab) {
                float3 lms = mul(OKLAB_2_OKLABLMS, oklab);
                lms = lms * lms * lms;
                return mul(OKLABLMS_2_BT709, lms);
            }

            float3 BT2020(float3 oklab) {
                return BT2020::from::BT709(BT709(oklab));
            }
        }
    }

    namespace y {
        namespace from {
            float BT709(float3 bt709) {
                return dot(bt709, BT709_TO_XYZ_MAT[1].rgb);
            }
            
            float BT2020(float3 bt2020) {
                return dot(bt2020, BT2020_TO_XYZ_MAT[1].rgb);
            }
        }
    }

    namespace oklch {
        namespace from {
            float3 BT709(float3 bt709) {
                // Convert to OKLab
                float3 oklab = color::oklab::from::BT709(bt709);
                float L = oklab.x;
                float a = oklab.y;
                float b = oklab.z;
                float C = sqrt(a * a + b * b);
                float H = atan2(b, a); // radians
                return float3(L, C, H);
            }

            float3 BT2020(float3 bt2020) {
                return BT709(BT709::from::BT2020(bt2020));
            }
        }

        namespace to {
            float3 BT709(float3 oklch) {
                float L = oklch.x;
                float C = oklch.y;
                float H = oklch.z;
                float a = C * cos(H);
                float b = C * sin(H);
                float3 oklab = float3(L, a, b);
                return color::oklab::to::BT709(oklab);
            }

            float3 BT2020(float3 oklch) {
                return BT2020::from::BT709(BT709(oklch));
            }
        }
    }
    /*
    namespace ictcp {
        // XYZ to LMS + 4% crosstalk
        static const float CROSSTALK = 0.04f;
        static const float3x3 CROSSTALK_MAT = float3x3(
            1.0f - (2 * CROSSTALK), CROSSTALK, CROSSTALK,
            CROSSTALK, 1.0f - (2 * CROSSTALK), CROSSTALK,
            CROSSTALK, CROSSTALK, 1.0f - (2 * CROSSTALK));

        // Von Kries chromatic adaptation
        static const float3x3 XYZ_TO_LMS_D65_MAT = float3x3(
            0.4002400f, 0.7076000f, -0.0808100f,
            -0.2263000f, 1.1653200f, 0.0457000f,
            0.0000000f, 0.0000000f, 0.9182200f);

        float3x3 XYZ_TO_LMS_MAT() {return mul(XYZ_TO_LMS_D65_MAT, CROSSTALK_MAT); };

        static const float3x3 PLMS_TO_IPT_MAT = float3x3(
            0.4f, 0.4f, 0.2f,
            4.4550f, -4.8510f, 0.3960f,
            0.8056f, 0.3572f, -1.1628f);

        static const float3x3 PLMS_TO_IPT_OPTIMIZED_MAT = float3x3(
            0.5f, 0.5f, 0.0f,
            4.4550f, -4.8510f, 0.3960f,
            0.8056f, 0.3572f, -1.1628f);

        static const float3x3 PLMS_TO_IPT_OPTIMIZED_INV_MAT = float3x3(
            1.f, 0.10572270799388052089f, 0.036004637397296771824f,
            1.f, -0.10572270799388052089f, -0.036004637397296771824f,
            1.f, 0.040768887396333011328f, -0.84610897883647413789f);

        static const float PI = 3.1415926538f;
        static const float VECTORSCOPE_DEGREES = 65.f;
        static const float ROTATION_POINT = VECTORSCOPE_DEGREES * PI / 180.f;

        float3x3 IPT_ROTATION_MAT() { return float3x3(
            1.f, 0, 0.f,
            0, cos(ROTATION_POINT), -sin(ROTATION_POINT),
            0, sin(ROTATION_POINT), cos(ROTATION_POINT));
        }

        float3x3 IPV_ROTATION_INV_MAT() { return float3x3(
            1.f, 0, 0.f,
            0, cos(ROTATION_POINT), sin(ROTATION_POINT),
            0, -sin(ROTATION_POINT), cos(ROTATION_POINT));
        }

        static const float SCALE_FACTOR = 1.4f;
        static const float3x3 IPT_SCALE_MAT = float3x3(
            1.0f, 1.0f, 1.0f,
            SCALE_FACTOR, SCALE_FACTOR, SCALE_FACTOR,
            1.0f, 1.0f, 1.0f);

        static const float3x3 IPT_SCALE_INV_MAT = float3x3(
            1.0f, 1.0f, 1.0f,
            1.0f / SCALE_FACTOR, 1.0f / SCALE_FACTOR, 1.0f / SCALE_FACTOR,
            1.0f, 1.0f, 1.0f);

        float3x3 PLMS_TO_ICTCP_MAT() { return mul(IPT_ROTATION_MAT(), PLMS_TO_IPT_OPTIMIZED_MAT) * IPT_SCALE_MAT; }
        float3x3 ICTCP_TO_PLMS_MAT() { return mul(PLMS_TO_IPT_OPTIMIZED_INV_MAT, IPV_ROTATION_INV_MAT()) * IPT_SCALE_INV_MAT; }

        static const float3x3 ICTCP_LMS_TO_XYZ_MAT = float3x3(
            2.07036161f, -1.32659053f, 0.206681042f,
            0.364990383f, 0.680468797f, -0.0454616732f,
            -0.0495028905f, -0.0495028905f, 1.18806946f);
            
        namespace from {
            float3 BT709(float3 bt709_color, float scaling = 100.f) {
                float3 xyz_color = mul(BT709_TO_XYZ_MAT, bt709_color);
                float3 lms = mul(XYZ_TO_LMS_MAT(), xyz_color);
                float3 pq_color = pq::Encode(max(0, lms), scaling);
                float3 ictcp_color = mul(PLMS_TO_ICTCP_MAT(), pq_color);

                return ictcp_color;
            }
        } 
        namespace to {
            float3 BT709(float3 ictcp_color, float scaling = 100.f) {
                // Inverse of from::BT709
                float3 pq_color = mul(ICTCP_TO_PLMS_MAT(), ictcp_color);
                float3 lms = pq::Decode(max(0, pq_color), scaling);
                float3 xyz_color = mul(ICTCP_LMS_TO_XYZ_MAT, lms); // Inverse of XYZ_TO_LMS_MAT
                return mul(XYZ_TO_BT709_MAT, xyz_color);
            }
        } 
    } */
    
    // namespace ictcp

    namespace ictcp {
        namespace from {
            float3 BT709(float3 bt709_color) {
                float3 lms_color = mul(BT709_TO_ICTCP_LMS_MAT, bt709_color);

                // L'M'S' -> ICtCp
                float3x3 lms_to_ictcp = float3x3(// TODO REDO
                    0.5f, 0.5f, 0.f,
                    1.61370003f, -3.32339620f, 1.70969617f,
                    4.37806224f, -4.24553966f, -0.132522642f
                );

                return mul(lms_to_ictcp, pq::Encode(max(0, lms_color), 100.0f));
            }
        }

        namespace to {
            float3 BT709(float3 col) {
                // ICtCp -> L'M'S'
                float3x3 ictcp_to_lms = float3x3(// TODO REDO
                    1.f, 0.00860647484f, 0.111033529f,
                    1.f, -0.00860647484f, -0.111033529f,
                    1.f, 0.560046315f, -0.320631951f);

                col = mul(ictcp_to_lms, col);

                // 1.0f = 100 nits, 100.0f = 10k nits
                col = pq::DecodeSafe(col, 100.f);
                return mul(ICTCP_LMS_TO_BT709_MAT, col);
            }
        }
    }

    namespace convert {
        float3 ColorSpaces(float3 color, uint from_space, uint to_space) {
            if (from_space == to_space) {
                return color;
            }
            
            if (to_space == COLOR_SPACE_BT709) {
                if (from_space == COLOR_SPACE_BT2020) {
                    return BT709::from::BT2020(color);
                }
                else if (from_space == COLOR_SPACE_P3) {
                    return BT709::from::P3(color);
                } 
            }
            
            return color; 
        }
    }
}

// Inverse Reinhard tonemapping function
// "L_white" of 2 matches simple Reinhard
float3 inv_tonemap_ReinhardPerComponent(float3 L, float L_white /*= 1.0f*/)
{
    // x

    const float3 L2 = L * L;
    const float LW2 = L_white * L_white;
    const float3 LP1 = (0.5f * ((L * LW2) - LW2));
	// It shouldn't be possible for this to be negative (but if it was, put a max() with 0)
    const float3 LP2P1 = LW2 * ((L2 * LW2) - (2.0f * L * LW2) + (4.0f * L) + LW2);
    const float3 LP2 = (0.5f * sqrt(LP2P1));

	// The results can both be negative for some reason (especially on pitch black pixels), so we max against 0.
    const float3 LA = LP1 + LP2;
    L = max(LA, 0.0f);

    return L;
}

// --- OKLCH conversion helpers ---
// Converts linear sRGB (BT.709) to OKLCH
float3 linear_srgb_to_oklch(float3 color) {
    return color::oklch::from::BT709(color);
}

// Converts OKLCH to linear sRGB (BT.709)
float3 oklch_to_linear_srgb(float3 color) {
    return color::oklch::to::BT709(color);
}

// Reverse conversion namespaces for CRT color spaces
namespace BT709D93 {
    namespace from {
        float3 BT709(float3 bt709) {
            // Inverse of BT709_D93_TO_BT709_D65_MAT
            static const float3x3 BT709_TO_BT709D93_MAT = float3x3(
                1.032369f, 0.047189f, 0.003262f,
                -0.002312f, 0.996608f, -0.016152f,
                -0.003262f, -0.000087f, 0.898463f
            );
            return mul(BT709_TO_BT709D93_MAT, bt709);
        }
    }
}

namespace BT601NTSCU {
    namespace from {
        float3 BT709(float3 bt709) {
            // Inverse of BT601_NTSC_U_TO_BT709_MAT
            static const float3x3 BT709_TO_BT601NTSCU_MAT = float3x3(
                1.064439f, -0.055336f, -0.010276f,
                -0.019555f, 1.035456f, -0.016435f,
                0.001621f, 0.004370f, 0.994009f
            );
            return mul(BT709_TO_BT601NTSCU_MAT, bt709);
        }
    }
}

namespace ARIBTRB9 {
    namespace from {
        float3 BT709(float3 bt709) {
            // Inverse of ARIB_TR_B9_D93_TO_BT709_D65_MAT
            static const float3x3 BT709_TO_ARIBTRB9_MAT = float3x3(
                1.114285f, 0.155714f, -0.002103f,
                -0.046034f, 1.029966f, -0.005758f,
                -0.001363f, -0.032369f, 0.675689f
            );
            return mul(BT709_TO_ARIBTRB9_MAT, bt709);
        }
    }
}

#endif // PMNOX_COLOR_HLSL 


namespace XYZ {
namespace from {
float3 BT709(float3 bt709) {
  return mul(BT709_TO_XYZ_MAT, bt709);
}
}  // namespace from
} 

