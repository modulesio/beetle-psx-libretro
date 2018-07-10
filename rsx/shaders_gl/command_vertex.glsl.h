#include "shaders_common.h"

#undef command_vertex_name_
#if defined(FILTER_SABR) || defined(FILTER_XBR)
#define command_vertex_name_ command_vertex_xbr
#else
#define command_vertex_name_ command_vertex
#endif

static const char * command_vertex_name_ = GLSL_VERTEX "\n\
// Vertex shader for rendering GPU draw commands in the framebuffer\n\
in vec4 position;\n\
in ivec3 color;\n\
in uvec2 texture_page;\n\
in uvec2 texture_coord;\n\
in uvec2 clut;\n\
in uint texture_blend_mode;\n\
in uint depth_shift;\n\
in uint dither;\n\
in uint semi_transparent;\n\
in uvec4 texture_window;\n\
in uvec4 texture_limits;\n\
\n\
// Drawing offset\n\
uniform ivec2 offset;\n\
\n\
out vec3 frag_shading_color;\n\
flat out uvec2 frag_texture_page;\n\
out vec2 frag_texture_coord;\n\
flat out uvec2 frag_clut;\n\
flat out uint frag_texture_blend_mode;\n\
flat out uint frag_depth_shift;\n\
flat out uint frag_dither;\n\
flat out uint frag_semi_transparent;\n\
flat out uvec4 frag_texture_window;\n\
flat out uvec4 frag_texture_limits;\n\
"
#if defined(FILTER_SABR) || defined(FILTER_XBR)
"\n\
out vec2 tc;\n\
out vec4 xyp_1_2_3;\n\
out vec4 xyp_6_7_8;\n\
out vec4 xyp_11_12_13;\n\
out vec4 xyp_16_17_18;\n\
out vec4 xyp_21_22_23;\n\
out vec4 xyp_5_10_15;\n\
out vec4 xyp_9_14_9;\n\
"
#endif
"\n\
void main() {\n\
   vec2 pos = position.xy + vec2(offset);\n\
\n\
   // Convert VRAM coordinates (0;1023, 0;511) into OpenGL coordinates\n\
   // (-1;1, -1;1)\n\
   float wpos = position.w;\n\
   float xpos = (pos.x / 512.0) - 1.0;\n\
   float ypos = (pos.y / 256.0) - 1.0;\n\
\n\
   // position.z increases as the primitives near the camera so we\n\
   // reverse the order to match the common GL convention\n\
   float zpos = 1.0 - (position.z / 32768.);\n\
\n\
   gl_Position.xyzw = vec4(xpos * wpos, ypos * wpos, zpos * wpos, wpos);\n\
   //gl_Position.xyzw = vec4(xpos, ypos, zpos, 1.);\n\
\n\
   // Glium doesn't support 'normalized' for now\n\
   frag_shading_color = vec3(color) / 255.;\n\
\n\
   // Let OpenGL interpolate the texel position\n\
   frag_texture_coord = vec2(texture_coord);\n\
\n\
   frag_texture_page = texture_page;\n\
   frag_clut = clut;\n\
   frag_texture_blend_mode = texture_blend_mode;\n\
   frag_depth_shift = depth_shift;\n\
   frag_dither = dither;\n\
   frag_semi_transparent = semi_transparent;\n\
   frag_texture_window = texture_window;\n\
   frag_texture_limits = texture_limits;\n\
"
#if defined(FILTER_SABR) || defined(FILTER_XBR)
"\n\
	tc = frag_texture_coord.xy;\n\
   xyp_1_2_3    = tc.xxxy + vec4(-1.,  0., 1., -2.);\n\
   xyp_6_7_8    = tc.xxxy + vec4(-1.,  0., 1., -1.);\n\
   xyp_11_12_13 = tc.xxxy + vec4(-1.,  0., 1.,  0.);\n\
   xyp_16_17_18 = tc.xxxy + vec4(-1.,  0., 1.,  1.);\n\
   xyp_21_22_23 = tc.xxxy + vec4(-1.,  0., 1.,  2.);\n\
   xyp_5_10_15  = tc.xyyy + vec4(-2., -1., 0.,  1.);\n\
   xyp_9_14_9   = tc.xyyy + vec4( 2., -1., 0.,  1.);\n\
"
#endif
"\n\
}\n\
";

#undef command_vertex_name_
