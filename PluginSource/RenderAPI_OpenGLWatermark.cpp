#include "RenderAPI_OpenGLWatermark.h"
#include "watermark.raw.h"
#include <string>
#include <cassert>
#include <random>
#include <chrono>
#include "Log.h"

namespace {

static const int imageWidth = 600;
static const int imageHeight = 180;
static const float scale = 2.0f;

void checkCompileErrors(GLuint shader, std::string type)
{
    GLint success;
    GLchar infoLog[1024];
    if(type == "PROGRAM")
    {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if(!success)
        {
            glGetProgramInfoLog(shader, 1024, NULL, infoLog);
            DEBUG("ERROR::PROGRAM_LINKING_ERROR of type: %s: %s ", type.c_str(),  infoLog);
        }

    }
    else
    {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if(!success)
        {
            glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            DEBUG("ERROR::SHADER error of type: %s: %s ", type.c_str(),  infoLog);
        }
    }
}

}


bool OpenGLWatermark::setup()
{
    //load the external texture here
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    assert(imageWidth*imageHeight*4 == watermark_raw_len);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageWidth, imageHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, watermark_raw);

    //I'm not sure we need a mipmap
 //   glGenerateMipmap(GL_TEXTURE_2D); // this gives an error 502 on iOS

    //create OpenGL program
    static const char *vertexShaderSource = R"raw(
attribute vec2 aPos;
attribute vec2 aUV;
varying vec2 texCoordinate;

void main()
{
  texCoordinate = aUV;
  gl_Position = vec4(aPos, 0.0, 1.0);
}
)raw";

    static const char *fragmentShaderSource = R"raw(
#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D texture;
varying vec2 texCoordinate;

void main()
{
   gl_FragColor = texture2D(texture, texCoordinate);
}
)raw";

    // vertex shader
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertexShaderSource, NULL);
    glCompileShader(vertex);
    checkCompileErrors(vertex, "VERTEX");

    // fragment Shader
    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragment);
    checkCompileErrors(fragment, "FRAGMENT");

    //build the program
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    checkCompileErrors(program, "PROGRAM");

    //shaders are no longer needed as they are linked to our program
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    //use the program to bind our shaders attributes
    glUseProgram(program);

    //create and bind our geometry
    glGenBuffers(1, &vbo);
    
    //vertex position attributes
    posAttrib = glGetAttribLocation(program, "aPos");
    //texture position attributes
    uvAttrib = glGetAttribLocation(program, "aUV");
    
    // Initialize vertex buffer with random position
    randomizePosition();

    // Specify the texture of the watermark
    GLint textUniform = glGetUniformLocation(program, "texture");
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(textUniform, /*GL_TEXTURE*/0);

    //we want alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    //unuse our program
    glUseProgram(0);

    return true;
}


void OpenGLWatermark::cleanup()
{
    if (tex != 0) {
        glDeleteTextures(1, &tex);
        tex = 0;
    }
    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (program != 0) {
        glDeleteProgram(program);
        program = 0;
    }
}

void OpenGLWatermark::draw(GLuint framebuffer, unsigned width, unsigned height)
{
        //save current OpenGL state, we need to restore this after drawing
    GLint oldProgram, oldVbo, oldTexture;
    GLboolean oldBlend;
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldVbo);
    glGetBooleanv(GL_BLEND, &oldBlend);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);

    GLint oldViewport[4];
    glGetIntegerv(GL_VIEWPORT, oldViewport);

    //load our program to draw the watermark
    glUseProgram(program);

    //we want alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    //bind the frame buffer were we're going to draw, is it superfluous ?
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    //set the viewport over the whole framebuffer
    glViewport(0, 0, width, height);

    //bind the texture
    glActiveTexture(GL_TEXTURE0); //should we store and restrore the active texture?
    glBindTexture(GL_TEXTURE_2D, tex);

    //bind the geometry
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glEnableVertexAttribArray(posAttrib);
    glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE,
        4*sizeof(float), 0);
    glEnableVertexAttribArray(uvAttrib);
    glVertexAttribPointer(uvAttrib, 2, GL_FLOAT, GL_FALSE,
        4*sizeof(float), (GLvoid*)(2*sizeof(float)));

    //draw our wartermark
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    //restore previous state
    glUseProgram(oldProgram);
    glBindBuffer(GL_ARRAY_BUFFER, oldVbo);
    glBindTexture(GL_TEXTURE_2D, oldTexture);
    if (!oldBlend)
        glDisable(GL_BLEND);
    glViewport(oldViewport[0], oldViewport[1], (GLsizei)oldViewport[2], (GLsizei)oldViewport[3]);
}

void OpenGLWatermark::randomizePosition()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    
    std::uniform_int_distribution<int> positionDist(0, 2);
    int position = positionDist(gen);
    
    xOffset = -scale * 0.5f;
    
    switch(position) {
        case 0: // Center top
            yOffset = -0.4f;
            break;
        case 1: // Center
            yOffset = 0.2f;
            break;
        case 2: // Center bottom
           yOffset = 0.7f;
            break;
    }
    
    updateVertexBuffer();
}

void OpenGLWatermark::updateVertexBuffer()
{
    static const float xScale = scale;
    static const float yScale = (xScale * imageHeight) / imageWidth;

    GLfloat vertices[] = {
        //aPos X               , aPos Y                 , aUV X  , aUV Y
        0.f * xScale + xOffset , -1.f * yScale + yOffset, 1.f    , 0.f,
        0.f * xScale + xOffset ,  0.f * yScale + yOffset, 1.f    , 1.f,
        1.f * xScale + xOffset , -1.f * yScale + yOffset, 0.f    , 0.f,
        1.f * xScale + xOffset ,  0.f * yScale + yOffset, 0.f    , 1.f
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(posAttrib);
    glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE,
        4*sizeof(float), 0);

    glEnableVertexAttribArray(uvAttrib);
    glVertexAttribPointer(uvAttrib, 2, GL_FLOAT, GL_FALSE,
        4*sizeof(float), (GLvoid*)(2*sizeof(float)));
}
