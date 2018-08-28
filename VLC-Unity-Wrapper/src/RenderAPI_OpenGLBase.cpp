#include "RenderAPI_OpenGLBase.h"
#include "Log.h"

RenderAPI_OpenGLBase::RenderAPI_OpenGLBase(UnityGfxRenderer apiType)
	: m_APIType(apiType)
{
#if SUPPORT_OPENGL_CORE
    glewExperimental=true;
    if (glewInit() != GLEW_OK) {
        DEBUG("unable to initialise GLEW");
    } else {
        DEBUG("GLEW init OK");
    }
#endif
    glGetError();
}


bool RenderAPI_OpenGLBase::setup(void* data)
{
    RenderAPI_OpenGLBase* that = static_cast<RenderAPI_OpenGLBase*>(data);
    that->width = 0;
    that->height = 0;
    return true;
}

void RenderAPI_OpenGLBase::cleanup(void* data)
{
    DEBUG("destroy_fbo");
    RenderAPI_OpenGLBase* that = reinterpret_cast<RenderAPI_OpenGLBase*>(data);
    if (that->width == 0 && that->height == 0)
        return;

    glDeleteTextures(3, that->tex);
    glDeleteFramebuffers(3, that->fbo);
}


void RenderAPI_OpenGLBase::resize(void* data, unsigned width, unsigned height)
{
    DEBUG("create_fbo %p, %lu x %lu", data, width, height);
    RenderAPI_OpenGLBase* that = reinterpret_cast<RenderAPI_OpenGLBase*>(data);

    if (width != that->width || height != that->height)
        cleanup(data);

    glGenTextures(3, that->tex);
    glGenFramebuffers(3, that->fbo);

    for (int i = 0; i < 3; i++) {
        glBindTexture(GL_TEXTURE_2D, that->tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        DEBUG("bind FBO");
        glBindFramebuffer(GL_FRAMEBUFFER, that->fbo[i]);

        DEBUG("FB Texture 2D");
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, that->tex[i], 0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        DEBUG("failed to create the FBO");
        return;
    }

    that->width = width;
    that->height = height;

    glBindFramebuffer(GL_FRAMEBUFFER, that->fbo[that->idx_render]);
}

void RenderAPI_OpenGLBase::swap(void* data)
{
    RenderAPI_OpenGLBase* that = reinterpret_cast<RenderAPI_OpenGLBase*>(data);
    std::lock_guard<std::mutex> lock(that->text_lock);
    that->updated = true;
    std::swap(that->idx_swap, that->idx_render);
    glBindFramebuffer(GL_FRAMEBUFFER, that->fbo[that->idx_render]);
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
    //DEBUG("get Video Frame %u", tex[idx_display]);
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
