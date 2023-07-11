plugin_sources_base = files(
    'Log.cpp',
    'Log.h',
    'PlatformBase.h',
    'RenderAPI.cpp',
    'RenderAPI.h',
    'RenderAPI_OpenGLBase.cpp',
    'RenderAPI_OpenGLBase.h',
    'RenderingPlugin.cpp',
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

corevideo_dep = dependency('CoreVideo', required: host_system == 'darwin')
corefoundation_dep = dependency('CoreFoundation', required: host_system == 'darwin')
foundation_dep = dependency('Foundation', required: host_system == 'darwin')

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
      dependency('glew'),
      cc.find_library('opengl32'),
    ]

    if has_windows_uwp
      vlc_unity_deps += [
        cc.find_library('windowsapp'),
        cc.find_library('ucrt'),
      ] # TODO
      vlc_unity_cxxflags += [ '-O3', '-g', '-gcodeview',
        '-fdebug-prefix-map=\'/mnt/s/\'=\'s:/\'',
        '-Wall', '-I./sdk/include/']
      # UWP support
      vlc_unity_ldflags += [ '-Wl,--nxcompat', '-Wl,--no-seh', '-Wl,--dynamicbase' ]
      vlc_unity_cppflags += [
        '-DUWP', '-DWINAPI_FAMILY=WINAPI_FAMILY_APP', '-D_UNICODE',
        '-DUNICODE', '-D__MSVCRT_VERSION__=0xE00',
        '-D_WIN32_WINNT=0x0A00' ]
    endif
elif host_system == 'linux'
    plugin_sources += [ egl_sources ]
endif

if have_ios or have_tvos
    opengl_dep = dependency('OpenGLES', required: true)
    vlc_unity_deps += [ opengl_dep, corevideo_dep, corefoundation_dep, foundation_dep ]
    plugin_sources += [ eagl_sources ]
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
   plugin_sources += [ cgl_sources ]
endif

if get_option('watermark')
  vlc_unity_cppflags += [ '-DSHOW_WATERMARK' ]
endif

vlc_unity_plugin = library(
    'VLCUnityPlugin',
    plugin_sources,
    include_directories: plugin_include_dirs,
    version: '1.0.0', # TODO
    dependencies: vlc_unity_deps,
    install: true
)
