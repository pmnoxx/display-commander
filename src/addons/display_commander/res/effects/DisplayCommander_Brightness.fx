// Display Commander - Brightness
// Simple brightness adjustment effect driven by Display Commander main tab (0-200%, 100% = neutral).
// Requires ReShade.fxh (place this file in ReShade's Shaders folder or add DC effect path to ReShade).

#include "ReShade.fxh"

uniform float Brightness <
    ui_type = "slider";
    ui_min = 0.0;
    ui_max = 2.0;
    ui_step = 0.01;
    ui_label = "Brightness";
    ui_tooltip = "1.0 = neutral. Set by Display Commander when using Main tab Brightness.";
> = 1.0;

float4 MainPS(float4 pos : SV_Position, float2 tex : TexCoord) : SV_Target {
    float4 color = tex2D(ReShade::BackBuffer, tex);
    color.rgb *= Brightness;
    return color;
}

technique Brightness {
    pass {
        VertexShader = PostProcessVS;
        PixelShader = MainPS;
    }
}
