plugin_sources_base = files(
    'Log.cpp',
    'Log.h',
    'PlatformBase.h',
    'RenderAPI.cpp',
    'RenderAPI.h',
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
    'RenderAPI_Android.cpp',
)

d3d11_sources = files(
    'RenderAPI_D3D11.cpp', 
)

egl_sources = files(
    'RenderAPI_OpenGLEGL.cpp',
    'RenderAPI_OpenGLEGL.h',
)

vulkan_sources = files(
    'RenderAPI_Vulkan.cpp',
    'RenderAPI_Vulkan.h',
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
    vlc_unity_cppflags += [ '-DNDEBUG' ]
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
      ]
      vlc_unity_cxxflags += [ '-O3', '-g', '-gcodeview',
        '-fdebug-prefix-map=\'/mnt/s/\'=\'s:/\'',
        '-Wall', '-I./sdk/include/', '-DNDEBUG', '-DUWP', '-Wl,-lwindowsapp', '-lwindowsappcompat', '-lucrt' ]
      # UWP support
      vlc_unity_ldflags += [ '-Wl,--nxcompat', '-Wl,--no-seh', '-Wl,--dynamicbase', '-lwindowsapp' ]
      vlc_unity_cppflags += [
        '-DUWP', '-DWINAPI_FAMILY=WINAPI_FAMILY_APP', '-D_UNICODE',
        '-DUNICODE', '-D__MSVCRT_VERSION__=0xE00',
        '-D_WIN32_WINNT=0x0A00' ]
    endif
elif host_system == 'linux'
    plugin_sources += [ opengl_sources_base, egl_sources ]
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
        plugin_sources += [ vulkan_sources ]
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
    version: '1.0.0', # TODO,
    dependencies: vlc_unity_deps,
    install: true,
    link_args: vlc_unity_ldflags,
    cpp_args: vlc_unity_cppflags + vlc_unity_cxxflags,
    objcpp_args: vlc_unity_cppflags + vlc_unity_cxxflags
)
