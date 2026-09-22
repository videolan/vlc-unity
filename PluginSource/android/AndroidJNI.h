#pragma once

#include <jni.h>

jobject AndroidCreateAWindow(const char* logTag);
void AndroidDeleteGlobalRef(jobject object);
