#include "RenderAPI_OpenGLBase.h"
#include "Log.h"

RenderAPI_OpenGLBase::RenderAPI_OpenGLBase(UnityGfxRenderer apiType)
	: m_APIType(apiType)
{
    glewExperimental=true;
    if (glewInit() != GLEW_OK) {
        DEBUG("unable to initialise GLEW");
    } else {
        DEBUG("GLEW init OK");
    }

    glGetError();
}

void RenderAPI_OpenGLBase::create_fbo(void* data, size_t width, size_t height)
{
    DEBUG("create_fbo %p, %lu x %lu", data, width, height);
    RenderAPI_OpenGLBase* that = reinterpret_cast<RenderAPI_OpenGLBase*>(data);
    glGenTextures(3, that->tex);
    glGenFramebuffersEXT(3, that->fbo);

    for (int i = 0; i < 3; i++) {
        glBindTexture(GL_TEXTURE_2D, that->tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_INT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        DEBUG("bind FBO");
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, that->fbo[i]);

        DEBUG("FB Texture 2D");
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, that->tex[i], 0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);

    if (status != GL_FRAMEBUFFER_COMPLETE_EXT) {
        DEBUG("failed to create the FBO");
        return;
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}

void RenderAPI_OpenGLBase::destroy_fbo(void* data)
{
    DEBUG("destroy_fbo");
    RenderAPI_OpenGLBase* that = reinterpret_cast<RenderAPI_OpenGLBase*>(data);
    glDeleteTextures(3, that->tex);
    glDeleteFramebuffersEXT(3, that->fbo);
}

void RenderAPI_OpenGLBase::render_fbo(void* data, bool enter)
{
    RenderAPI_OpenGLBase* that = reinterpret_cast<RenderAPI_OpenGLBase*>(data);
    if (enter) {
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, that->fbo[that->idx_render]);
    } else {
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, 0);
        {
            std::lock_guard<std::mutex> lock(that->text_lock);
            that->updated = true;
            std::swap(that->idx_swap, that->idx_render);
        }
    }
}

void* RenderAPI_OpenGLBase::getVideoFrame(bool* out_updated)
{
    std::lock_guard<std::mutex> lock(text_lock);
    if (out_updated)
        *out_updated = updated;
    if (updated)
    {
        std::swap(idx_swap, idx_display);
        updated = false;
    }
    return (void*)(size_t)tex[idx_display];
}

void* RenderAPI_OpenGLBase::BeginModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int* outRowPitch)
{
    return nullptr;
}


void RenderAPI_OpenGLBase::EndModifyTexture(void* textureHandle, int textureWidth, int textureHeight, int rowPitch, void* dataPtr)
{
    return;
}
