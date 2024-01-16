/*****************************************************************************
 * RenderAPI_OpenGLCGL.mm: MacOS OpenGL renderer to Metal
 *****************************************************************************
 * Copyright (C) 2023 Videolabs
 *
 * Authors: Alexandre Janniaux <ajanni@videolabs.io>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#import <TargetConditionals.h>

/* VLCUnity includes */
#include "RenderAPI_OpenGLCGL.h"
#include "Log.h"

/* Unity includes */
#include "Unity/IUnityGraphicsMetal.h"

/* MacOS frameworks */
#import <Cocoa/Cocoa.h>
#import <CoreVideo/CoreVideo.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>

/* Standard library */
#import <cassert>
#import <dlfcn.h>
#import <stdexcept>

/* Libvlc includes */
#import <vlc/libvlc_media_player.h>

namespace {

bool makeCurrent(void* data, bool current)
{
    auto that = static_cast<RenderAPI_OpenGLCGL*>(data);
    return that->makeCurrent(current);
}

void swap(void *data)
{
    auto that = static_cast<RenderAPI_OpenGLCGL*>(data);
    that->swap();
}

}

RenderAPI* CreateRenderAPI_OpenGLCGL(UnityGfxRenderer apiType)
{
    return new RenderAPI_OpenGLCGL(apiType);
}

RenderAPI_OpenGLCGL::RenderAPI_OpenGLCGL(UnityGfxRenderer apiType)
{
    DEBUG("[GLCGL] Entering OpenGLCGL constructor");

    CGLPixelFormatAttribute pixel_attr[] = {
        kCGLPFAAccelerated,
        kCGLPFAAllowOfflineRenderers,
        static_cast<CGLPixelFormatAttribute>(0),
    };

    CGLPixelFormatObj pixelFormat;
    GLint numPixelFormats = 0;

    CGLChoosePixelFormat(pixel_attr, &pixelFormat, &numPixelFormats);
    CGLError cglerr = CGLCreateContext(pixelFormat, NULL, &m_context);
    CGLDestroyPixelFormat(pixelFormat);
    if (cglerr != kCGLNoError)
    {
        DEBUG("[GLCGL] Cannot allocate CGL context");
        throw std::runtime_error("[GLCGL] Cannot allocate CGL context");
    }

    int cvret = CVOpenGLTextureCacheCreate(kCFAllocatorDefault,
        nil, m_context, pixelFormat, nil, &_textureCache);
    (void)cvret;
}

void RenderAPI_OpenGLCGL::swap()
{
    //DEBUG("[GLCGL] swapping");
    std::lock_guard<std::mutex> lock(text_lock);
    updated = true;

#ifdef SHOW_WATERMARK
    watermark.draw(that->fbo[that->idx_render], that->width, that->height);
#endif

    std::swap(idx_swap, idx_render);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo[idx_render]);
}


bool RenderAPI_OpenGLCGL::makeCurrent(bool current)
{
    int ret;
    if (current)
    {
        ret = CGLSetCurrentContext(m_context);
        if (ret)
        {
            DEBUG("[GLCGL] make current failed");
            return false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[idx_render]);
    }
    else
    {
        assert(CGLGetCurrentContext() == m_context);
        ret = CGLSetCurrentContext(nil);
        if (ret)
            return false;
    }

    return true;
}

void* RenderAPI_OpenGLCGL::get_proc_address(void* /*data*/, const char* procname)
{
    return reinterpret_cast<void*>(dlsym(RTLD_DEFAULT, procname));
}

void RenderAPI_OpenGLCGL::setVlcContext(libvlc_media_player_t *mp)
{
    DEBUG("[GLCGL] subscribing to opengl output callbacks %p", this);
    libvlc_video_set_output_callbacks(mp, libvlc_video_engine_opengl,
        setup, cleanup, nullptr, resize, ::swap,
        ::makeCurrent, get_proc_address, nullptr, nullptr, this);
}

bool RenderAPI_OpenGLCGL::setup(void **opaque, const libvlc_video_setup_device_cfg_t *cfg, libvlc_video_setup_device_info_t *out)
{
    (void)cfg; (void)out;
    DEBUG("output callback setup");

    auto *that = static_cast<RenderAPI_OpenGLCGL*>(*opaque);
    that->width = 0;
    that->height = 0;

    return true;
}

void RenderAPI_OpenGLCGL::cleanup(void* opaque)
{
    DEBUG("output callback cleanup");

    auto *that = static_cast<RenderAPI_OpenGLCGL*>(opaque);
    that->releaseFrameBufferResources();
}

bool RenderAPI_OpenGLCGL::resize(void* opaque, const libvlc_video_render_cfg_t *cfg, libvlc_video_output_cfg_t *output)
{
    DEBUG("[GLCGL] call resize");
    auto *that = static_cast<RenderAPI_OpenGLCGL*>(opaque);

    if (cfg->width != that->width || cfg->height != that->height)
        that->releaseFrameBufferResources();

    for (auto& buffer : that->buffers)
        buffer = that->initBuffer(cfg->width, cfg->height);

    glGenFramebuffers(3, that->fbo);

#ifndef GL_TEXTURE_RECTANGLE
# define GL_TEXTURE_RECTANGLE 0x84F5
#endif

    for (int i = 0; i < 3; i++) {
        DEBUG("bind FBO");

        glBindTexture(GL_TEXTURE_RECTANGLE, that->buffers[i].texture_name);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindFramebuffer(GL_FRAMEBUFFER, that->fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, that->buffers[i].texture_name, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

        if (status != GL_FRAMEBUFFER_COMPLETE) {
            DEBUG("failed to create the FBO");
            return false;
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    that->width = cfg->width;
    that->height = cfg->height;

    glBindFramebuffer(GL_FRAMEBUFFER, that->fbo[that->idx_render]);

    output->opengl_format = GL_RGBA;
    output->full_range = true;
    output->colorspace = libvlc_video_colorspace_BT709;
    output->primaries  = libvlc_video_primaries_BT709;
    output->transfer   = libvlc_video_transfer_func_SRGB;
    output->orientation = libvlc_video_orient_bottom_right;

    return true;
}

RenderAPICoreVideoBuffer RenderAPI_OpenGLCGL::initBuffer(unsigned width, unsigned height)
{
    RenderAPICoreVideoBuffer buffer;

    /* CoreVideo functions will use this variable for error reporting. */
    CVReturn cvret;

    NSDictionary *cvpx_attr = @{
        (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (__bridge NSString*)kCVPixelBufferWidthKey: @(width),
        (__bridge NSString*)kCVPixelBufferHeightKey: @(height),
        (__bridge NSString*)kCVPixelBufferBytesPerRowAlignmentKey: @(16),
        /* Necessary for having iosurface-backed CVPixelBuffer, but
         * note that iOS simulator won't be able to display them. */
        (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
    };

    cvret = CVPixelBufferCreate(kCFAllocatorDefault, width, height,
        kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef)cvpx_attr,
        &buffer.cvpx);

    if (cvret != kCVReturnSuccess)
        throw std::runtime_error("[GLCGL] Cannot create CVPixelBuffer");

    cvret = CVOpenGLTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
            _textureCache, buffer.cvpx,
            nil, &buffer.texture);

    if (cvret != kCVReturnSuccess)
        throw std::runtime_error("[GLCGL] Cannot bind CVPixelBuffer to OpenGL");

    GLenum target = CVOpenGLTextureGetTarget(buffer.texture);
    GLuint name = CVOpenGLTextureGetName(buffer.texture);
    assert(target == GL_TEXTURE_RECTANGLE);
    buffer.texture_name = name;

    cvret = CVMetalTextureCacheCreateTextureFromImage(
        nil, _textureCacheMetal, buffer.cvpx, nil, MTLPixelFormatBGRA8Unorm,
        width, height, 0, &buffer.texture_metal);

    if (cvret != kCVReturnSuccess)
        throw std::runtime_error("[GLCGL] Cannot bind CVPixelBuffer to Metal");

    /* Move buffer to client */
    return buffer;
}

void RenderAPI_OpenGLCGL::ProcessDeviceEvent(UnityGfxDeviceEventType type, IUnityInterfaces* interfaces)
{
	if (type == kUnityGfxDeviceEventInitialize) {
      DEBUG("[GLCGL] Entering ProcessDeviceEvent with kUnityGfxDeviceEventInitialize");

      IUnityGraphicsMetal* gfx = interfaces->Get<IUnityGraphicsMetal>();
      if (gfx == nullptr)
          DEBUG("[GLMETAL] Could not retrieve IUnityGraphicsMetal\n");
      
      int cvret = CVMetalTextureCacheCreate(kCFAllocatorDefault,
          nil, gfx->MetalDevice(), nil, &_textureCacheMetal);

      (void)cvret; // TODO: what kind of error reporting?

	} else if (type == kUnityGfxDeviceEventShutdown) {
        DEBUG("[GLCGL] kUnityGfxDeviceEventShutdown");
	}
}

void* RenderAPI_OpenGLCGL::getVideoFrame(unsigned width, unsigned height, bool* out_updated)
{
    (void)width; (void)height;
    std::lock_guard<std::mutex> lock(text_lock);
    if (out_updated)
        *out_updated = updated;
    if (updated)
    {
        std::swap(idx_swap, idx_display);
        updated = false;
    }
    return CVMetalTextureGetTexture(buffers[idx_display].texture_metal);
}

void RenderAPI_OpenGLCGL::releaseFrameBufferResources()
{
    glDeleteFramebuffers(3, fbo);
}
