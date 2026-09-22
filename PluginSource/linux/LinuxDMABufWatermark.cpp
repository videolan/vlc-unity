#include "LinuxDMABufWatermark.h"

#if defined(SHOW_WATERMARK)
#include "RenderAPI_OpenGLWatermark.h"
#include "TrialWatermark.h"
#endif

bool LinuxDMABufWatermarkSetup(ILinuxDMABufProducerContext& context,
                                OpenGLWatermark* watermark)
{
#if defined(SHOW_WATERMARK)
    if (!watermark || !context.producerMakeCurrent(true))
        return false;
    const bool result = watermark->setup();
    context.producerMakeCurrent(false);
    return result;
#else
    (void)context;
    (void)watermark;
    return true;
#endif
}

void LinuxDMABufWatermarkCleanup(ILinuxDMABufProducerContext& context,
                                  OpenGLWatermark* watermark)
{
#if defined(SHOW_WATERMARK)
    if (watermark && context.producerMakeCurrent(true)) {
        watermark->cleanup();
        context.producerMakeCurrent(false);
    }
#else
    (void)context;
    (void)watermark;
#endif
}

bool LinuxDMABufWatermarkBeforeSwap(OpenGLWatermark* watermark,
                                     GLuint framebuffer,
                                     unsigned width,
                                     unsigned height)
{
#if defined(SHOW_WATERMARK)
    if (!watermark || !libvlc_unity_trial_allows_frame())
        return false;
    watermark->draw(framebuffer, width, height);
#else
    (void)watermark;
    (void)framebuffer;
    (void)width;
    (void)height;
#endif
    return true;
}
