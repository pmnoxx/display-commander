#ifndef SRC_TEMPLATE_SHARED_H_
#define SRC_TEMPLATE_SHARED_H_

// #define RENODX_PEAK_WHITE_NITS                 1000.f
// #define RENODX_DIFFUSE_WHITE_NITS              renodx::color::bt2408::REFERENCE_WHITE
// #define RENODX_GRAPHICS_WHITE_NITS             renodx::color::bt2408::GRAPHICS_WHITE
// #define RENODX_COLOR_GRADE_STRENGTH            1.f
// #define RENODX_TONE_MAP_TYPE                   TONE_MAP_TYPE_RENO_DRT
// #define RENODX_TONE_MAP_EXPOSURE               1.f
// #define RENODX_TONE_MAP_HIGHLIGHTS             1.f
// #define RENODX_TONE_MAP_SHADOWS                1.f
// #define RENODX_TONE_MAP_CONTRAST               1.f
// #define RENODX_TONE_MAP_SATURATION             1.f
// #define RENODX_TONE_MAP_HIGHLIGHT_SATURATION   1.f
// #define RENODX_TONE_MAP_BLOWOUT                0
// #define RENODX_TONE_MAP_FLARE                  0
// #define RENODX_TONE_MAP_HUE_CORRECTION         1.f
// #define RENODX_TONE_MAP_HUE_SHIFT              0
// #define RENODX_TONE_MAP_WORKING_COLOR_SPACE    color::convert::COLOR_SPACE_BT709
// #define RENODX_TONE_MAP_CLAMP_COLOR_SPACE      color::convert::COLOR_SPACE_NONE
// #define RENODX_TONE_MAP_CLAMP_PEAK             color::convert::COLOR_SPACE_BT709
// #define RENODX_TONE_MAP_HUE_PROCESSOR          HUE_PROCESSOR_OKLAB
// #define RENODX_TONE_MAP_PER_CHANNEL            0
// #define RENODX_GAMMA_CORRECTION                GAMMA_CORRECTION_GAMMA_2_2
// #define RENODX_INTERMEDIATE_SCALING            (RENODX_DIFFUSE_WHITE_NITS / RENODX_GRAPHICS_WHITE_NITS)
// #define RENODX_INTERMEDIATE_ENCODING           (RENODX_GAMMA_CORRECTION + 1.f)
// #define RENODX_INTERMEDIATE_COLOR_SPACE        color::convert::COLOR_SPACE_BT709
// #define RENODX_SWAP_CHAIN_DECODING             RENODX_INTERMEDIATE_ENCODING
// #define RENODX_SWAP_CHAIN_DECODING_COLOR_SPACE RENODX_INTERMEDIATE_COLOR_SPACE
// #define RENODX_SWAP_CHAIN_CUSTOM_COLOR_SPACE   COLOR_SPACE_CUSTOM_BT709D65
// #define RENODX_SWAP_CHAIN_SCALING_NITS         RENODX_GRAPHICS_WHITE_NITS
// #define RENODX_SWAP_CHAIN_CLAMP_NITS           RENODX_PEAK_WHITE_NITS
// #define RENODX_SWAP_CHAIN_CLAMP_COLOR_SPACE    color::convert::COLOR_SPACE_UNKNOWN
// #define RENODX_SWAP_CHAIN_ENCODING             ENCODING_SCRGB
// #define RENODX_SWAP_CHAIN_ENCODING_COLOR_SPACE color::convert::COLOR_SPACE_BT709

// Must be 32bit aligned
// Should be 4x32
struct ShaderInjectData {
  float peak_white_nits;              // 1000.f 
  float diffuse_white_nits;
  float graphics_white_nits;
  float color_grade_strength;
  float tone_map_type;
  float tone_map_exposure;
  float tone_map_highlights;
  float tone_map_shadows;
  float tone_map_contrast;
  float tone_map_saturation;
  float tone_map_highlight_saturation;
  float tone_map_blowout;
  float tone_map_flare;
  float tone_map_hue_correction;
  float tone_map_hue_shift;
  float tone_map_working_color_space;
  float tone_map_clamp_color_space;
  float tone_map_clamp_peak;
  float tone_map_hue_processor;
  float tone_map_per_channel;
  float gamma_correction;
  float tone_map_gamma; // For ColorGradeGamma setting (0.0-1.0)
  float intermediate_scaling;
  float intermediate_encoding;
  float intermediate_color_space;
  float swap_chain_decoding;
  float swap_chain_gamma_correction;
  float swap_chain_custom_color_space;
  float swap_chain_clamp_color_space;
  float swap_chain_encoding;
  float swap_chain_encoding_color_space;
  float reno_drt_white_clip;
  float perceptual_boost_method; 
  float perceptual_boost_param;
  float perceptual_boost_color;
  float perceptual_boost_strength;

  float effect_split_x;
  float effect_split_mode;
  float effect_split_angle;

  float color_grade_per_channel_blowout_restoration;     // 0.f
  float color_grade_per_channel_hue_correction;          // 0.f
  float color_grade_per_channel_chrominance_correction;  // 0.f
  float color_grade_tone_map_pass_autocorrection;        // TODO add slider
  
  // Perceptual Boost
  float perceptual_boost_mode; // 0 = OFF, 1 = Reinhard, 2 = XY->PQ, 3 = ICTCP
  float perceptual_boost_channel_max; // Used for Reinhard, user adjustable
  // Reinhard mode parameters
  float perceptual_boost_reinhard_strength; // Used for Reinhard method
  // XY->PQ mode parameters
  float perceptual_boost_xypq_param; // Used for XY->PQ method
  float perceptual_boost_xypq_color; // Used for XY->PQ method
  float perceptual_boost_xypq_strength; // Used for XY->PQ method
  // ICTCP mode parameters
  float perceptual_boost_ictcp_param; // Used for ICTCP method
  float perceptual_boost_ictcp_color; // Used for ICTCP method
  float perceptual_boost_ictcp_strength; // Used for ICTCP method
  // Scene Type Perceptual Boost Strength
  float perceptual_boost_2d_character; // Perceptual boost strength for 2D character scenes (0.0-10.0)
  float perceptual_boost_2d_background; // Perceptual boost strength for 2D background scenes (0.0-10.0)
  float perceptual_boost_3d; // Perceptual boost strength for 3D scenes (0.0-10.0)
  float perceptual_boost_display_output;


  // Debug Mode
  float debug_mode; // Debug mode for development and testing (0.0-1.0)
  float debug_mode2; // Debug mode 2 for development and testing (0.0-1.0)
  float debug_mode3; // Debug mode 3 for development and testing (0.0-1.0)
  

  float display_map_type;
  float display_map_peak;
  float display_map_shoulder;
  float display_map_saturation;

  // Tone Map Pass Control
  float enable_tone_map_pass; // Enable/disable tone mapping pass (0.0 = off, 1.0 = on)

  // CUSTOM GAME SETTINGS
  // Text Brightness Coefficient
  float custom_text_brightness_coef; // Text brightness coefficient for UI elements (default: 1.0)
  
  // Character Brightness
  float custom_character_brightness; // Character brightness multiplier (default: 0.85)
};

#ifndef __cplusplus
#if ((__SHADER_TARGET_MAJOR == 5 && __SHADER_TARGET_MINOR >= 1) || __SHADER_TARGET_MAJOR >= 6)
cbuffer shader_injection : register(b13, space50) {
#elif (__SHADER_TARGET_MAJOR < 5) || ((__SHADER_TARGET_MAJOR == 5) && (__SHADER_TARGET_MINOR < 1))
cbuffer shader_injection : register(b13) {
#endif
  ShaderInjectData shader_injection : packoffset(c0);
}

#define RENODX_TONE_MAP_TYPE                 shader_injection.tone_map_type
#define RENODX_PEAK_WHITE_NITS               shader_injection.peak_white_nits
#define RENODX_DIFFUSE_WHITE_NITS            shader_injection.diffuse_white_nits
#define RENODX_GRAPHICS_WHITE_NITS           shader_injection.graphics_white_nits
#define RENODX_GAMMA_CORRECTION              shader_injection.gamma_correction
#define RENODX_TONE_MAP_PER_CHANNEL          shader_injection.tone_map_per_channel
#define RENODX_TONE_MAP_WORKING_COLOR_SPACE  shader_injection.tone_map_working_color_space
#define RENODX_TONE_MAP_HUE_PROCESSOR        shader_injection.tone_map_hue_processor
#define RENODX_TONE_MAP_HUE_CORRECTION       shader_injection.tone_map_hue_correction
#define RENODX_TONE_MAP_HUE_SHIFT            shader_injection.tone_map_hue_shift
#define RENODX_TONE_MAP_CLAMP_COLOR_SPACE    shader_injection.tone_map_clamp_color_space
#define RENODX_TONE_MAP_CLAMP_PEAK           shader_injection.tone_map_clamp_peak
#define RENODX_TONE_MAP_EXPOSURE             shader_injection.tone_map_exposure
#define RENODX_TONE_MAP_HIGHLIGHTS           shader_injection.tone_map_highlights
#define RENODX_TONE_MAP_SHADOWS              shader_injection.tone_map_shadows
#define RENODX_TONE_MAP_CONTRAST             shader_injection.tone_map_contrast
#define RENODX_TONE_MAP_SATURATION           shader_injection.tone_map_saturation
#define RENODX_TONE_MAP_HIGHLIGHT_SATURATION shader_injection.tone_map_highlight_saturation
#define RENODX_TONE_MAP_BLOWOUT              shader_injection.tone_map_blowout
#define RENODX_TONE_MAP_FLARE                shader_injection.tone_map_flare
#define RENODX_COLOR_GRADE_STRENGTH          shader_injection.color_grade_strength
#define RENODX_INTERMEDIATE_ENCODING         shader_injection.intermediate_encoding
#define RENODX_SWAP_CHAIN_DECODING           shader_injection.swap_chain_decoding
#define RENODX_SWAP_CHAIN_GAMMA_CORRECTION   shader_injection.swap_chain_gamma_correction
// #define RENODX_SWAP_CHAIN_DECODING_COLOR_SPACE shader_injection.swap_chain_decoding_color_space
#define RENODX_SWAP_CHAIN_CUSTOM_COLOR_SPACE shader_injection.swap_chain_custom_color_space
// #define RENODX_SWAP_CHAIN_SCALING_NITS         shader_injection.swap_chain_scaling_nits
// #define RENODX_SWAP_CHAIN_CLAMP_NITS           shader_injection.swap_chain_clamp_nits
#define RENODX_SWAP_CHAIN_CLAMP_COLOR_SPACE    shader_injection.swap_chain_clamp_color_space
#define RENODX_SWAP_CHAIN_ENCODING             shader_injection.swap_chain_encoding
#define RENODX_SWAP_CHAIN_ENCODING_COLOR_SPACE shader_injection.swap_chain_encoding_color_space
#define RENODX_RENO_DRT_TONE_MAP_METHOD        renodx::tonemap::renodrt::config::tone_map_method::REINHARD

#define RENODX_RENO_DRT_WHITE_CLIP               shader_injection.reno_drt_white_clip

#define RENODX_PERCEPTUAL_BOOST_PARAM          shader_injection.perceptual_boost_param
#define RENODX_PERCEPTUAL_BOOST_COLOR         shader_injection.perceptual_boost_color
#define RENODX_PERCEPTUAL_BOOST_STRENGTH          shader_injection.perceptual_boost_strength
#define RENODX_PERCEPTUAL_BOOST_METHOD           shader_injection.perceptual_boost_method
#define RENODX_PERCEPTUAL_BOOST_METHOD_XY_PQ 1
#define RENODX_PERCEPTUAL_BOOST_METHOD_ICTCP 2
#define RENODX_PIXEL_SHADER_DECODE_MODE shader_injection.pixel_shader_decode_mode


#define RENODX_PERCEPTUAL_BOOST_REINHARD_STRENGTH shader_injection.perceptual_boost_reinhard_strength
// XY->PQ mode parameters
#define RENODX_PERCEPTUAL_BOOST_XYPQ_PARAM     shader_injection.perceptual_boost_xypq_param
#define RENODX_PERCEPTUAL_BOOST_XYPQ_COLOR     shader_injection.perceptual_boost_xypq_color
#define RENODX_PERCEPTUAL_BOOST_XYPQ_STRENGTH  shader_injection.perceptual_boost_xypq_strength
// ICTCP mode parameters
#define RENODX_PERCEPTUAL_BOOST_ICTCP_PARAM    shader_injection.perceptual_boost_ictcp_param
#define RENODX_PERCEPTUAL_BOOST_ICTCP_COLOR    shader_injection.perceptual_boost_ictcp_color
#define RENODX_PERCEPTUAL_BOOST_ICTCP_STRENGTH shader_injection.perceptual_boost_ictcp_strength
#define RENODX_PERCEPTUAL_BOOST_2D_CHARACTER   shader_injection.perceptual_boost_2d_character
#define RENODX_PERCEPTUAL_BOOST_2D_BACKGROUND  shader_injection.perceptual_boost_2d_background
#define RENODX_PERCEPTUAL_BOOST_3D             shader_injection.perceptual_boost_3d
#define RENODX_PERCEPTUAL_BOOST_DISPLAY_OUTPUT shader_injection.perceptual_boost_display_output


// Debug Mode
#define RENODX_DEBUG_MODE                      shader_injection.debug_mode
#define RENODX_DEBUG_MODE2                     shader_injection.debug_mode2
#define RENODX_DEBUG_MODE3                     shader_injection.debug_mode3

#define DISPLAY_MAP_TYPE                       shader_injection.display_map_type
#define DISPLAY_MAP_PEAK                       shader_injection.display_map_peak
#define DISPLAY_MAP_SHOULDER                   shader_injection.display_map_shoulder
#define DISPLAY_MAP_SATURATION                 shader_injection.display_map_saturation

#define COLOR_GRADE_PER_CHANNEL_BLOWOUT_RESTORATION     shader_injection.color_grade_per_channel_blowout_restoration
#define COLOR_GRADE_PER_CHANNEL_HUE_CORRECTION          shader_injection.color_grade_per_channel_hue_correction
#define COLOR_GRADE_PER_CHANNEL_CHROMINANCE_CORRECTION  shader_injection.color_grade_per_channel_chrominance_correction


#define RENODX_ENABLE_UI_TONEMAPPASS                    shader_injection.enable_tone_map_pass

// CUSTOM GAME SETTINGS

// Text Brightness Coefficient
#define CUSTOM_TEXT_BRIGHTNESS_COEF            shader_injection.custom_text_brightness_coef

// Character Brightness
#define CUSTOM_CHARACTER_BRIGHTNESS            shader_injection.custom_character_brightness


#include "../../shaders/renodx.hlsl"

#endif

#endif  // SRC_TEMPLATE_SHARED_H_
