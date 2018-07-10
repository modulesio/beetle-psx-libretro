#include "shaders_common.h"

static const char *image_load_vertex = GLSL_VERTEX "\n\
      // Vertex shader for uploading textures from the VRAM texture buffer\n\
      // into the output framebuffer\n\
\n\
      // The position in the input and output framebuffer are the same\n\
      in uvec2 position;\n\
      out vec2 frag_fb_coord;\n\
\n\
      void main() {\n\
      // Convert VRAM position into OpenGL coordinates\n\
      float xpos = (float(position.x) / 512.) - 1.0;\n\
      float ypos = (float(position.y) / 256.) - 1.0;\n\
\n\
      gl_Position.xyzw = vec4(xpos, ypos, 0.0, 1.0);\n\
\n\
      // frag_fb_coord will remain in PlayStation fb coordinates for\n\
      // texelFetch\n\
      frag_fb_coord = vec2(position);\n\
      }\n\
";
