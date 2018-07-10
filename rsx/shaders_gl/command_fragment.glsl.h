#include "shaders_common.h"

#undef command_fragment_name_

#ifdef FILTER_SABR
#define command_fragment_name_ command_fragment_sabr
#elif defined(FILTER_XBR)
#define command_fragment_name_ command_fragment_xbr
#elif defined(FILTER_BILINEAR)
#define command_fragment_name_ command_fragment_bilinear
#elif defined(FILTER_3POINT)
#define command_fragment_name_ command_fragment_3point
#elif defined(FILTER_JINC2)
#define command_fragment_name_ command_fragment_jinc2
#else
#define command_fragment_name_ command_fragment
#endif

static const char * command_fragment_name_ = GLSL_FRAGMENT "\n\
uniform sampler2D fb_texture;\n\
\n\
// Scaling to apply to the dither pattern\n\
uniform uint dither_scaling;\n\
\n\
// 0: Only draw opaque pixels, 1: only draw semi-transparent pixels\n\
uniform uint draw_semi_transparent;\n\
\n\
//uniform uint mask_setor;\n\
//uniform uint mask_evaland;\n\
\n\
in vec3 frag_shading_color;\n\
// Texture page: base offset for texture lookup.\n\
flat in uvec2 frag_texture_page;\n\
// Texel coordinates within the page. Interpolated by OpenGL.\n\
in vec2 frag_texture_coord;\n\
// Clut coordinates in VRAM\n\
flat in uvec2 frag_clut;\n\
// 0: no texture, 1: raw-texture, 2: blended\n\
flat in uint frag_texture_blend_mode;\n\
// 0: 16bpp (no clut), 1: 8bpp, 2: 4bpp\n\
flat in uint frag_depth_shift;\n\
// 0: No dithering, 1: dithering enabled\n\
flat in uint frag_dither;\n\
// 0: Opaque primitive, 1: semi-transparent primitive\n\
flat in uint frag_semi_transparent;\n\
// Texture window: [ X mask, X or, Y mask, Y or ]\n\
flat in uvec4 frag_texture_window;\n\
// Texture limits: [Umin, Vmin, Umax, Vmax]\n\
flat in uvec4 frag_texture_limits;\n\
\n\
out vec4 frag_color;\n\
\n\
const uint BLEND_MODE_NO_TEXTURE    = 0u;\n\
const uint BLEND_MODE_RAW_TEXTURE   = 1u;\n\
const uint BLEND_MODE_TEXTURE_BLEND = 2u;\n\
\n\
const uint FILTER_MODE_NEAREST      = 0u;\n\
const uint FILTER_MODE_SABR         = 1u;\n\
\n\
// Read a pixel in VRAM\n\
vec4 vram_get_pixel(uint x, uint y) {\n\
  x = (x & 0x3ffu);\n\
  y = (y & 0x1ffu);\n\
\n\
  return texelFetch(fb_texture, ivec2(x, y), 0);\n\
}\n\
\n\
// Take a normalized color and convert it into a 16bit 1555 ABGR\n\
// integer in the format used internally by the Playstation GPU.\n\
uint rebuild_psx_color(vec4 color) {\n\
  uint a = uint(floor(color.a + 0.5));\n\
  uint r = uint(floor(color.r * 31. + 0.5));\n\
  uint g = uint(floor(color.g * 31. + 0.5));\n\
  uint b = uint(floor(color.b * 31. + 0.5));\n\
\n\
  // return (r << 11) | (g << 6) | (b << 1) | a;\n\
  return (a << 15) | (b << 10) | (g << 5) | r;\n\
}\n\
\n\
// Texture color 0x0000 is special in the Playstation GPU, it denotes\n\
// a fully transparent texel (even for opaque draw commands). If you\n\
// want black you have to use an opaque draw command and use `0x8000`\n\
// instead.\n\
bool is_transparent(vec4 texel) {\n\
  return rebuild_psx_color(texel) == 0u;\n\
}\n\
\n\
// PlayStation dithering pattern. The offset is selected based on the\n\
// pixel position in VRAM, by blocks of 4x4 pixels. The value is added\n\
// to the 8bit color components before they're truncated to 5 bits.\n\
//// TODO: r5 - There might be extra line breaks in here\n\
const int dither_pattern[16] =\n\
  int[16](-4,  0, -3,  1,\n\
           2, -2,  3, -1,\n\
          -3,  1, -4,  0,\n\
           3, -1,  2, -2);\n\
\n\
vec4 sample_texel(vec2 coords) {\n\
   // Number of texel per VRAM 16bit 'pixel' for the current depth\n\
   uint pix_per_hw = 1u << frag_depth_shift;\n\
\n\
   // Texture pages are limited to 256x256 pixels\n\
   uint tex_x = clamp(uint(coords.x), 0x0u, 0xffu);\n\
   uint tex_y = clamp(uint(coords.y), 0x0u, 0xffu);\n\
\n\
   // Clamp to primitive limits\n\
   tex_x = clamp(tex_x, frag_texture_limits[0], frag_texture_limits[2] - 1u);\n\
   tex_y = clamp(tex_y, frag_texture_limits[1], frag_texture_limits[3] - 1u);\n\
\n\
   // Texture window adjustments\n\
   tex_x = (tex_x & frag_texture_window[0]) | frag_texture_window[1];\n\
   tex_y = (tex_y & frag_texture_window[2]) | frag_texture_window[3];\n\
\n\
   // Adjust x coordinate based on the texel color depth.\n\
   uint tex_x_pix = tex_x / pix_per_hw;\n\
\n\
   tex_x_pix += frag_texture_page.x;\n\
   tex_y += frag_texture_page.y;\n\
\n\
   vec4 texel = vram_get_pixel(tex_x_pix, tex_y);\n\
\n\
   if (frag_depth_shift > 0u) {\n\
      // 8 and 4bpp textures are paletted so we need to lookup the\n\
      // real color in the CLUT\n\
\n\
      // 8 and 4bpp textures contain several texels per 16bit VRAM\n\
      // 'pixel'\n\
      float tex_x_float = coords.x / float(pix_per_hw);\n\
\n\
      uint icolor = rebuild_psx_color(texel);\n\
\n\
      // A little bitwise magic to get the index in the CLUT. 4bpp\n\
      // textures have 4 texels per VRAM 'pixel', 8bpp have 2. We need\n\
      // to shift the current color to find the proper part of the\n\
      // halfword and then mask away the rest.\n\
\n\
      // Bits per pixel (4 or 8)\n\
      uint bpp = 16u >> frag_depth_shift;\n\
\n\
      // 0xf for 4bpp, 0xff for 8bpp\n\
      uint mask = ((1u << bpp) - 1u);\n\
\n\
      // 0...3 for 4bpp, 0...1 for 8bpp\n\
      uint align = tex_x & ((1u << frag_depth_shift) - 1u);\n\
\n\
      // 0, 4, 8 or 12 for 4bpp, 0 or 8 for 8bpp\n\
      uint shift = (align * bpp);\n\
\n\
      // Finally we have the index in the CLUT\n\
      uint index = (icolor >> shift) & mask;\n\
\n\
      uint clut_x = frag_clut.x + index;\n\
      uint clut_y = frag_clut.y;\n\
\n\
      // Look up the real color for the texel in the CLUT\n\
      texel = vram_get_pixel(clut_x, clut_y);\n\
   }\n\
   return texel;\n\
}\n\
"

#if defined(FILTER_SABR) || defined(FILTER_XBR)
"\n\
in vec2 tc;\n\
in vec4 xyp_1_2_3;\n\
in vec4 xyp_6_7_8;\n\
in vec4 xyp_11_12_13;\n\
in vec4 xyp_16_17_18;\n\
in vec4 xyp_21_22_23;\n\
in vec4 xyp_5_10_15;\n\
in vec4 xyp_9_14_9;\n\
\n\
float c_df(vec3 c1, vec3 c2) {\n\
	vec3 df = abs(c1 - c2);\n\
	return df.r + df.g + df.b;\n\
}\n\
\n\
const float coef = 2.0;\n\
"
#endif

#ifdef FILTER_SABR
"\n\
// constants and functions for sabr\n\
const  vec4 Ai  = vec4( 1.0, -1.0, -1.0,  1.0);\n\
const  vec4 B45 = vec4( 1.0,  1.0, -1.0, -1.0);\n\
const  vec4 C45 = vec4( 1.5,  0.5, -0.5,  0.5);\n\
const  vec4 B30 = vec4( 0.5,  2.0, -0.5, -2.0);\n\
const  vec4 C30 = vec4( 1.0,  1.0, -0.5,  0.0);\n\
const  vec4 B60 = vec4( 2.0,  0.5, -2.0, -0.5);\n\
const  vec4 C60 = vec4( 2.0,  0.0, -1.0,  0.5);\n\
\n\
const  vec4 M45 = vec4(0.4, 0.4, 0.4, 0.4);\n\
const  vec4 M30 = vec4(0.2, 0.4, 0.2, 0.4);\n\
const  vec4 M60 = M30.yxwz;\n\
const  vec4 Mshift = vec4(0.2, 0.2, 0.2, 0.2);\n\
\n\
const  vec4 threshold = vec4(0.32, 0.32, 0.32, 0.32);\n\
\n\
const  vec3 lum = vec3(0.21, 0.72, 0.07);\n\
\n\
vec4 lum_to(vec3 v0, vec3 v1, vec3 v2, vec3 v3) {\n\
	return vec4(dot(lum, v0), dot(lum, v1), dot(lum, v2), dot(lum, v3));\n\
}\n\
\n\
vec4 lum_df(vec4 A, vec4 B) {\n\
	return abs(A - B);\n\
}\n\
\n\
bvec4 lum_eq(vec4 A, vec4 B) {\n\
	return lessThan(lum_df(A, B) , vec4(threshold));\n\
}\n\
\n\
vec4 lum_wd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h) {\n\
	return lum_df(a, b) + lum_df(a, c) + lum_df(d, e) + lum_df(d, f) + 4.0 * lum_df(g, h);\n\
}\n\
\n\
//sabr\n\
vec4 get_texel_sabr()\n\
{\n\
	// Store mask values\n\
	vec3 P1  = sample_texel(xyp_1_2_3.xw   ).rgb;\n\
	vec3 P2  = sample_texel(xyp_1_2_3.yw   ).rgb;\n\
	vec3 P3  = sample_texel(xyp_1_2_3.zw   ).rgb;\n\
\n\
	vec3 P6  = sample_texel(xyp_6_7_8.xw   ).rgb;\n\
	vec3 P7  = sample_texel(xyp_6_7_8.yw   ).rgb;\n\
	vec3 P8  = sample_texel(xyp_6_7_8.zw   ).rgb;\n\
\n\
	vec3 P11 = sample_texel(xyp_11_12_13.xw).rgb;\n\
	vec3 P12 = sample_texel(xyp_11_12_13.yw).rgb;\n\
	vec3 P13 = sample_texel(xyp_11_12_13.zw).rgb;\n\
\n\
	vec3 P16 = sample_texel(xyp_16_17_18.xw).rgb;\n\
	vec3 P17 = sample_texel(xyp_16_17_18.yw).rgb;\n\
	vec3 P18 = sample_texel(xyp_16_17_18.zw).rgb;\n\
\n\
	vec3 P21 = sample_texel(xyp_21_22_23.xw).rgb;\n\
	vec3 P22 = sample_texel(xyp_21_22_23.yw).rgb;\n\
	vec3 P23 = sample_texel(xyp_21_22_23.zw).rgb;\n\
\n\
	vec3 P5  = sample_texel(xyp_5_10_15.xy ).rgb;\n\
	vec3 P10 = sample_texel(xyp_5_10_15.xz ).rgb;\n\
	vec3 P15 = sample_texel(xyp_5_10_15.xw ).rgb;\n\
\n\
	vec3 P9  = sample_texel(xyp_9_14_9.xy  ).rgb;\n\
	vec3 P14 = sample_texel(xyp_9_14_9.xz  ).rgb;\n\
	vec3 P19 = sample_texel(xyp_9_14_9.xw  ).rgb;\n\
\n\
// Store luminance values of each point\n\
	vec4 p7  = lum_to(P7,  P11, P17, P13);\n\
	vec4 p8  = lum_to(P8,  P6,  P16, P18);\n\
	vec4 p11 = p7.yzwx;                      // P11, P17, P13, P7\n\
	vec4 p12 = lum_to(P12, P12, P12, P12);\n\
	vec4 p13 = p7.wxyz;                      // P13, P7,  P11, P17\n\
	vec4 p14 = lum_to(P14, P2,  P10, P22);\n\
	vec4 p16 = p8.zwxy;                      // P16, P18, P8,  P6\n\
	vec4 p17 = p7.zwxy;                      // P11, P17, P13, P7\n\
	vec4 p18 = p8.wxyz;                      // P18, P8,  P6,  P16\n\
	vec4 p19 = lum_to(P19, P3,  P5,  P21);\n\
	vec4 p22 = p14.wxyz;                     // P22, P14, P2,  P10\n\
	vec4 p23 = lum_to(P23, P9,  P1,  P15);\n\
\n\
	vec2 fp = fract(tc);\n\
\n\
	vec4 ma45 = smoothstep(C45 - M45, C45 + M45, Ai * fp.y + B45 * fp.x);\n\
	vec4 ma30 = smoothstep(C30 - M30, C30 + M30, Ai * fp.y + B30 * fp.x);\n\
	vec4 ma60 = smoothstep(C60 - M60, C60 + M60, Ai * fp.y + B60 * fp.x);\n\
	vec4 marn = smoothstep(C45 - M45 + Mshift, C45 + M45 + Mshift, Ai * fp.y + B45 * fp.x);\n\
\n\
	vec4 e45   = lum_wd(p12, p8, p16, p18, p22, p14, p17, p13);\n\
	vec4 econt = lum_wd(p17, p11, p23, p13, p7, p19, p12, p18);\n\
	vec4 e30   = lum_df(p13, p16);\n\
	vec4 e60   = lum_df(p8, p17);\n\
\n\
    vec4 final45 = vec4(1.0);\n\
	vec4 final30 = vec4(0.0);\n\
	vec4 final60 = vec4(0.0);\n\
	vec4 final36 = vec4(0.0);\n\
	vec4 finalrn = vec4(0.0);\n\
\n\
	vec4 px = step(lum_df(p12, p17), lum_df(p12, p13));\n\
\n\
	vec4 mac = final36 * max(ma30, ma60) + final30 * ma30 + final60 * ma60 + final45 * ma45 + finalrn * marn;\n\
\n\
	vec3 res1 = P12;\n\
	res1 = mix(res1, mix(P13, P17, px.x), mac.x);\n\
	res1 = mix(res1, mix(P7 , P13, px.y), mac.y);\n\
	res1 = mix(res1, mix(P11, P7 , px.z), mac.z);\n\
	res1 = mix(res1, mix(P17, P11, px.w), mac.w);\n\
\n\
	vec3 res2 = P12;\n\
	res2 = mix(res2, mix(P17, P11, px.w), mac.w);\n\
	res2 = mix(res2, mix(P11, P7 , px.z), mac.z);\n\
	res2 = mix(res2, mix(P7 , P13, px.y), mac.y);\n\
	res2 = mix(res2, mix(P13, P17, px.x), mac.x);\n\
\n\
	float texel_alpha = sample_texel(vec2(frag_texture_coord.x, frag_texture_coord.y)).w;\n\
\n\
   vec4 texel = vec4(mix(res1, res2, step(c_df(P12, res1), c_df(P12, res2))), texel_alpha);\n\
\n\
   return texel;\n\
}\n\
"
#endif

#ifdef FILTER_XBR
"\n\
// constants and functions for xbr\n\
const   vec3  rgbw          = vec3(14.352, 28.176, 5.472);\n\
const   vec4  eq_threshold  = vec4(15.0, 15.0, 15.0, 15.0);\n\
\n\
const vec4 delta   = vec4(1.0/4., 1.0/4., 1.0/4., 1.0/4.);\n\
const vec4 delta_l = vec4(0.5/4., 1.0/4., 0.5/4., 1.0/4.);\n\
const vec4 delta_u = delta_l.yxwz;\n\
\n\
const  vec4 Ao = vec4( 1.0, -1.0, -1.0, 1.0 );\n\
const  vec4 Bo = vec4( 1.0,  1.0, -1.0,-1.0 );\n\
const  vec4 Co = vec4( 1.5,  0.5, -0.5, 0.5 );\n\
const  vec4 Ax = vec4( 1.0, -1.0, -1.0, 1.0 );\n\
const  vec4 Bx = vec4( 0.5,  2.0, -0.5,-2.0 );\n\
const  vec4 Cx = vec4( 1.0,  1.0, -0.5, 0.0 );\n\
const  vec4 Ay = vec4( 1.0, -1.0, -1.0, 1.0 );\n\
const  vec4 By = vec4( 2.0,  0.5, -2.0,-0.5 );\n\
const  vec4 Cy = vec4( 2.0,  0.0, -1.0, 0.5 );\n\
const  vec4 Ci = vec4(0.25, 0.25, 0.25, 0.25);\n\
\n\
// Difference between vector components.\n\
vec4 df(vec4 A, vec4 B)\n\
{\n\
    return vec4(abs(A-B));\n\
}\n\
\n\
// Compare two vectors and return their components are different.\n\
vec4 diff(vec4 A, vec4 B)\n\
{\n\
    return vec4(notEqual(A, B));\n\
}\n\
\n\
// Determine if two vector components are equal based on a threshold.\n\
vec4 eq(vec4 A, vec4 B)\n\
{\n\
    return (step(df(A, B), vec4(15.)));\n\
}\n\
\n\
// Determine if two vector components are NOT equal based on a threshold.\n\
vec4 neq(vec4 A, vec4 B)\n\
{\n\
    return (vec4(1.0, 1.0, 1.0, 1.0) - eq(A, B));\n\
}\n\
\n\
// Weighted distance.\n\
vec4 wd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h)\n\
{\n\
    return (df(a,b) + df(a,c) + df(d,e) + df(d,f) + 4.0*df(g,h));\n\
}\n\
\n\
vec4 get_texel_xbr()\n\
{\n\
    vec4 edri;\n\
    vec4 edr;\n\
    vec4 edr_l;\n\
    vec4 edr_u;\n\
    vec4 px; // px = pixel, edr = edge detection rule\n\
    vec4 irlv0;\n\
    vec4 irlv1;\n\
    vec4 irlv2l;\n\
    vec4 irlv2u;\n\
    vec4 block_3d;\n\
    vec4 fx;\n\
    vec4 fx_l;\n\
    vec4 fx_u; // inequations of straight lines.\n\
\n\
    vec2 fp  = fract(tc);\n\
\n\
    vec3 A1 = sample_texel(xyp_1_2_3.xw    ).xyz;\n\
    vec3 B1 = sample_texel(xyp_1_2_3.yw    ).xyz;\n\
    vec3 C1 = sample_texel(xyp_1_2_3.zw    ).xyz;\n\
    vec3 A  = sample_texel(xyp_6_7_8.xw    ).xyz;\n\
    vec3 B  = sample_texel(xyp_6_7_8.yw    ).xyz;\n\
    vec3 C  = sample_texel(xyp_6_7_8.zw    ).xyz;\n\
    vec3 D  = sample_texel(xyp_11_12_13.xw ).xyz;\n\
    vec3 E  = sample_texel(xyp_11_12_13.yw ).xyz;\n\
    vec3 F  = sample_texel(xyp_11_12_13.zw ).xyz;\n\
    vec3 G  = sample_texel(xyp_16_17_18.xw ).xyz;\n\
    vec3 H  = sample_texel(xyp_16_17_18.yw ).xyz;\n\
    vec3 I  = sample_texel(xyp_16_17_18.zw ).xyz;\n\
    vec3 G5 = sample_texel(xyp_21_22_23.xw ).xyz;\n\
    vec3 H5 = sample_texel(xyp_21_22_23.yw ).xyz;\n\
    vec3 I5 = sample_texel(xyp_21_22_23.zw ).xyz;\n\
    vec3 A0 = sample_texel(xyp_5_10_15.xy  ).xyz;\n\
    vec3 D0 = sample_texel(xyp_5_10_15.xz  ).xyz;\n\
    vec3 G0 = sample_texel(xyp_5_10_15.xw  ).xyz;\n\
    vec3 C4 = sample_texel(xyp_9_14_9.xy   ).xyz;\n\
    vec3 F4 = sample_texel(xyp_9_14_9.xz   ).xyz;\n\
    vec3 I4 = sample_texel(xyp_9_14_9.xw   ).xyz;\n\
\n\
    vec4 b  = vec4(dot(B ,rgbw), dot(D ,rgbw), dot(H ,rgbw), dot(F ,rgbw));\n\
    vec4 c  = vec4(dot(C ,rgbw), dot(A ,rgbw), dot(G ,rgbw), dot(I ,rgbw));\n\
    vec4 d  = b.yzwx;\n\
    vec4 e  = vec4(dot(E,rgbw));\n\
    vec4 f  = b.wxyz;\n\
    vec4 g  = c.zwxy;\n\
    vec4 h  = b.zwxy;\n\
    vec4 i  = c.wxyz;\n\
    vec4 i4 = vec4(dot(I4,rgbw), dot(C1,rgbw), dot(A0,rgbw), dot(G5,rgbw));\n\
    vec4 i5 = vec4(dot(I5,rgbw), dot(C4,rgbw), dot(A1,rgbw), dot(G0,rgbw));\n\
    vec4 h5 = vec4(dot(H5,rgbw), dot(F4,rgbw), dot(B1,rgbw), dot(D0,rgbw));\n\
    vec4 f4 = h5.yzwx;\n\
\n\
    // These inequations define the line below which interpolation occurs.\n\
    fx   = (Ao*fp.y+Bo*fp.x);\n\
    fx_l = (Ax*fp.y+Bx*fp.x);\n\
    fx_u = (Ay*fp.y+By*fp.x);\n\
\n\
    irlv1 = irlv0 = diff(e,f) * diff(e,h);\n\
\n\
//#ifdef CORNER_B\n\
//    irlv1      = (irlv0 * ( neq(f,b) * neq(h,d) + eq(e,i) * neq(f,i4) * neq(h,i5) + eq(e,g) + eq(e,c) ) );\n\
//#endif\n\
//#ifdef CORNER_D\n\
//    vec4 c1 = i4.yzwx;\n\
//    vec4 g0 = i5.wxyz;\n\
//    irlv1     = (irlv0  *  ( neq(f,b) * neq(h,d) + eq(e,i) * neq(f,i4) * neq(h,i5) + eq(e,g) + eq(e,c) ) * (diff(f,f4) * diff(f,i) + diff(h,h5) * diff(h,i) + diff(h,g) + diff(f,c) + eq(b,c1) * eq(d,g0)));\n\
//#endif\n\
//#ifdef CORNER_C\n\
    irlv1     = (irlv0  * ( neq(f,b) * neq(f,c) + neq(h,d) * neq(h,g) + eq(e,i) * (neq(f,f4) * neq(f,i4) + neq(h,h5) * neq(h,i5)) + eq(e,g) + eq(e,c)) );\n\
//#endif\n\
\n\
    irlv2l = diff(e,g) * diff(d,g);\n\
    irlv2u = diff(e,c) * diff(b,c);\n\
\n\
    vec4 fx45i = clamp((fx   + delta   -Co - Ci)/(2.0*delta  ), 0.0, 1.0);\n\
    vec4 fx45  = clamp((fx   + delta   -Co     )/(2.0*delta  ), 0.0, 1.0);\n\
    vec4 fx30  = clamp((fx_l + delta_l -Cx     )/(2.0*delta_l), 0.0, 1.0);\n\
    vec4 fx60  = clamp((fx_u + delta_u -Cy     )/(2.0*delta_u), 0.0, 1.0);\n\
\n\
    vec4 wd1 = wd( e, c,  g, i, h5, f4, h, f);\n\
    vec4 wd2 = wd( h, d, i5, f, i4,  b, e, i);\n\
\n\
    edri  = step(wd1, wd2) * irlv0;\n\
    edr   = step(wd1 + vec4(0.1, 0.1, 0.1, 0.1), wd2) * step(vec4(0.5, 0.5, 0.5, 0.5), irlv1);\n\
    edr_l = step( 2.*df(f,g), df(h,c) ) * irlv2l * edr;\n\
    edr_u = step( 2.*df(h,c), df(f,g) ) * irlv2u * edr;\n\
\n\
    fx45  = edr   * fx45;\n\
    fx30  = edr_l * fx30;\n\
    fx60  = edr_u * fx60;\n\
    fx45i = edri  * fx45i;\n\
\n\
    px = step(df(e,f), df(e,h));\n\
\n\
//#ifdef SMOOTH_TIPS\n\
    vec4 maximos = max(max(fx30, fx60), max(fx45, fx45i));\n\
//#endif\n\
//#ifndef SMOOTH_TIPS\n\
//    vec4 maximos = max(max(fx30, fx60), fx45);\n\
//#endif\n\
\n\
    vec3 res1 = E;\n\
    res1 = mix(res1, mix(H, F, px.x), maximos.x);\n\
    res1 = mix(res1, mix(B, D, px.z), maximos.z);\n\
\n\
    vec3 res2 = E;\n\
    res2 = mix(res2, mix(F, B, px.y), maximos.y);\n\
    res2 = mix(res2, mix(D, H, px.w), maximos.w);\n\
\n\
    vec3 res = mix(res1, res2, step(c_df(E, res1), c_df(E, res2)));\n\
    float texel_alpha = sample_texel(tc).a;\n\
\n\
    return vec4(res, texel_alpha);\n\
}\n\
"
#endif

#ifdef FILTER_3POINT
"\n\
vec4 get_texel_3point()\n\
{\n\
  float x = frag_texture_coord.x;\n\
  float y = frag_texture_coord.y;\n\
\n\
  float u_frac = fract(x);\n\
  float v_frac = fract(y);\n\
\n\
  vec4 texel_00;\n\
\n\
  if (u_frac + v_frac < 1.0) {\n\
    // Use bottom-left\n\
    texel_00 = sample_texel(vec2(x + 0, y + 0));\n\
  } else {\n\
    // Use top-right\n\
    texel_00 = sample_texel(vec2(x + 1, y + 1));\n\
\n\
    float tmp = 1 - v_frac;\n\
    v_frac = 1 - u_frac;\n\
    u_frac = tmp;\n\
  }\n\
\n\
   vec4 texel_10 = sample_texel(vec2(x + 1, y + 0));\n\
   vec4 texel_01 = sample_texel(vec2(x + 0, y + 1));\n\
\n\
   vec4 texel = texel_00\n\
     + u_frac * (texel_10 - texel_00)\n\
     + v_frac * (texel_01 - texel_00);\n\
\n\
   return texel;\n\
}\n\
"
#endif

#ifdef FILTER_BILINEAR
"\n\
// Bilinear filtering\n\
vec4 get_texel_bilinear()\n\
{\n\
  float x = frag_texture_coord.x;\n\
  float y = frag_texture_coord.y;\n\
\n\
  float u_frac = fract(x);\n\
  float v_frac = fract(y);\n\
\n\
  vec4 texel_00 = sample_texel(vec2(x + 0, y + 0));\n\
  vec4 texel_10 = sample_texel(vec2(x + 1, y + 0));\n\
  vec4 texel_01 = sample_texel(vec2(x + 0, y + 1));\n\
  vec4 texel_11 = sample_texel(vec2(x + 1, y + 1));\n\
\n\
   vec4 texel = texel_00 * (1. - u_frac) * (1. - v_frac)\n\
     + texel_10 * u_frac * (1. - v_frac)\n\
     + texel_01 * (1. - u_frac) * v_frac\n\
     + texel_11 * u_frac * v_frac;\n\
\n\
   return texel;\n\
}\n\
"
#endif

#ifdef FILTER_JINC2
"\n\
const float JINC2_WINDOW_SINC = 0.44;\n\
const float JINC2_SINC = 0.82;\n\
const float JINC2_AR_STRENGTH = 0.8;\n\
\n\
const   float halfpi            = 1.5707963267948966192313216916398;\n\
const   float pi                = 3.1415926535897932384626433832795;\n\
const   float wa                = 1.382300768;\n\
const   float wb                = 2.576105976;\n\
\n\
// Calculates the distance between two points\n\
float d(vec2 pt1, vec2 pt2)\n\
{\n\
  vec2 v = pt2 - pt1;\n\
  return sqrt(dot(v,v));\n\
}\n\
\n\
vec3 min4(vec3 a, vec3 b, vec3 c, vec3 d)\n\
{\n\
    return min(a, min(b, min(c, d)));\n\
}\n\
\n\
vec3 max4(vec3 a, vec3 b, vec3 c, vec3 d)\n\
{\n\
    return max(a, max(b, max(c, d)));\n\
}\n\
\n\
vec4 resampler(vec4 x)\n\
{\n\
   vec4 res;\n\
\n\
   res = (x==vec4(0.0, 0.0, 0.0, 0.0)) ?  vec4(wa*wb)  :  sin(x*wa)*sin(x*wb)/(x*x);\n\
\n\
   return res;\n\
}\n\
\n\
vec4 get_texel_jinc2()\n\
{\n\
    vec3 color;\n\
    vec4 weights[4];\n\
\n\
    vec2 dx = vec2(1.0, 0.0);\n\
    vec2 dy = vec2(0.0, 1.0);\n\
\n\
    vec2 pc = frag_texture_coord.xy;\n\
\n\
    vec2 tc = (floor(pc-vec2(0.5,0.5))+vec2(0.5,0.5));\n\
\n\
    weights[0] = resampler(vec4(d(pc, tc    -dx    -dy), d(pc, tc           -dy), d(pc, tc    +dx    -dy), d(pc, tc+2.0*dx    -dy)));\n\
    weights[1] = resampler(vec4(d(pc, tc    -dx       ), d(pc, tc              ), d(pc, tc    +dx       ), d(pc, tc+2.0*dx       )));\n\
    weights[2] = resampler(vec4(d(pc, tc    -dx    +dy), d(pc, tc           +dy), d(pc, tc    +dx    +dy), d(pc, tc+2.0*dx    +dy)));\n\
    weights[3] = resampler(vec4(d(pc, tc    -dx+2.0*dy), d(pc, tc       +2.0*dy), d(pc, tc    +dx+2.0*dy), d(pc, tc+2.0*dx+2.0*dy)));\n\
\n\
    dx = dx;\n\
    dy = dy;\n\
    tc = tc;\n\
\n\
    vec3 c00 = sample_texel(tc    -dx    -dy).xyz;\n\
    vec3 c10 = sample_texel(tc           -dy).xyz;\n\
    vec3 c20 = sample_texel(tc    +dx    -dy).xyz;\n\
    vec3 c30 = sample_texel(tc+2.0*dx    -dy).xyz;\n\
    vec3 c01 = sample_texel(tc    -dx       ).xyz;\n\
    vec3 c11 = sample_texel(tc              ).xyz;\n\
    vec3 c21 = sample_texel(tc    +dx       ).xyz;\n\
    vec3 c31 = sample_texel(tc+2.0*dx       ).xyz;\n\
    vec3 c02 = sample_texel(tc    -dx    +dy).xyz;\n\
    vec3 c12 = sample_texel(tc           +dy).xyz;\n\
    vec3 c22 = sample_texel(tc    +dx    +dy).xyz;\n\
    vec3 c32 = sample_texel(tc+2.0*dx    +dy).xyz;\n\
    vec3 c03 = sample_texel(tc    -dx+2.0*dy).xyz;\n\
    vec3 c13 = sample_texel(tc       +2.0*dy).xyz;\n\
    vec3 c23 = sample_texel(tc    +dx+2.0*dy).xyz;\n\
    vec3 c33 = sample_texel(tc+2.0*dx+2.0*dy).xyz;\n\
\n\
    color = sample_texel(frag_texture_coord.xy).xyz;\n\
\n\
    //  Get min/max samples\n\
    vec3 min_sample = min4(c11, c21, c12, c22);\n\
    vec3 max_sample = max4(c11, c21, c12, c22);\n\
/*\n\
      color = mat4x3(c00, c10, c20, c30) * weights[0];\n\
      color+= mat4x3(c01, c11, c21, c31) * weights[1];\n\
      color+= mat4x3(c02, c12, c22, c32) * weights[2];\n\
      color+= mat4x3(c03, c13, c23, c33) * weights[3];\n\
      mat4 wgts = mat4(weights[0], weights[1], weights[2], weights[3]);\n\
      vec4 wsum = wgts * vec4(1.0,1.0,1.0,1.0);\n\
      color = color/(dot(wsum, vec4(1.0,1.0,1.0,1.0)));\n\
*/\n\
\n\
\n\
    color = vec3(dot(weights[0], vec4(c00.x, c10.x, c20.x, c30.x)), dot(weights[0], vec4(c00.y, c10.y, c20.y, c30.y)), dot(weights[0], vec4(c00.z, c10.z, c20.z, c30.z)));\n\
    color+= vec3(dot(weights[1], vec4(c01.x, c11.x, c21.x, c31.x)), dot(weights[1], vec4(c01.y, c11.y, c21.y, c31.y)), dot(weights[1], vec4(c01.z, c11.z, c21.z, c31.z)));\n\
    color+= vec3(dot(weights[2], vec4(c02.x, c12.x, c22.x, c32.x)), dot(weights[2], vec4(c02.y, c12.y, c22.y, c32.y)), dot(weights[2], vec4(c02.z, c12.z, c22.z, c32.z)));\n\
    color+= vec3(dot(weights[3], vec4(c03.x, c13.x, c23.x, c33.x)), dot(weights[3], vec4(c03.y, c13.y, c23.y, c33.y)), dot(weights[3], vec4(c03.z, c13.z, c23.z, c33.z)));\n\
    color = color/(dot(weights[0], vec4(1,1,1,1)) + dot(weights[1], vec4(1,1,1,1)) + dot(weights[2], vec4(1,1,1,1)) + dot(weights[3], vec4(1,1,1,1)));\n\
\n\
    // Anti-ringing\n\
    vec3 aux = color;\n\
    color = clamp(color, min_sample, max_sample);\n\
    color = mix(aux, color, JINC2_AR_STRENGTH);\n\
\n\
    // final sum and weight normalization\n\
    vec4 texel = vec4(color, 1.0);\n\
    return texel;\n\
}\n\
"
#endif
"\n\
void main() {\n\
   vec4 color;\n\
\n\
      if (frag_texture_blend_mode == BLEND_MODE_NO_TEXTURE)\n\
      {\n\
         color = vec4(frag_shading_color, 0.);\n\
      }\n\
      else\n\
      {\n\
         vec4 texel;\n\
         vec4 texel0 = sample_texel(vec2(frag_texture_coord.x,\n\
                  frag_texture_coord.y));\n\
"
#if defined(FILTER_SABR)
"\n\
		texel = get_texel_sabr();\n\
"
#elif defined(FILTER_XBR)
"\n\
         texel = get_texel_xbr();\n\
"
#elif defined(FILTER_BILINEAR)
"\n\
         texel = get_texel_bilinear();\n\
"
#elif defined(FILTER_3POINT)
"\n\
         texel = get_texel_3point();\n\
"
#elif defined(FILTER_JINC2)
"\n\
         texel = get_texel_jinc2();\n\
"
#else
"\n\
         texel = texel0; //use nearest if nothing else is chosen\n\
"
#endif
"\n\
	 // texel color 0x0000 is always fully transparent (even for opaque\n\
         // draw commands)\n\
         if (is_transparent(texel0)) {\n\
	   // Fully transparent texel, discard\n\
	   discard;\n\
         }\n\
\n\
         // Bit 15 (stored in the alpha) is used as a flag for\n\
         // semi-transparency, but only if this is a semi-transparent draw\n\
         // command\n\
         uint transparency_flag = uint(floor(texel0.a + 0.5));\n\
\n\
         uint is_texel_semi_transparent = transparency_flag & frag_semi_transparent;\n\
\n\
         if (is_texel_semi_transparent != draw_semi_transparent) {\n\
            // We're not drawing those texels in this pass, discard\n\
            discard;\n\
         }\n\
\n\
         if (frag_texture_blend_mode == BLEND_MODE_RAW_TEXTURE) {\n\
            color = texel;\n\
         } else /* BLEND_MODE_TEXTURE_BLEND */ {\n\
            // Blend the texel with the shading color. `frag_shading_color`\n\
            // is multiplied by two so that it can be used to darken or\n\
            // lighten the texture as needed. The result of the\n\
            // multiplication should be saturated to 1.0 (0xff) but I think\n\
            // OpenGL will take care of that since the output buffer holds\n\
            // integers. The alpha/mask bit bit is taken directly from the\n\
            // texture however.\n\
            color = vec4(frag_shading_color * 2. * texel.rgb, texel.a);\n\
         }\n\
      }\n\
\n\
   // 4x4 dithering pattern scaled by `dither_scaling`\n\
   uint x_dither = (uint(gl_FragCoord.x) / dither_scaling) & 3u;\n\
   uint y_dither = (uint(gl_FragCoord.y) / dither_scaling) & 3u;\n\
\n\
   // The multiplication by `frag_dither` will result in\n\
   // `dither_offset` being 0 if dithering is disabled\n\
   int dither_offset =\n\
      dither_pattern[y_dither * 4u + x_dither] * int(frag_dither);\n\
\n\
   float dither = float(dither_offset) / 255.;\n\
\n\
   frag_color = color + vec4(dither, dither, dither, 0.);\n\
}\n\
";

#undef command_fragment_name_
