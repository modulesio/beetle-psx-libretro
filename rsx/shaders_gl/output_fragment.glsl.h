#include "shaders_common.h"

static const char *output_fragment = GLSL_FRAGMENT "\n\
   // We're sampling from the internal framebuffer texture\n\
   uniform sampler2D fb;\n\
   // Framebuffer sampling: 0: Normal 16bpp mode, 1: Use 24bpp mode\n\
   uniform int depth_24bpp;\n\
   // Internal resolution upscaling factor. Necessary for proper 24bpp\n\
   // display since we need to know how the pixels are laid out in RAM.\n\
   uniform uint internal_upscaling;\n\
   // Coordinates of the top-left displayed pixel in VRAM (1x resolution)\n\
   uniform uvec2 offset;\n\
   // Normalized relative offset in the displayed area in VRAM. Absolute\n\
   // coordinates must take `offset` into account.\n\
   in vec2 frag_fb_coord;\n\
\n\
   out vec4 frag_color;\n\
\n\
   // Take a normalized color and convert it into a 16bit 1555 ABGR\n\
   // integer in the format used internally by the Playstation GPU.\n\
   uint rebuild_color(vec4 color) {\n\
      uint a = uint(floor(color.a + 0.5));\n\
      uint r = uint(floor(color.r * 31. + 0.5));\n\
      uint g = uint(floor(color.g * 31. + 0.5));\n\
      uint b = uint(floor(color.b * 31. + 0.5));\n\
\n\
      // return (r << 11) | (g << 6) | (b << 1) | a;\n\
      return (a << 15) | (b << 10) | (g << 5) | r;\n\
   }\n\
\n\
   void main() {\n\
      vec3 color;\n\
\n\
      if (depth_24bpp == 0) {\n\
         // Use the regular 16bpp mode, fetch directly from the framebuffer\n\
         // texture. The alpha/mask bit is ignored here.\n\
	vec2 off = vec2(offset) / vec2(1024., 512.);\n\
\n\
	color = texture(fb, frag_fb_coord + off).rgb;\n\
      } else {\n\
         // In this mode we have to interpret the framebuffer as containing\n\
         // 24bit RGB values instead of the usual 16bits 1555.\n\
         ivec2 fb_size = textureSize(fb, 0);\n\
\n\
         uint x_24 = uint(frag_fb_coord.x * float(fb_size.x));\n\
         uint y = uint(frag_fb_coord.y * float(fb_size.y));\n\
\n\
         uint x_native = x_24 / internal_upscaling;\n\
\n\
         x_24 = x_native * internal_upscaling;\n\
\n\
         // The 24bit color is stored over two 16bit pixels, convert the\n\
         // coordinates\n\
         uint x0_16 = (x_24 * 3u) / 2u;\n\
\n\
	 // Add the offsets\n\
	 x0_16 += offset.x * internal_upscaling;\n\
	 y     += offset.y * internal_upscaling;\n\
\n\
         // Move on to the next pixel at native resolution\n\
         uint x1_16 = x0_16 + internal_upscaling;\n\
\n\
         uint col0 = rebuild_color(texelFetch(fb, ivec2(x0_16, y), 0));\n\
         uint col1 = rebuild_color(texelFetch(fb, ivec2(x1_16, y), 0));\n\
\n\
         uint col = (col1 << 16) | col0;\n\
\n\
         // If we're drawing an odd 24 bit pixel we're starting in the\n\
         // middle of a 16bit cell so we need to adjust accordingly.\n\
         col >>= 8u * (x_native & 1u);\n\
\n\
         // Finally we can extract and normalize the 24bit pixel\n\
         float b = float((col >> 16u) & 0xffu) / 255.;\n\
         float g = float((col >> 8u) & 0xffu) / 255.;\n\
         float r = float(col & 0xffu) / 255.;\n\
\n\
         color = vec3(r, g, b);\n\
      }\n\
\n\
      frag_color = vec4(color, 1.0);\n\
   }\n\
";
