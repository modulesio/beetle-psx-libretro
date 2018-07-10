#include "shaders_common.h"

static const char *output_vertex = GLSL_VERTEX "\n\
   // Vertex shader for rendering GPU draw commands in the framebuffer\n\
   in vec2 position;\n\
   in uvec2 fb_coord;\n\
\n\
   out vec2 frag_fb_coord;\n\
\n\
   void main() {\n\
      gl_Position.xyzw = vec4(position, 0.0, 1.0);\n\
\n\
      // Convert the PlayStation framebuffer coordinate into an OpenGL\n\
      // texture coordinate\n\
      float fb_x_coord = float(fb_coord.x) / 1024.;\n\
      float fb_y_coord = float(fb_coord.y) / 512.;\n\
\n\
      frag_fb_coord = vec2(fb_x_coord, fb_y_coord);\n\
   }\n\
";
