// Display Commander - Control (brightness; self-contained, no ReShade.fxh)
// Simple brightness adjustment driven by Display Commander main tab (0-200%, 100% = neutral).
// Uses ReShade semantics : COLOR for backbuffer; can live in any effect path (e.g. Display_Commander\Reshade\Shaders).

#if !defined(__RESHADE__) || __RESHADE__ < 30000
#error "ReShade 3.0+ is required"
#endif
#include "color.fxh"
#include "Reshade.fxh"

#if (BUFFER_COLOR_BIT_DEPTH == 10) || BUFFER_COLOR_SPACE == CSP_HDR10
  #define ACTUAL_COLOR_SPACE 2
#elif (BUFFER_COLOR_BIT_DEPTH == 16)
  #define ACTUAL_COLOR_SPACE 1
#else
  #define ACTUAL_COLOR_SPACE 4 // gamma 2.2
#endif

uniform float INPUT_BRIGHTNESS_NITS
<
  ui_label = "Input Brightness (nits)";
  ui_tooltip = "Reference white/peak for input (scRGB/PQ/HDR10). Typical SDR is 80 nits, HDR is 1000+ nits.";
  ui_category = "Color Space";
  ui_type = "slider";
  ui_min = 10.f;
  ui_max = 500.f;
  ui_step = 1.f;
> = 203.f;

uniform float OUTPUT_BRIGHTNESS_NITS
<
  ui_label = "Output Brightness (nits)";
  ui_tooltip = "Reference white/peak for output (scRGB/PQ/HDR10). Match to your display or desired output.";
  ui_category = "Color Space";
  ui_type = "slider";
  ui_min = 10.f;
  ui_max = 500.f;
  ui_step = 1.f;
> = 203.f;


uniform uint WORKING_COLOR_SPACE
<
  ui_label    = "Working Color Space";
  ui_type     = "combo";
  ui_items    = "BT709\0BT2020\0";
  ui_tooltip  = "Select the working color space for luminance calculations and color processing";
  ui_category = "Color Space";
> = 0;


// Backbuffer (ReShade binds this via : COLOR)
texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform uint DECODE_METHOD
<
  ui_label    = "Decode Method";
  ui_type     = "combo";
  ui_items    = "Auto\0scRGB(default)\0HDR10\0sRGB\0Gamma 2.2\0None\0";
  ui_tooltip  = "Select the decoding method for input colors (Auto = detect from pipeline)";
  ui_category = "Color Space";
> = 0;

uniform uint ENCODE_METHOD
<
  ui_label    = "Encode Method";
  ui_type     = "combo";
  ui_items    = "Auto\0scRGB(default)\0HDR10\0sRGB\0Gamma 2.2\0None\0";
  ui_tooltip  = "Select the encoding method for output colors (Auto = match input)";
  ui_category = "Color Space";
> = 0;

uniform float Brightness <
    ui_type = "slider";
    ui_min = 0.0;
    ui_max = 2.0;
    ui_step = 0.01;
    ui_label = "Brightness";
    ui_tooltip = "1.0 = neutral. Set by Display Commander when using Main tab Brightness.";
> = 1.0;

uniform float Gamma <
    ui_type = "slider";
    ui_min = 0.5;
    ui_max = 2.0;
    ui_step = 0.01;
    ui_label = "Gamma";
    ui_tooltip = "1.0 = neutral. Set by Display Commander when using Main tab Gamma.";
> = 1.0;

uniform float Contrast <
    ui_type = "slider";
    ui_min = 0.0;
    ui_max = 2.0;
    ui_step = 0.01;
    ui_label = "Contrast";
    ui_tooltip = "1.0 = neutral. Set by Display Commander when using Main tab Misc Contrast.";
> = 1.0;

uniform float Saturation <
    ui_type = "slider";
    ui_min = 0.0;
    ui_max = 2.0;
    ui_step = 0.01;
    ui_label = "Saturation";
    ui_tooltip = "1.0 = neutral, 0 = grayscale. Set by Display Commander when using Main tab Misc Saturation.";
> = 1.0;

uniform float HueDegrees <
    ui_type = "slider";
    ui_min = -15.0;
    ui_max = 15.0;
    ui_step = 0.5;
    ui_label = "Hue (degrees)";
    ui_tooltip = "0 = neutral. Shift hue by -15 to +15 degrees. Set by Display Commander when using Main tab Misc Hue.";
> = 0.0;

#define COLOR_SPACE_BT709 0.f
#define COLOR_SPACE_BT2020 1.f

float3 ConvertColorSpace(float3 color, float from_space, float to_space)
{
    if (from_space == to_space) {
        return color; // No conversion needed
    }
    if (to_space == COLOR_SPACE_BT709)
    {
       return BT709::from::BT2020(color);
    }
    return BT2020::from::BT709(color);
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
        return ConvertColorSpace(color * 80.f / INPUT_BRIGHTNESS_NITS, COLOR_SPACE_BT709, WORKING_COLOR_SPACE);
        // scRGB is already in linear space, just apply scaling
    }
    else if (decode_method == 2) // HDR10
    {
        // Decode from PQ (HDR10) to linear
        return ConvertColorSpace(color::pq::DecodeSafe(color, INPUT_BRIGHTNESS_NITS), COLOR_SPACE_BT2020, WORKING_COLOR_SPACE);
    }
    else if (decode_method == 3) // sRGB
    {
        return ConvertColorSpace(color::sRGB::decodeSafe(color), COLOR_SPACE_BT709, WORKING_COLOR_SPACE);
    }
    else if (decode_method == 4) // Gamma 2.2
    {
        return ConvertColorSpace(color::gamma::decodeSafe(color, 2.2f), COLOR_SPACE_BT709, WORKING_COLOR_SPACE);
    }
    return ConvertColorSpace(color, COLOR_SPACE_BT709, WORKING_COLOR_SPACE); // Default fallback
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
        return ConvertColorSpace(color, WORKING_COLOR_SPACE, COLOR_SPACE_BT709) * OUTPUT_BRIGHTNESS_NITS / 80.f;
    }
    else if (encode_method == 2) // HDR10
    {
        // Encode to PQ (HDR10)
        return color::pq::EncodeSafe(ConvertColorSpace(color, WORKING_COLOR_SPACE, COLOR_SPACE_BT2020), OUTPUT_BRIGHTNESS_NITS);
    }
    else if (encode_method == 3) // sRGB
    {
        return color::sRGB::encodeSafe(ConvertColorSpace(color, WORKING_COLOR_SPACE, COLOR_SPACE_BT709) );
    }
    else if (encode_method == 4) // Gamma 2.2
    {
        return color::gamma::encodeSafe(ConvertColorSpace(color, WORKING_COLOR_SPACE, COLOR_SPACE_BT709) , 2.2f);
    }
    return ConvertColorSpace(color, WORKING_COLOR_SPACE, COLOR_SPACE_BT709) ; // Default fallback
}

// Hue/chroma/value and HSV conversion (Chilliant-style, H in 0-1; perfect round-trip)
#define HSV_EPS 1e-10

float3 HUEtoRGB(float H) {
    float R = abs(H * 6.0 - 3.0) - 1.0;
    float G = 2.0 - abs(H * 6.0 - 2.0);
    float B = 2.0 - abs(H * 6.0 - 4.0);
    return saturate(float3(R, G, B));
}

float3 RGBtoHCV(float3 RGB) {
    float4 P = (RGB.g < RGB.b) ? float4(RGB.bg, -1.0, 2.0 / 3.0) : float4(RGB.gb, 0.0, -1.0 / 3.0);
    float4 Q = (RGB.r < P.x) ? float4(P.xyw, RGB.r) : float4(RGB.r, P.yzx);
    float C = Q.x - min(Q.w, Q.y);
    float H = abs((Q.w - Q.y) / (6.0 * C + HSV_EPS) + Q.z);
    return float3(H, C, Q.x);
}

float3 RGBtoHSV(float3 RGB) {
    float3 HCV = RGBtoHCV(RGB);
    float S = HCV.y / (HCV.z + HSV_EPS);
    return float3(HCV.x, S, HCV.z);
}

float3 HSVtoRGB(float3 HSV) {
    float3 RGB = HUEtoRGB(HSV.x);
    return ((RGB - 1.0) * HSV.y + 1.0) * HSV.z;
}

// Fullscreen triangle vertex shader (no vertex buffer needed)
void PostProcessVS2(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD) {
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}


float4 MainPS(float4 pos : SV_Position, float2 tex : TexCoord) : SV_Target {
    float4 color = tex2D(BackBuffer, tex);
    color = DecodeColor(color);

    // Contrast: pivot 0.5 (1.0 = no change)
    color.rgb = (color.rgb - 0.5) * Contrast + 0.5;

    // Saturation: 1.0 = no change, 0 = grayscale
    float luma = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    color.rgb = lerp(float3(luma, luma, luma), color.rgb, Saturation);

    // Hue: shift hue by HueDegrees (-15 to +15). Always use same path so 0 and near-0 are continuous.
    float3 hsv = RGBtoHSV(color.rgb);
    hsv.x = frac(hsv.x + HueDegrees / 360.0);
    color.rgb = HSVtoRGB(hsv);

    // Gamma: linear -> gamma-corrected (1.0 = no change)
    color.rgb = pow(max(color.rgb, 1e-5), 1.0 / Gamma);

    color.rgb *= Brightness;

    color = EncodeColor(color);

    return color;
}

technique Brightness {
    pass {
        VertexShader = PostProcessVS2;
        PixelShader = MainPS;
    }
}
