#ifndef RENDERAPI_OPENGLWATERMARK_H
#define RENDERAPI_OPENGLWATERMARK_H

#if defined(UNITY_IPHONE)
#	include <OpenGLES/ES2/gl.h>
#elif defined(UNITY_OSX)
#  include <OpenGL/GL.h>
#elif defined(UNITY_ANDROID) || defined(UNITY_WEBGL)
#	include <GLES2/gl2.h>
#elif defined(UNITY_WIN)
#   define GL_GLEXT_PROTOTYPES
#	include "GLEW/glew.h"
#else
# error "OpenGL not implemented"
#endif


class OpenGLWatermark
{
public:
    bool setup();

    void cleanup();

    void draw(GLuint framebuffer, unsigned width, unsigned height);

    void randomizePosition();

private:
    void updateVertexBuffer();

    GLuint program = 0;
    GLuint vbo = 0;
    GLuint tex = 0;
    GLuint posAttrib = 0;
    GLuint uvAttrib = 0;

    float xOffset = -1.f;
    float yOffset = 1.f;

    uint32_t frameCounter = 0;
    static const uint32_t REPOSITION_INTERVAL = 150; // Move watermark every ~2.5 seconds at 60fps
};

#endif /* RENDERAPI_OPENGLWATERMARK_H */
