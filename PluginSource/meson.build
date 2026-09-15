plugin_sources_base = files(
    'Log.cpp',
    'Log.h',
    'PlatformBase.h',
    'RenderAPI.cpp',
    'RenderAPI.h',
    'RenderAPIRegistry.cpp',
    'RenderAPIRegistry.h',
    'RenderingPlugin.cpp',
)

opengl_sources_base = files(
    'RenderAPI_OpenGLBase.cpp',
    'RenderAPI_OpenGLBase.h',
)

watermark_sources = files(
    'RenderAPI_OpenGLWatermark.cpp',
    'RenderAPI_OpenGLWatermark.h',
)

android_sources = files(
    'AndroidJNI.h',
    'RenderAPI_Android.cpp',
)

d3d11_sources = files(
    'RenderAPI_D3D11.cpp',
)

egl_sources = files(
    'RenderAPI_OpenGLEGL.cpp',
    'RenderAPI_OpenGLEGL.h',
)

glx_sources = files(
    'RenderAPI_OpenGLGLX.cpp',
    'RenderAPI_OpenGLGLX.h',
)

linux_egl_sources = files(
    'RenderAPI_OpenGLLinuxEGL.cpp',
    'RenderAPI_OpenGLLinuxEGL.h',
)

linux_graphics_interop_sources = files(
    'RenderAPI_LinuxOpenGL.cpp',
    'LinuxVideoOutput.h',
    'LinuxGraphicsInterop.cpp',
    'LinuxGraphicsInterop.h',
    'LinuxGBMDevice.cpp',
    'LinuxGBMDevice.h',
    'LinuxDMABufProducer.cpp',
    'LinuxDMABufProducer.h',
    'LinuxDMABufWatermark.cpp',
    'LinuxDMABufWatermark.h',
    'LinuxOpenGLUnityImports.cpp',
    'LinuxOpenGLUnityImports.h',
    'RenderAPI_OpenGLLinuxDMABuf.cpp',
    'RenderAPI_OpenGLLinuxDMABuf.h',
    'UniqueFd.cpp',
    'UniqueFd.h',
)

vulkan_core_sources = files(
    'VulkanFrameSource.h',
    'VulkanSubmissionStrategy.h',
    'VulkanUnityCopyCore.cpp',
    'VulkanUnityCopyCore.h',
    'VulkanPlatformRequirements.cpp',
    'VulkanPlatformRequirements.h',
)

vulkan_android_sources = files(
    'AndroidVulkanFrameSource.cpp',
    'AndroidVulkanFrameSource.h',
    'RenderAPI_Vulkan.cpp',
    'RenderAPI_Vulkan.h',
    'UnitySubmissionStrategy.cpp',
    'UnitySubmissionStrategy.h',
    'VulkanPlatformRequirementsAndroid.cpp',
)

vulkan_linux_sources = files(
    'LinuxDMABufFrameSource.cpp',
    'LinuxDMABufFrameSource.h',
    'LinuxVulkanDeviceIdentity.cpp',
    'LinuxVulkanDeviceIdentity.h',
    'OwnedQueueSubmissionStrategy.cpp',
    'OwnedQueueSubmissionStrategy.h',
    'RenderAPI_VulkanLinux.cpp',
    'RenderAPI_VulkanLinux.h',
    'VulkanPlatformRequirementsLinux.cpp',
)

cgl_sources = files(
    'RenderAPI_OpenGLCGL.mm',
    'RenderAPI_OpenGLCGL.h',
)

eagl_sources = files(
    'RenderAPI_OpenGLEAGL.mm',
    'RenderAPI_OpenGLEAGL.h',
)

plugin_sources = [
    plugin_sources_base,
]

libvlccore_link_args = []

vlc_unity_cppflags = []
vlc_unity_cxxflags = []
vlc_unity_ldflags = []
vlc_unity_install_rpath = ''

vlc_unity_deps = [ libvlc_dep ]

if host_system == 'darwin'
    corevideo_dep = dependency('CoreVideo', required: true)
    corefoundation_dep = dependency('CoreFoundation', required: true)
    foundation_dep = dependency('Foundation', required: true)
endif

if host_system == 'darwin'
    add_project_arguments('-DGL_SILENCE_DEPRECATION=1', language: 'objcpp')
elif host_system == 'windows'
    plugin_sources += [ d3d11_sources ]
    vlc_unity_ldflags += [ '-static-libgcc', '-static-libstdc++', '-shared',
                           '-Wl,-pdb=' ]
    vlc_unity_deps += [
      cc.find_library('d3d11'),
      #cc.find_library('d3d11compiler_47'),
      cc.find_library('dxgi'),
      cc.find_library('d2d1'),
      cc.find_library('dwrite')
    ]

    if has_windows_uwp
      vlc_unity_deps += [
        cc.find_library('windowsapp'),
        cc.find_library('ucrt'),
        cc.find_library('windowsappcompat'),
      ]
      vlc_unity_cxxflags += [ '-O3', '-g', '-gcodeview',
        '-fdebug-prefix-map=\'/mnt/s/\'=\'s:/\'',
        '-Wall', '-I./sdk/include/', '-DUWP' ]
      # UWP support
      vlc_unity_ldflags += [ '-Wl,--nxcompat', '-Wl,--no-seh', '-Wl,--dynamicbase' ]
      vlc_unity_cppflags += [
        '-DUWP', '-DWINAPI_FAMILY=WINAPI_FAMILY_APP', '-D_UNICODE',
        '-DUNICODE', '-D__MSVCRT_VERSION__=0xE00',
        '-D_WIN32_WINNT=0x0A00' ]
    endif
elif host_system == 'linux'
    gl_dep = dependency('gl', required: true)
    egl_dep = dependency('egl', required: true)
    x11_dep = dependency('x11', required: true)
    gbm_dep = dependency('gbm', version: '>=21.1', required: true)
    libdrm_dep = dependency('libdrm', required: true)
    vlc_unity_deps += [ gl_dep, egl_dep, x11_dep ]
    vlc_unity_cppflags += [ '-DUNITY_LINUX=1' ]
    vlc_unity_install_rpath = '$ORIGIN'
    plugin_sources += [
        opengl_sources_base,
        linux_graphics_interop_sources,
        glx_sources,
        egl_sources,
        linux_egl_sources,
    ]
    vlc_unity_deps += [ gbm_dep, libdrm_dep ]
    # VkPhysicalDeviceDrmPropertiesEXT and the DRM-format-modifier structs
    # need vulkan-headers >= 1.2.174; gate here so 'auto' degrades cleanly on
    # older toolchains instead of failing mid-compile.
    vulkan_dep = dependency(
        'vulkan', version: '>=1.2.174',
        required: get_option('linux_vulkan'))
    if vulkan_dep.found()
        vulkan_headers_dep = vulkan_dep.partial_dependency(
            compile_args: true,
            includes: true,
        )
        vlc_unity_deps += [ vulkan_headers_dep ]
        vlc_unity_cppflags += [ '-DSUPPORT_VULKAN=1' ]
        plugin_sources += [ vulkan_core_sources, vulkan_linux_sources ]
    endif
elif host_system == 'android'
    if get_option('static_android')
        vlc_unity_ldflags += [ '-static-libstdc++' ]
    endif
    vlc_unity_deps += [ cc.find_library('GLESv2'), cc.find_library('EGL'), cc.find_library('log'), cc.find_library('android') ]
    vlc_unity_cppflags += [ '-DSUPPORT_OPENGL_ES=1', '-DUNITY_ANDROID=1' ]
    plugin_sources += [ opengl_sources_base, android_sources, egl_sources ]

    # Only enable Vulkan support for arm64-v8a
    if host_machine.cpu_family() == 'aarch64'
        vlc_unity_deps += [ cc.find_library('vulkan') ]
        vlc_unity_cppflags += [ '-DSUPPORT_VULKAN=1' ]
        plugin_sources += [ vulkan_core_sources, vulkan_android_sources ]
    endif
endif

if have_ios or have_tvos
    opengl_dep = dependency('OpenGLES', required: true)
    vlc_unity_deps += [ opengl_dep, corevideo_dep, corefoundation_dep, foundation_dep ]
    plugin_sources += [ opengl_sources_base, eagl_sources ]
    vlc_unity_deps += [
        dependency('VideoToolbox'),
        dependency('CoreAudio'),
        dependency('CoreMedia'),
        dependency('CoreImage'),
        dependency('Security'),
        dependency('CoreText'),
        dependency('iconv'),
        dependency('UIKit'),
        dependency('CoreGraphics'),
        dependency('Audiotoolbox'),
        dependency('AVFAudio'),
    ]
endif

if have_osx
   opengl_dep = dependency('OpenGL', required: true)
   vlc_unity_deps += [ opengl_dep, corevideo_dep, corefoundation_dep, foundation_dep ]
   plugin_sources += [ opengl_sources_base, cgl_sources ]
endif

if get_option('watermark')
   vlc_unity_cppflags += [ '-DSHOW_WATERMARK' ]
   if host_system != 'windows'
      plugin_sources += [ watermark_sources ]
   endif
endif

vlc_unity_plugin = library(
    'VLCUnityPlugin',
    plugin_sources,
    include_directories: plugin_include_dirs,
    dependencies: vlc_unity_deps,
    install: true,
    build_rpath: vlc_unity_install_rpath,
    install_rpath: vlc_unity_install_rpath,
    link_args: vlc_unity_ldflags,
    cpp_args: vlc_unity_cppflags + vlc_unity_cxxflags,
    objcpp_args: vlc_unity_cppflags + vlc_unity_cxxflags
)

if host_system == 'linux'
    linux_video_output_tests = executable(
        'linux-video-output-tests',
        files('Log.cpp', 'tests/LinuxVideoOutputTests.cpp'),
        include_directories: plugin_include_dirs,
        dependencies: [libvlc_dep.partial_dependency(compile_args: true, includes: true), dependency('threads')],
        cpp_args: ['-DUNITY_LINUX=1'],
    )
    test('linux-video-output', linux_video_output_tests)

    render_api_registry_tests = executable(
        'render-api-registry-tests',
        files(
            'RenderAPIRegistry.cpp',
            'tests/RenderAPIRegistryTests.cpp',
        ),
        include_directories: plugin_include_dirs,
        dependencies: [ libvlc_dep ],
    )
    test('render-api-registry', render_api_registry_tests)

    linux_graphics_interop_tests = executable(
        'linux-graphics-interop-tests',
        files(
            'LinuxGraphicsInterop.cpp',
            'tests/LinuxGraphicsInteropTests.cpp',
        ),
        include_directories: plugin_include_dirs,
    )
    test('linux-graphics-interop', linux_graphics_interop_tests)

    linux_vulkan_identity_tests = executable(
        'linux-vulkan-identity-tests',
        files(
            'LinuxVulkanDeviceIdentity.cpp',
            'tests/LinuxVulkanDeviceIdentityTests.cpp',
        ),
        include_directories: plugin_include_dirs,
    )
    test('linux-vulkan-identity', linux_vulkan_identity_tests)

    unique_fd_tests = executable(
        'unique-fd-tests',
        files('UniqueFd.cpp', 'tests/UniqueFdTests.cpp'),
        include_directories: plugin_include_dirs,
    )
    test('unique-fd', unique_fd_tests)

    if vulkan_dep.found()
        vulkan_core_tests = executable(
            'vulkan-core-tests',
            files(
                'Log.cpp',
                'VulkanUnityCopyCore.cpp',
                'tests/VulkanCoreTests.cpp',
            ),
            include_directories: plugin_include_dirs,
            dependencies: [ vulkan_headers_dep ],
            cpp_args: [ '-DUNITY_LINUX=1' ],
        )
        test('vulkan-core', vulkan_core_tests)

        vulkan_requirements_tests = executable(
            'vulkan-requirements-tests',
            files(
                'Log.cpp',
                'VulkanPlatformRequirements.cpp',
                'VulkanPlatformRequirementsLinux.cpp',
                'tests/VulkanRequirementsTests.cpp',
            ),
            include_directories: plugin_include_dirs,
            dependencies: [ vulkan_headers_dep ],
            cpp_args: [ '-DUNITY_LINUX=1' ],
        )
        test('vulkan-requirements', vulkan_requirements_tests)

        owned_queue_submission_tests = executable(
            'owned-queue-submission-tests',
            files(
                'Log.cpp',
                'OwnedQueueSubmissionStrategy.cpp',
                'tests/OwnedQueueSubmissionTests.cpp',
            ),
            include_directories: plugin_include_dirs,
            dependencies: [ vulkan_headers_dep ],
            cpp_args: [ '-DUNITY_LINUX=1' ],
        )
        test('owned-queue-submission', owned_queue_submission_tests)

        unity_submission_strategy_tests = executable(
            'unity-submission-strategy-tests',
            files(
                'UnitySubmissionStrategy.cpp',
                'tests/UnitySubmissionStrategyTests.cpp',
            ),
            include_directories: plugin_include_dirs,
            dependencies: [ vulkan_headers_dep ],
            cpp_args: [ '-DUNITY_LINUX=1' ],
        )
        test('unity-submission-strategy', unity_submission_strategy_tests)
    endif
endif
