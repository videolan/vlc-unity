

#if UNITY_IPHONE
#	include <OpenGLES/ES2/gl.h>
#elif UNITY_ANDROID || UNITY_WEBGL
#	include <GLES2/gl2.h>
#else
#   define GL_GLEXT_PROTOTYPES
#	include "GLEW/glew.h"
#endif


class OpenGLWatermark
{
public:
    bool setup();

    void cleanup();

    void draw(GLuint framebuffer, unsigned width, unsigned height);

private:
    GLuint program = 0;
    GLuint vbo = 0;
    GLuint tex = 0;
    GLuint posAttrib = 0;
    GLuint uvAttrib = 0;
};
