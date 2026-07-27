#include "RenderAPI_OpenGLLinuxDMABuf.h"
#include "Log.h"
#include <cstring>
#include <fcntl.h>
#include <gbm.h>
#include <unistd.h>

#ifndef GL_HANDLE_TYPE_OPAQUE_FD_EXT
#define GL_HANDLE_TYPE_OPAQUE_FD_EXT 0x9586
#endif
#ifndef GL_DEDICATED_MEMORY_OBJECT_EXT
#define GL_DEDICATED_MEMORY_OBJECT_EXT 0x9581
#endif

namespace {

bool extensionListContains(const char* extensions, const char* name)
{
    if (!extensions || !name)
        return false;

    const size_t nameLen = strlen(name);
    const char* current = extensions;
    while ((current = strstr(current, name)) != nullptr) {
        const bool startsAtBoundary = current == extensions || current[-1] == ' ';
        const char next = current[nameLen];
        const bool endsAtBoundary = next == '\0' || next == ' ';
        if (startsAtBoundary && endsAtBoundary)
            return true;
        current += nameLen;
    }
    return false;
}

bool markExtensionFound(const char* extension,
                        const char* const* requiredExtensions,
                        size_t requiredExtensionCount,
                        bool* found)
{
    bool changed = false;
    for (size_t i = 0; i < requiredExtensionCount; ++i) {
        if (!found[i] && strcmp(extension, requiredExtensions[i]) == 0) {
            found[i] = true;
            changed = true;
        }
    }
    return changed;
}

bool allFound(const bool* found, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (!found[i])
            return false;
    }
    return true;
}

} // namespace

LinuxGBMDevice::~LinuxGBMDevice()
{
    reset();
}

bool LinuxGBMDevice::open(const char* logPrefix, const std::string& path)
{
    reset();

    m_fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (m_fd < 0) {
        DEBUG("[%s] could not open DRM render node %s", logPrefix, path.c_str());
        return false;
    }

    m_device = gbm_create_device(m_fd);
    if (!m_device) {
        DEBUG("[%s] gbm_create_device failed for %s", logPrefix, path.c_str());
        reset();
        return false;
    }

    const char* backend = gbm_device_get_backend_name(m_device);
    DEBUG("[%s] DRM render node %s uses GBM backend %s",
          logPrefix, path.c_str(), backend ? backend : "?");
    return true;
}

void LinuxGBMDevice::reset()
{
    if (m_device) {
        gbm_device_destroy(m_device);
        m_device = nullptr;
    }
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
}

bool LinuxGLHasExtensions(const char* logPrefix,
                          LinuxGLProcLoader loadProc,
                          void* loadProcData,
                          const char* const* requiredExtensions,
                          size_t requiredExtensionCount)
{
    bool found[16] = {};
    if (requiredExtensionCount > sizeof(found) / sizeof(found[0])) {
        DEBUG("[%s] too many required GL extensions to check", logPrefix);
        return false;
    }

    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (extensions) {
        for (size_t i = 0; i < requiredExtensionCount; ++i)
            found[i] = extensionListContains(extensions, requiredExtensions[i]);
    }

    if (!allFound(found, requiredExtensionCount)) {
        typedef const GLubyte* (*PFNGLGETSTRINGIPROC)(GLenum, GLuint);
        auto glGetStringi_ = reinterpret_cast<PFNGLGETSTRINGIPROC>(
            loadProc ? loadProc("glGetStringi", loadProcData) : nullptr);
        if (glGetStringi_) {
            GLint numExtensions = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
            DEBUG("[%s] enumerating %d GL extensions", logPrefix, numExtensions);
            for (GLint i = 0; i < numExtensions; ++i) {
                const char* extension = reinterpret_cast<const char*>(glGetStringi_(GL_EXTENSIONS, i));
                if (extension)
                    markExtensionFound(extension, requiredExtensions, requiredExtensionCount, found);
            }
        }
    }

    for (size_t i = 0; i < requiredExtensionCount; ++i) {
        if (!found[i]) {
            DEBUG("[%s] missing GL extension %s", logPrefix, requiredExtensions[i]);
            return false;
        }
    }

    return true;
}

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
                                      PFNGLDELETETEXTURESPROC_RAW& raw_glDeleteTextures)
{
    if (!loadProc) {
        DEBUG("[%s] no GL proc loader available", logPrefix);
        return false;
    }

    glCreateMemoryObjectsEXT = reinterpret_cast<PFNGLCREATEMEMORYOBJECTSEXTPROC>(
        loadProc("glCreateMemoryObjectsEXT", loadProcData));
    glTexStorageMem2DEXT = reinterpret_cast<PFNGLTEXSTORAGEMEM2DEXTPROC>(
        loadProc("glTexStorageMem2DEXT", loadProcData));
    glImportMemoryFdEXT = reinterpret_cast<PFNGLIMPORTMEMORYFDEXTPROC>(
        loadProc("glImportMemoryFdEXT", loadProcData));
    glDeleteMemoryObjectsEXT = reinterpret_cast<PFNGLDELETEMEMORYOBJECTSEXTPROC>(
        loadProc("glDeleteMemoryObjectsEXT", loadProcData));
    glMemoryObjectParameterivEXT = reinterpret_cast<PFNGLMEMORYOBJECTPARAMETERIVEXTPROC_>(
        loadProc("glMemoryObjectParameterivEXT", loadProcData));

    if (!glCreateMemoryObjectsEXT || !glTexStorageMem2DEXT ||
        !glImportMemoryFdEXT || !glDeleteMemoryObjectsEXT) {
        DEBUG("[%s] failed to load GL_EXT_memory_object_fd functions", logPrefix);
        return false;
    }

    raw_glGenTextures = reinterpret_cast<PFNGLGENTEXTURESPROC_RAW>(
        loadProc("glGenTextures", loadProcData));
    raw_glBindTexture = reinterpret_cast<PFNGLBINDTEXTUREPROC_RAW>(
        loadProc("glBindTexture", loadProcData));
    raw_glTexParameteri = reinterpret_cast<PFNGLTEXPARAMETERIPROC_RAW>(
        loadProc("glTexParameteri", loadProcData));
    raw_glDeleteTextures = reinterpret_cast<PFNGLDELETETEXTURESPROC_RAW>(
        loadProc("glDeleteTextures", loadProcData));

    if (!raw_glGenTextures || !raw_glBindTexture || !raw_glTexParameteri || !raw_glDeleteTextures) {
        DEBUG("[%s] failed to load raw GL function pointers", logPrefix);
        return false;
    }

    DEBUG("[%s] GL_EXT_memory_object_fd functions loaded", logPrefix);
    return true;
}

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
                           const char* label)
{
    glCreateMemoryObjectsEXT(1, &memObj);

    if (glMemoryObjectParameterivEXT) {
        GLint dedicated = GL_TRUE;
        glMemoryObjectParameterivEXT(memObj, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
    }

    int importFd = dup(dmabufFd);
    if (importFd < 0) {
        DEBUG("[%s] dup(dmabuf_fd) failed for %s import", logPrefix, label);
        if (glDeleteMemoryObjectsEXT && memObj) {
            glDeleteMemoryObjectsEXT(1, &memObj);
            memObj = 0;
        }
        return false;
    }

    clearGlErrors();
    glImportMemoryFdEXT(memObj, size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, importFd);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG("[%s] glImportMemoryFdEXT failed for %s, GL error=0x%x", logPrefix, label, err);
        if (glDeleteMemoryObjectsEXT && memObj) {
            glDeleteMemoryObjectsEXT(1, &memObj);
            memObj = 0;
        }
        close(importFd);
        return false;
    }

    clearGlErrors();
    glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8, width, height, memObj, 0);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG("[%s] glTexStorageMem2DEXT failed for %s, GL error=0x%x (size=%lu, %ux%u)",
              logPrefix, label, err, (unsigned long)size, width, height);
        if (glDeleteMemoryObjectsEXT && memObj) {
            glDeleteMemoryObjectsEXT(1, &memObj);
            memObj = 0;
        }
        return false;
    }

    DEBUG("[%s] memory object imported for %s: tex=%u mem=%u size=%lu",
          logPrefix, label, tex, memObj, (unsigned long)size);
    return true;
}
