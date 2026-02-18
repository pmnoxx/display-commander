// Display Commander - Brightness (self-contained, no ReShade.fxh)
// Simple brightness adjustment driven by Display Commander main tab (0-200%, 100% = neutral).
// Uses ReShade semantics : COLOR for backbuffer; can live in any effect path (e.g. Display_Commander\Reshade\Shaders).

#if !defined(__RESHADE__) || __RESHADE__ < 30000
#error "ReShade 3.0+ is required"
#endif

// Backbuffer (ReShade binds this via : COLOR)
texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

// Fullscreen triangle vertex shader (no vertex buffer needed)
void PostProcessVS(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD) {
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

uniform float Brightness <
    ui_type = "slider";
    ui_min = 0.0;
    ui_max = 2.0;
    ui_step = 0.01;
    ui_label = "Brightness";
    ui_tooltip = "1.0 = neutral. Set by Display Commander when using Main tab Brightness.";
> = 1.0;

float4 MainPS(float4 pos : SV_Position, float2 tex : TexCoord) : SV_Target {
    float4 color = tex2D(BackBuffer, tex);
    color.rgb *= Brightness;
    return color;
}

technique Brightness {
    pass {
        VertexShader = PostProcessVS;
        PixelShader = MainPS;
    }
}
