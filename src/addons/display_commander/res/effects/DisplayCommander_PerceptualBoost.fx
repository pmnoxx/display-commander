/*
SpecialK Perceptual Boost Shader
================================

This shader implements SpecialK's perceptual boost algorithm for HDR enhancement.
The perceptual boost applies a non-linear transformation to luminance values to
improve perceived brightness and contrast in HDR content.

Default parameters (matching SpecialK v1 preset):
- Perceptual Boost 0: 1.0f
- Perceptual Boost 1: 0.1f  
- Perceptual Boost 2: 1.273f
- Perceptual Boost 3: 0.5f
- Color Boost: 0.333f (33.3%)

Based on SpecialK's implementation from:
- resource/shaders/HDR/basic_hdr_shader_ps.hlsl
- resource/shaders/HDR/common_defs.hlsl
*/
#include "color.fxh"


#include "ReShade.fxh"

#if (BUFFER_COLOR_BIT_DEPTH == 10) || BUFFER_COLOR_SPACE == CSP_HDR10
  #define ACTUAL_COLOR_SPACE 2
#elif (BUFFER_COLOR_BIT_DEPTH == 16)
  #define ACTUAL_COLOR_SPACE 1
#else
  #define ACTUAL_COLOR_SPACE 4 // gamma 2.2
#endif

// Color Space Controls
uniform uint DECODE_METHOD <
    ui_label    = "Decode Method";
    ui_type     = "combo";
    ui_items    = "Auto\0scRGB(default)\0HDR10\0sRGB\0Gamma 2.2\0None\0";
    ui_tooltip  = "Select the decoding method for input colors (Auto = detect from pipeline)";
    ui_category = "Color Space";
> = 0;

uniform uint ENCODE_METHOD <
    ui_label    = "Encode Method";
    ui_type     = "combo";
    ui_items    = "Auto\0scRGB(default)\0HDR10\0sRGB\0Gamma 2.2\0None\0";
    ui_tooltip  = "Select the encoding method for output colors (Auto = match input)";
    ui_category = "Color Space";
> = 0;

uniform float INPUT_BRIGHTNESS_NITS <
    ui_label = "Input Brightness (nits)";
    ui_tooltip = "Reference white/peak for input (scRGB/PQ/HDR10). Typical SDR is 80 nits, HDR is 1000+ nits.";
    ui_category = "Color Space";
    ui_type = "slider";
    ui_min = 10.f;
    ui_max = 500.f;
    ui_step = 1.f;
> = 203.f;

uniform float OUTPUT_BRIGHTNESS_NITS <
    ui_label = "Output Brightness (nits)";
    ui_tooltip = "Reference white/peak for output (scRGB/PQ/HDR10). Match to your display or desired output.";
    ui_category = "Color Space";
    ui_type = "slider";
    ui_min = 10.f;
    ui_max = 500.f;
    ui_step = 1.f;
> = 203.f;

uniform uint WORKING_COLOR_SPACE <
    ui_label    = "Working Color Space";
    ui_type     = "combo";
    ui_items    = "BT709\0BT2020\0";
    ui_tooltip  = "Select the working color space for luminance calculations and color processing";
    ui_category = "Color Space";
> = 0;

// Profile Selection
uniform uint ProfileSelect <
    ui_type = "combo";
    ui_label = "Profile Selection";
    ui_tooltip = "Select which profile to use";
    ui_items = "Profile 1 (SpecialK v1)\0Profile 2 (High Boost)\0Profile 3\0Profile 4\0";
> = 2;

// Profile 1 Perceptual Boost Mode
uniform int PerceptualBoostMode_P1 <
    ui_type = "combo";
    ui_label = "Perceptual Boost Mode";
    ui_tooltip = "Choose which Perceptual Boost algorithm to use for Profile 1";
    ui_items = "Original (XYZ/PQ)\0ICtCp (experimental)\0PQ + ICtCp\0None\0Inverse Tonemapping with Reinhard\0";
    ui_category = "Profile 1 (SpecialK v1)";
> = 0;

// Profile 1 Parameters (SpecialK v1 preset)
uniform float PerceptualBoost0_P1 <
    ui_type = "slider";
    ui_min = 100.0; ui_max = 20000.0;
    ui_label = "Perceptual Boost 0 (nits)";
    ui_tooltip = "Primary boost parameter in nits (default: 10000)";
    ui_category = "Profile 1 (SpecialK v1)";
> = 10000.0;

uniform float PerceptualBoost2_P1 <
    ui_type = "slider";
    ui_min = 0.5; ui_max = 2.0;
    ui_label = "Perceptual Boost 2";
    ui_tooltip = "Multiplier parameter (default: 1.273)";
    ui_category = "Profile 1 (SpecialK v1)";
> = 1.273;

uniform float ColorBoost_P1 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0;
    ui_label = "Color Boost";
    ui_tooltip = "Color intensity boost (0.0 = luminance only, 1.0 = full color boost)";
    ui_category = "Profile 1 (SpecialK v1)";
> = 0.333;

uniform float LumPivot_P1 <
    ui_type = "slider";
    ui_min = 0.001; ui_max = 1.0;
    ui_label = "Luminance Pivot";
    ui_tooltip = "Luminance pivot value used for perceptual boost (default: 0.18)";
    ui_category = "Profile 1 (SpecialK v1)";
> = 0.18;

uniform float EffectStrength_P1 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 2.0;
    ui_label = "Effect Strength";
    ui_tooltip = "Strength of the perceptual boost effect (0.0 = no effect, 1.0 = full effect)";
    ui_category = "Profile 1 (SpecialK v1)";
> = 1.0;

// Add boost3 uniforms for each profile after boost2
uniform float PerceptualBoost3_P1 <
    ui_type = "slider";
    ui_min = -1.0; ui_max = 1.0;
    ui_label = "Perceptual Boost 3";
    ui_tooltip = "Additive boost parameter (default: 0.0)";
    ui_category = "Profile 1 (SpecialK v1)";
> = 0.0;

// Profile 1 Parameters (SpecialK v1 preset)
uniform float MinCoef_P1 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 10.0;
    ui_label = "Min Coef";
    ui_tooltip = "Minimum brightness coefficient for Profile 1";
    ui_category = "Profile 1 (SpecialK v1)";
> = 0.0;

uniform float MaxCoef_P1 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 10.0;
    ui_label = "Max Coef";
    ui_tooltip = "Maximum brightness coefficient for Profile 1";
    ui_category = "Profile 1 (SpecialK v1)";
> = 10.0;

// Profile 1 Perceptual Boost Mode
uniform int PerceptualBoostMode_P2 <
    ui_type = "combo";
    ui_label = "Perceptual Boost Mode";
    ui_tooltip = "Choose which Perceptual Boost algorithm to use for Profile 2";
    ui_items = "Original (XYZ/PQ)\0ICtCp (experimental)\0PQ + ICtCp\0None\0Inverse Tonemapping with Reinhard\0";
    ui_category = "Profile 2 (High Boost)";
> = 0;


// Profile 2 Parameters (High Boost preset)
uniform float PerceptualBoost0_P2 <
    ui_type = "slider";
    ui_min = 100.0; ui_max = 20000.0;
    ui_label = "Perceptual Boost 0 (nits)";
    ui_tooltip = "Primary boost parameter in nits (default: 333)";
    ui_category = "Profile 2 (High Boost)";
> = 333.0;

uniform float PerceptualBoost2_P2 <
    ui_type = "slider";
    ui_min = 0.5; ui_max = 3.0;
    ui_label = "Perceptual Boost 2";
    ui_tooltip = "Multiplier parameter (default: 1.5)";
    ui_category = "Profile 2 (High Boost)";
> = 1.5;

uniform float ColorBoost_P2 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0;
    ui_label = "Color Boost";
    ui_tooltip = "Color intensity boost (0.0 = luminance only, 1.0 = full color boost)";
    ui_category = "Profile 2 (High Boost)";
> = 0.333;

uniform float LumPivot_P2 <
    ui_type = "slider";
    ui_min = 0.001; ui_max = 1.0;
    ui_label = "Luminance Pivot";
    ui_tooltip = "Luminance pivot value used for perceptual boost (default: 0.18)";
    ui_category = "Profile 2 (High Boost)";
> = 0.18;

uniform float EffectStrength_P2 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 2.0;
    ui_label = "Effect Strength";
    ui_tooltip = "Strength of the perceptual boost effect (0.0 = no effect, 1.0 = full effect)";
    ui_category = "Profile 2 (High Boost)";
> = 1.0;

// Add boost3 uniforms for each profile after boost2
uniform float PerceptualBoost3_P2 <
    ui_type = "slider";
    ui_min = -1.0; ui_max = 1.0;
    ui_label = "Perceptual Boost 3";
    ui_tooltip = "Additive boost parameter (default: 0.0)";
    ui_category = "Profile 2 (High Boost)";
> = 0.0;

uniform float MinCoef_P2 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 10.0;
    ui_label = "Min Coef";
    ui_tooltip = "Minimum brightness coefficient for Profile 2";
    ui_category = "Profile 2 (High Boost)";
> = 0.0;

uniform float MaxCoef_P2 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 10.0;
    ui_label = "Max Coef";
    ui_tooltip = "Maximum brightness coefficient for Profile 2";
    ui_category = "Profile 2 (High Boost)";
> = 10.0;

// Profile 3 Perceptual Boost Mode
uniform int PerceptualBoostMode_P3 <
    ui_type = "combo";
    ui_label = "Perceptual Boost Mode";
    ui_tooltip = "Choose which Perceptual Boost algorithm to use for Profile 3";
    ui_items = "Original (XYZ/PQ)\0ICtCp (experimental)\0PQ + ICtCp\0None\0Inverse Tonemapping with Reinhard\0";
    ui_category = "Profile 3";
> = 2;

// Profile 3 Parameters
uniform float PerceptualBoost0_P3 <
    ui_type = "slider";
    ui_min = 100.0; ui_max = 20000.0;
    ui_label = "Perceptual Boost 0 (nits)";
    ui_tooltip = "Primary boost parameter in nits";
    ui_category = "Profile 3";
> = 10000.0;

uniform float PerceptualBoost2_P3 <
    ui_type = "slider";
    ui_min = 0.5; ui_max = 3.0;
    ui_label = "Perceptual Boost 2";
    ui_tooltip = "Multiplier parameter";
    ui_category = "Profile 3";
> = 1.208;

uniform float ColorBoost_P3 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0;
    ui_label = "Color Boost";
    ui_tooltip = "Color intensity boost (0.0 = luminance only, 1.0 = full color boost)";
    ui_category = "Profile 3";
> = 0.238;

uniform float LumPivot_P3 <
    ui_type = "slider";
    ui_min = 0.001; ui_max = 1.0;
    ui_label = "Luminance Pivot";
    ui_tooltip = "Luminance pivot value used for perceptual boost (default: 0.18)";
    ui_category = "Profile 3";
> = 0.027;

uniform float EffectStrength_P3 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 2.0;
    ui_label = "Effect Strength";
    ui_tooltip = "Strength of the perceptual boost effect (0.0 = no effect, 1.0 = full effect)";
    ui_category = "Profile 3";
> = 1.0;

// Add boost3 uniforms for each profile after boost2
uniform float PerceptualBoost3_P3 <
    ui_type = "slider";
    ui_min = -1.0; ui_max = 1.0;
    ui_label = "Perceptual Boost 3";
    ui_tooltip = "Additive boost parameter (default: 0.0)";
    ui_category = "Profile 3";
> = 0.0;

uniform float MinCoef_P3 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 10.0;
    ui_label = "Min Coef";
    ui_tooltip = "Minimum brightness coefficient for Profile 3";
    ui_category = "Profile 3";
> = 0.0;

uniform float MaxCoef_P3 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 10.0;
    ui_label = "Max Coef";
    ui_tooltip = "Maximum brightness coefficient for Profile 3";
    ui_category = "Profile 3";
> = 10.0;

// Profile 4 Perceptual Boost Mode
uniform int PerceptualBoostMode_P4 <
    ui_type = "combo";
    ui_label = "Perceptual Boost Mode";
    ui_tooltip = "Choose which Perceptual Boost algorithm to use for Profile 4";
    ui_items = "Original (XYZ/PQ)\0ICtCp (experimental)\0PQ + ICtCp\0None\0Inverse Tonemapping with Reinhard\0";
    ui_category = "Profile 4";
> = 2;

// Profile 4 Parameters
uniform float PerceptualBoost0_P4 <
    ui_type = "slider";
    ui_min = 100.0; ui_max = 20000.0;
    ui_label = "Perceptual Boost 0 (nits)";
    ui_tooltip = "Primary boost parameter in nits";
    ui_category = "Profile 4";
> = 10000.0;

uniform float PerceptualBoost2_P4 <
    ui_type = "slider";
    ui_min = 0.5; ui_max = 3.0;
    ui_label = "Perceptual Boost 2";
    ui_tooltip = "Multiplier parameter";
    ui_category = "Profile 4";
> = 1.273;

uniform float ColorBoost_P4 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0;
    ui_label = "Color Boost";
    ui_tooltip = "Color intensity boost (0.0 = luminance only, 1.0 = full color boost)";
    ui_category = "Profile 4";
> = 0.333;

uniform float LumPivot_P4 <
    ui_type = "slider";
    ui_min = 0.001; ui_max = 1.0;
    ui_label = "Luminance Pivot";
    ui_tooltip = "Luminance pivot value used for perceptual boost (default: 0.18)";
    ui_category = "Profile 4";
> = 0.18;

uniform float EffectStrength_P4 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 2.0;
    ui_label = "Effect Strength";
    ui_tooltip = "Strength of the perceptual boost effect (0.0 = no effect, 1.0 = full effect)";
    ui_category = "Profile 4";
> = 1.0;

// Add boost3 uniforms for each profile after boost2
uniform float PerceptualBoost3_P4 <
    ui_type = "slider";
    ui_min = -1.0; ui_max = 1.0;
    ui_label = "Perceptual Boost 3";
    ui_tooltip = "Additive boost parameter (default: 0.0)";
    ui_category = "Profile 4";
> = 0.0;

uniform float MinCoef_P4 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 10.0;
    ui_label = "Min Coef";
    ui_tooltip = "Minimum brightness coefficient for Profile 4";
    ui_category = "Profile 4";
> = 0.0;

uniform float MaxCoef_P4 <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 10.0;
    ui_label = "Max Coef";
    ui_tooltip = "Maximum brightness coefficient for Profile 4";
    ui_category = "Profile 4";
> = 10.0;

uniform bool EnablePerceptualBoost <
    ui_label = "Enable Perceptual Boost";
    ui_tooltip = "Enable/disable the perceptual boost effect";
> = true;

uniform bool EnableDebugCurve <
    ui_label = "Debug Coefficient Curve";
    ui_tooltip = "Show a 2D overlay displaying the brightness coefficient curve based on input nits";
    ui_category = "Debug";
> = false;

// Helper function to get current profile parameters, now also returns mode
void GetCurrentProfileParams(out float boost0, out float boost2, out float colorBoost, out float lum_pivot, out float effectStrength, out float boost3, out int mode, out float minCoef, out float maxCoef) {
    if (ProfileSelect == 0) {
        // Profile 1
        boost0 = PerceptualBoost0_P1;
        boost2 = PerceptualBoost2_P1;
        colorBoost = ColorBoost_P1;
        lum_pivot = LumPivot_P1;
        effectStrength = EffectStrength_P1;
        boost3 = PerceptualBoost3_P1;
        mode = PerceptualBoostMode_P1;
        minCoef = MinCoef_P1;
        maxCoef = MaxCoef_P1;
    } else if (ProfileSelect == 1) {
        // Profile 2
        boost0 = PerceptualBoost0_P2;
        boost2 = PerceptualBoost2_P2;
        colorBoost = ColorBoost_P2;
        lum_pivot = LumPivot_P2;
        effectStrength = EffectStrength_P2;
        boost3 = PerceptualBoost3_P2;
        mode = PerceptualBoostMode_P2;
        minCoef = MinCoef_P2;
        maxCoef = MaxCoef_P2;
    } else if (ProfileSelect == 2) {
        // Profile 3
        boost0 = PerceptualBoost0_P3;
        boost2 = PerceptualBoost2_P3;
        colorBoost = ColorBoost_P3;
        lum_pivot = LumPivot_P3;
        effectStrength = EffectStrength_P3;
        boost3 = PerceptualBoost3_P3;
        mode = PerceptualBoostMode_P3;
        minCoef = MinCoef_P3;
        maxCoef = MaxCoef_P3;
    } else {
        // Profile 4
        boost0 = PerceptualBoost0_P4;
        boost2 = PerceptualBoost2_P4;
        colorBoost = ColorBoost_P4;
        lum_pivot = LumPivot_P4;
        effectStrength = EffectStrength_P4;
        boost3 = PerceptualBoost3_P4;
        mode = PerceptualBoostMode_P4;
        minCoef = MinCoef_P4;
        maxCoef = MaxCoef_P4;
    }
}

// Color space conversion functions
float3 ConvertColorSpace(float3 color, uint from_space, uint to_space)
{
    if (from_space == to_space)
        return color;
    
    if (from_space == 0 && to_space == 1) // BT709 to BT2020
        return BT2020::from::BT709(color);
    else if (from_space == 1 && to_space == 0) // BT2020 to BT709
        return BT709::from::BT2020(color);
    
    return color; // Default fallback
}

float3 DecodeColor(float3 color)
{
    uint decode_method = DECODE_METHOD;
    if (decode_method == 0) // Auto
    {
        decode_method = ACTUAL_COLOR_SPACE;
    }
    if (decode_method == 1) // scRGB
    {
        return ConvertColorSpace(color * 80.f / INPUT_BRIGHTNESS_NITS, 0, WORKING_COLOR_SPACE);
        // scRGB is already in linear space, just apply scaling
    }
    else if (decode_method == 2) // HDR10
    {
        // Decode from PQ (HDR10) to linear
        return ConvertColorSpace(color::pq::DecodeSafe(color, INPUT_BRIGHTNESS_NITS), 1, WORKING_COLOR_SPACE);
    }
    else if (decode_method == 3) // sRGB
    {
        return ConvertColorSpace(color::sRGB::decodeSafe(color), 0, WORKING_COLOR_SPACE);
    }
    else if (decode_method == 4) // Gamma 2.2
    {
        return ConvertColorSpace(color::gamma::decodeSafe(color, 2.2f), 0, WORKING_COLOR_SPACE);
    }
    return ConvertColorSpace(color, 0, WORKING_COLOR_SPACE); // Default fallback
}

float3 EncodeColor(float3 color)
{
    uint encode_method = ENCODE_METHOD;
    if (encode_method == 0) // Auto
    {
        encode_method = ACTUAL_COLOR_SPACE;
    }
    if (encode_method == 1) // scRGB
    {
        // Convert back to scRGB space
        return ConvertColorSpace(color, WORKING_COLOR_SPACE, 0) * OUTPUT_BRIGHTNESS_NITS / 80.f;
    }
    else if (encode_method == 2) // HDR10
    {
        // Encode to PQ (HDR10)
        return color::pq::EncodeSafe(ConvertColorSpace(color, WORKING_COLOR_SPACE, 1), OUTPUT_BRIGHTNESS_NITS);
    }
    else if (encode_method == 3) // sRGB
    {
        return color::sRGB::encodeSafe(ConvertColorSpace(color, WORKING_COLOR_SPACE, 0));
    }
    else if (encode_method == 4) // Gamma 2.2
    {
        return color::gamma::encodeSafe(ConvertColorSpace(color, WORKING_COLOR_SPACE, 0), 2.2f);
    }
    return ConvertColorSpace(color, WORKING_COLOR_SPACE, 0); // Default fallback
}

float3 PerceptualBoostEffectWithProfile(float3 color) {
    float boost0, boost2, colorBoost, lum_pivot, effectStrength, boost3, __unused__, minCoef, maxCoef;
    GetCurrentProfileParams(boost0, boost2, colorBoost, lum_pivot, effectStrength, boost3, __unused__, minCoef, maxCoef);
    
    float3 xyz = XYZ::from::BT709(color);
    float mid_grey = lum_pivot;
    float new_mid_grey = mid_grey;
    float y = xyz.y;
    if (y > 0.0000001f)
    {    
        float4 input = float4(xyz, mid_grey);
        float4 newXYZ = sign(input) * color::pq::Decode4(
            color::pq::Encode4(abs(input), boost0) * boost2, 
            boost0);
        new_mid_grey = newXYZ.w;

        xyz = lerp(
            xyz * (newXYZ.y / max(0.000001f, y)),
            newXYZ.xyz,
            colorBoost);
    } else {
        return color;
    }
    //effectStrength = lerp(0, effectStrength, saturate(2.f * (y - 0.5f)));
    float3 newColor = BT709::from::XYZ(xyz) * (mid_grey / new_mid_grey);

    float newY = color::y::from::BT709(newColor);
    float brightnessCoef = newY / max(0.000001f, y);

    float newBrightnessCoef = clamp(brightnessCoef, minCoef, maxCoef);
    if (brightnessCoef >= maxCoef) {
        newBrightnessCoef = max(1.f, maxCoef - (brightnessCoef - maxCoef)/(2*maxCoef));
    }


    return newColor * newBrightnessCoef / max(0.000001f, brightnessCoef);

/*
    //     return lerp(color, BT709::from::XYZ(xyz) * (0.18f / new_mid_grey), effectStrength);
    if (true) {
        return newColor;
    }


    float3 c1  = lerp(color, BT709::from::XYZ(xyz) * (mid_grey/ new_mid_grey), effectStrength);
    float3 c2 =  pow(BT709::from::XYZ(xyz) * (mid_grey / new_mid_grey), effectStrength);

    return lerp(c1, c2, boost3);*/
}

/*
float3 PerceptualBoostEffectWithProfile(float3 color) {
    float boost0, boost2, colorBoost, brightnessCorrection, effectStrength, boost3;
    GetCurrentProfileParams(boost0, boost2, colorBoost, brightnessCorrection, effectStrength, boost3);
    
    float3 xyz = XYZ::from::BT709(color);
    float mid_grey = 0.18f;
    float new_mid_grey = mid_grey;
    float y = xyz.y;
    if (y > 0.00001f)
    {    
        float4 newXYZ = color::pq::Decode4(
            max(0.f, color::pq::Encode4(float4(max(0.f, xyz), mid_grey), boost0) * boost2 + float4(boost3, boost3, boost3, 0.f)), 
            10000.f);
        new_mid_grey = newXYZ.w;

        xyz = lerp(
            xyz * (newXYZ.y / y),
            newXYZ.xyz,
            colorBoost);
    }
    //effectStrength = lerp(0, effectStrength, saturate(2.f * (y - 0.5f)));
    return lerp(color, BT709::from::XYZ(xyz) * (mid_grey / new_mid_grey), effectStrength);
}*/

float3 PerceptualBoostEffectWithProfile2(float3 color) {
    float boost0, boost2, colorBoost, lum_pivot, effectStrength, boost3, __unused__;
    GetCurrentProfileParams(boost0, boost2, colorBoost, lum_pivot, effectStrength, boost3, __unused__, __unused__, __unused__
    );
    

    float3 ictcp = color::ictcp::from::BT709(color * (boost0 / 10000.f)); // boost0 adjustes such that 100 nits - (color * (boost0 / 10000.f)) = 0.5f
    ictcp.x *= boost2;
    ictcp.yz *= (1 + colorBoost);
    float3 new_color = color::ictcp::to::BT709(ictcp);

    float3 ictcp_grey= color::ictcp::from::BT709(0.18f * (boost0 / 10000.f));
    ictcp_grey.x *= boost2;
//    ictcp_grey.yz = 0.f;
    ictcp_grey.yz *= (1 + colorBoost);
    float3 new_grey = color::ictcp::to::BT709(ictcp_grey);
    return lerp(color, new_color * (0.18f / new_grey), effectStrength);
}



float3 PerceptualBoostEffectWithProfile3(float3 color) {
    float boost0, boost2, colorBoost, lum_pivot, effectStrength, boost3, minCoef, maxCoef;
    int __unused__;
    GetCurrentProfileParams(boost0, boost2, colorBoost, lum_pivot, effectStrength, boost3, __unused__, minCoef, maxCoef);
    
    float y = color::y::from::BT709(color);
    float new_lum_pivot = lum_pivot;
    float brightnessCoef = 1.f;
    if (y > 0.000001f)
    {    
        float3 newXYZ = color::pq::Decode(
            (color::pq::Encode(float3(lum_pivot, y, lum_pivot), boost0) * boost2), 
            10000.f);
        new_lum_pivot = newXYZ.z;
        brightnessCoef = (newXYZ.y / max(0.000001f, y)) * (lum_pivot / max(0.000001f, new_lum_pivot));
    }
    brightnessCoef = clamp(brightnessCoef, minCoef, maxCoef);

    float3 new_color = color * brightnessCoef;

    float3 ictcp = color::ictcp::from::BT709(new_color);
    ictcp.yz *= (1.f + (brightnessCoef - 1.f) * colorBoost * 0.5f);
    float3 new_color2 = color::ictcp::to::BT709(ictcp);

    return lerp(color, new_color2, effectStrength);
}


/*
/*

    float3 xyz = XYZ::from::BT709(color);
    float mid_grey = 0.18f;
    float y = xyz.y;
    if (y > 0.00001f)
    {    


        float4 newXYZ = color::pq::Decode4(
            color::pq::Encode4(float4(xyz, mid_grey), 10000.f) * boost2, 
            10000.f);
        mid_grey = newXYZ.w;

        xyz = lerp(
            xyz * (newXYZ.y / y),
            newXYZ.xyz,
            colorBoost);
    }
    return lerp(color, BT709::from::XYZ(xyz) * (0.18f / mid_grey), effectStrength);*/

float3 PerceptualBoostEffectWithProfile4(float3 color) {
    // None mode - just return the input color unchanged
    return color;
}

/*
// Inverse Reinhard tonemapping function
// "L_white" of 2 matches simple Reinhard
float3 inv_tonemap_ReinhardPerComponent(float3 L, float L_white)
{
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
}*/

float3 PerceptualBoostEffectWithProfile5(float3 color) {
    float boost0, boost2, colorBoost, lum_pivot, effectStrength, boost3, minCoef, maxCoef;
    int __unused__;
    GetCurrentProfileParams(boost0, boost2, colorBoost, lum_pivot, effectStrength, boost3, __unused__, minCoef, maxCoef);
    
    // Inverse Tonemapping with Reinhard
    float3 s = sign(color);
    float3 untonemapped = abs(color);
    
    // Calculate mid-gray for brightness preservation
    float3 mid_gray = float3(lum_pivot, lum_pivot, lum_pivot);
    
    // Apply inverse Reinhard tonemapping
    float3 inv_tonemapped = inv_tonemap_ReinhardPerComponent(untonemapped, boost0 / 10000.0f);
    
    // Re-map the image to roughly keep the same average brightness
    inv_tonemapped *= mid_gray / inv_tonemap_ReinhardPerComponent(mid_gray, boost0 / 10000.0f);
    
    // Apply brightness coefficient clamping
    float original_y = color::y::from::BT709(color);
    float new_y = color::y::from::BT709(inv_tonemapped * s);
    float brightnessCoef = new_y / max(0.000001f, original_y);
    brightnessCoef = clamp(brightnessCoef, minCoef, maxCoef);
    
    inv_tonemapped = color * brightnessCoef;
    
    // Apply color boost if enabled
    if (colorBoost > 0.0f) {
        float3 ictcp = color::ictcp::from::BT709(inv_tonemapped * s);
        ictcp.yz *= (1.f + (brightnessCoef - 1.f) * colorBoost * 0.5f);
        inv_tonemapped = color::ictcp::to::BT709(ictcp);
    }
    
    inv_tonemapped *= s;
    
    return lerp(color, inv_tonemapped, effectStrength);
}

float3 applyCurve(float3 testColor) {

    // Get current profile parameters
    float boost0, boost2, colorBoost, lum_pivot, effectStrength, boost3, minCoef, maxCoef;
    int mode;
    GetCurrentProfileParams(boost0, boost2, colorBoost, lum_pivot, effectStrength, boost3, mode, minCoef, maxCoef);

    float3 newColor;
    if (mode == 0) {
        newColor = PerceptualBoostEffectWithProfile(testColor.xyz);
    } else if (mode == 1) {
        newColor = PerceptualBoostEffectWithProfile2(testColor.xyz);
    } else if (mode == 2) {
        newColor = PerceptualBoostEffectWithProfile3(testColor.xyz);
    } else if (mode == 3) {
        newColor = PerceptualBoostEffectWithProfile4(testColor.xyz);
    } else {
        newColor = PerceptualBoostEffectWithProfile5(testColor.xyz);
    }
    
    return newColor;
}

// Debug curve drawing function
float3 DrawDebugCurve(float2 texcoord) {
    // Define curve area (bottom right corner, 300x200 pixels)

    const float barSizeInPixels = 100.f;
    const float2 bars = float2(8.f, 4.f);

    float2 curveSize = bars * barSizeInPixels;
    float2 curvePos = float2(BUFFER_SCREEN_SIZE.x - curveSize.x - 20.0, 20.0);
    float2 curveEnd = curvePos + curveSize;
    
    // Check if we're in the curve area
    if (texcoord.x < curvePos.x || texcoord.x > curveEnd.x || 
        texcoord.y < curvePos.y || texcoord.y > curveEnd.y) {
        return float3(0.0, 0.0, 0.0); // Transparent outside curve area
    }
    
    // Convert screen coordinates to curve coordinates

    float2 curveCoord = (texcoord - curvePos) / curveSize * bars;
    curveCoord.y = bars.y - curveCoord.y;
    
    // Draw background
    float3 bgColor = float3(0.1, 0.1, 0.1);
    
    // Draw grid lines
    float3 gridColor = float3(0.3, 0.3, 0.3);

    
    // Calculate nits from curve X coordinate
    float nits = curveCoord.x;
    
    // Calculate coefficient for this nits value
    float coefficient = 1.0;
    float coefficientGreen = 1.0;
    if (nits > 0.00001) {
        // Convert nits to PQ space

        float3 testColor = float3(nits, nits, nits);
        float y = color::y::from::BT709(testColor);

        float3 newColor = applyCurve(testColor);
        
        float newY = color::y::from::BT709(newColor);

        coefficient = newY / max(0.00001f, y);

        float expectedY = coefficient;
        
        if (newY >= 1500 / 203.f) {
            expectedY *= (1500 / 203.f) / newY;
        }

        // Draw curve point
        
        
        float dist = distance(curveCoord, float2(curveCoord.x, expectedY));
        if (dist < 0.02) {
            return float3(1.0, 1.0, 1.0); // Red curvee
        }

        // Draw RGB curves using loop
        float3 colors[3] = {float3(nits, 0.f, 0.f), float3(0.f, nits, 0.f), float3(0.f, 0.f, nits)};
        float3 curveColors[3] = {float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(0.0, 0.0, 1.0)}; // Red, Green, Blue
        
        for (int i = 0; i < 3; i++) {
            float3 colorChannel = colors[i];
            float3 newColorChannel = applyCurve(colorChannel);
            float newY2 = color::y::from::BT709(newColorChannel);
            float coefficientChannel = newY2 / color::y::from::BT709(colorChannel);
            
            if (max(newColorChannel.r, max(newColorChannel.g, newColorChannel.b)) >= 1500 / 203.f) {
                coefficientChannel *= (1500 / 203.f) / max(newColorChannel.r, max(newColorChannel.g, newColorChannel.b));
            }
            float distChannel = distance(curveCoord, float2(curveCoord.x, coefficientChannel));
            
            if (distChannel < 0.02) {
                return curveColors[i];
            }
        }

        //coefficient *= pow(y, 0.3);
        /*float3 newXYZ = color::pq::Decode(
            (color::pq::Encode(float3(lum_pivot, nits / 203.0, lum_pivot), boost0) * boost2), 
            10000.f);
        coefficient = (newXYZ.y / (nits / 203.0)) * (lum_pivot / newXYZ.z);
        coefficient = clamp(coefficient, minCoef, maxCoef);*/
    }
    
    // Convert coefficient to Y coordinate (0-5 range)
   



    // Vertical grid lines (every 300 nits)
    for (int i = 1; i < bars.x; i++) {
        float x = i ; // 300/1500 = 0.2
        if (abs(curveCoord.x - x) < 0.011f) {
            return gridColor;
        }
    }
    // Draw horizontal lines for specific nits values (203, 406, 609, 812, 1015, 1218, 1421)
    float3 nitsLineColor = float3(0.0, 0.7, 1.0); // Cyan color

    for (int i = 0; i < bars.y; i++) {
        // 50 nits -> 0.2
        float y = i;
        if (abs(curveCoord.y - y) < 0.011f) {
            return nitsLineColor;
        }
    }
    
    // Draw axes
    if (curveCoord.x < 0.01 || curveCoord.y < 0.01) {
        return float3(0.8, 0.8, 0.8);
    }
    
    
    // Draw labels
    if (curveCoord.x > 0.95 * bars.x && curveCoord.y > 0.95 * bars.y) {
        return float3(1.0, 1.0, 1.0); // White text area
    }
    
    return bgColor;
}

// Main pixel shader
float4 PerceptualBoostPS(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    float4 color = tex2D(ReShade::BackBuffer, texcoord);
    
    // Draw debug curve if enabled
    if (EnableDebugCurve) {
        float3 debugColor = DrawDebugCurve(texcoord * float2(3840.f, 2160.f));
        if (any(debugColor > 0.0)) {
            return float4(debugColor, 0.8);
        }
    }
    
    if (!EnablePerceptualBoost)
        return color;
    
    // Check if current profile has valid parameters
    float boost0, boost2, colorBoost, lum_pivot, effectStrength, boost3, minCoef, maxCoef;
    int mode;
    GetCurrentProfileParams(boost0, boost2, colorBoost, lum_pivot, effectStrength, boost3, mode, minCoef, maxCoef);
    
    if (boost0 <= 100.0f)
        return color;
    
    // Decode input color to working color space
    float3 decodedColor = DecodeColor(color.rgb);
    
    // Apply perceptual boost
    float3 processedColor = applyCurve(decodedColor);
    
    // Encode output color from working color space
    float3 encodedColor = EncodeColor(processedColor);

    return float4(encodedColor, color.a);
}

// Technique
technique SpecialK_PerceptualBoost
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = PerceptualBoostPS;
    }
} 