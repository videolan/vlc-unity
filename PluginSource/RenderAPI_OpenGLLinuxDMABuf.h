#ifndef RENDER_API_OPENGL_LINUX_DMABUF_H
#define RENDER_API_OPENGL_LINUX_DMABUF_H

#include "RenderAPI_OpenGLBase.h"
#include <cstddef>
#include <cstdint>

typedef void (*PFNGLMEMORYOBJECTPARAMETERIVEXTPROC_)(GLuint, GLenum, const GLint*);
typedef void (*PFNGLGENTEXTURESPROC_RAW)(GLsizei, GLuint*);
typedef void (*PFNGLBINDTEXTUREPROC_RAW)(GLenum, GLuint);
typedef void (*PFNGLTEXPARAMETERIPROC_RAW)(GLenum, GLenum, GLint);
typedef void (*PFNGLDELETETEXTURESPROC_RAW)(GLsizei, const GLuint*);

typedef void* (*LinuxGLProcLoader)(const char* name, void* data);

bool LinuxGLHasExtensions(const char* logPrefix,
                          LinuxGLProcLoader loadProc,
                          void* loadProcData,
                          const char* const* requiredExtensions,
                          size_t requiredExtensionCount);

bool LinuxGLLoadMemoryObjectFunctions(const char* logPrefix,
                                      LinuxGLProcLoader loadProc,
                                      void* loadProcData,
                                      PFNGLCREATEMEMORYOBJECTSEXTPROC& glCreateMemoryObjectsEXT,
                                      PFNGLTEXSTORAGEMEM2DEXTPROC& glTexStorageMem2DEXT,
                                      PFNGLIMPORTMEMORYFDEXTPROC& glImportMemoryFdEXT,
                                      PFNGLDELETEMEMORYOBJECTSEXTPROC& glDeleteMemoryObjectsEXT,
                                      PFNGLMEMORYOBJECTPARAMETERIVEXTPROC_& glMemoryObjectParameterivEXT,
                                      PFNGLGENTEXTURESPROC_RAW& raw_glGenTextures,
                                      PFNGLBINDTEXTUREPROC_RAW& raw_glBindTexture,
                                      PFNGLTEXPARAMETERIPROC_RAW& raw_glTexParameteri,
                                      PFNGLDELETETEXTURESPROC_RAW& raw_glDeleteTextures);

bool LinuxGLImportMemoryFd(const char* logPrefix,
                           PFNGLCREATEMEMORYOBJECTSEXTPROC glCreateMemoryObjectsEXT,
                           PFNGLIMPORTMEMORYFDEXTPROC glImportMemoryFdEXT,
                           PFNGLDELETEMEMORYOBJECTSEXTPROC glDeleteMemoryObjectsEXT,
                           PFNGLMEMORYOBJECTPARAMETERIVEXTPROC_ glMemoryObjectParameterivEXT,
                           PFNGLTEXSTORAGEMEM2DEXTPROC glTexStorageMem2DEXT,
                           GLuint& memObj,
                           GLuint tex,
                           int dmabufFd,
                           uint64_t size,
                           unsigned width,
                           unsigned height,
                           const char* label);

#endif /* RENDER_API_OPENGL_LINUX_DMABUF_H */
