#pragma once

#include "LinuxDMABufProducer.h"

class OpenGLWatermark;

bool LinuxDMABufWatermarkSetup(ILinuxDMABufProducerContext& context,
                                OpenGLWatermark* watermark);
void LinuxDMABufWatermarkCleanup(ILinuxDMABufProducerContext& context,
                                 OpenGLWatermark* watermark);
bool LinuxDMABufWatermarkBeforeSwap(OpenGLWatermark* watermark,
                                    GLuint framebuffer,
                                    unsigned width,
                                    unsigned height);
