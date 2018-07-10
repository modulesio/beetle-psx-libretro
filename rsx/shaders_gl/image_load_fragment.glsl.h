#include "shaders_common.h"

static const char *image_load_fragment = GLSL_FRAGMENT "\n\
      uniform sampler2D fb_texture;\n\
      uniform uint internal_upscaling;\n\
      in vec2 frag_fb_coord;\n\
      out vec4 frag_color;\n\
\n\
      // Read a pixel in VRAM\n\
      vec4 vram_get_pixel(int x, int y) {\n\
	return texelFetch(fb_texture, ivec2(x, y) * int(internal_upscaling), 0);\n\
      }\n\
\n\
      void main() {\n\
      frag_color = vram_get_pixel(int(frag_fb_coord.x), int(frag_fb_coord.y));\n\
      }\n\
";
