project('VLC-Unity', ['cpp'],
    version: '4.0.0-dev',
    default_options: ['cpp_std=c++14'],
    meson_version: '>=0.56.0')

vlc_copyright_years = '1996-2018'
vlc_version_codename = 'Otto Chriek'

# LibVLC library (ABI) version
# Format must be major.minor.micro
libvlc_abi_version = '12.0.0'

libvlc_abi_version_parts = libvlc_abi_version.split('.')
libvlc_abi_version_major = libvlc_abi_version_parts[0].to_int()
libvlc_abi_version_minor = libvlc_abi_version_parts[1].to_int()
libvlc_abi_version_micro = libvlc_abi_version_parts[2].to_int()

vlc_version_full = meson.project_version()

vlc_version_parts = vlc_version_full.split('-')[0].split('.')
vlc_version_type  = vlc_version_full.split('-').get(1, '')

if (vlc_version_parts.length() < 3 or vlc_version_parts.length() > 4)
    error('Unexpected project version @0@. Expected a format of major.minor.revision[.extra][-dev]'.format(vlc_version_full))
endif

vlc_version_major    = vlc_version_parts[0].to_int()
vlc_version_minor    = vlc_version_parts[1].to_int()
vlc_version_revision = vlc_version_parts[2].to_int()
vlc_version_extra    = vlc_version_parts.get(3, '0').to_int()

# Short version (major.minor.revision)
vlc_version_short = '@0@.@1@.@2@'.format(vlc_version_major, vlc_version_minor, vlc_version_revision)

# Normal VLC version (major.minor.revision[-dev])

vlc_version = vlc_version_short + ((vlc_version_type != '') ? '-@0@'.format(vlc_version_type) : '')

vlc_package_name = meson.project_name().to_lower()

vlc_src_root = meson.current_source_dir()
vlc_build_root = meson.current_build_dir()

cdata = configuration_data()

add_project_arguments('-DHAVE_CONFIG_H=1', language: ['cpp', 'objc'])

cc = meson.get_compiler('cpp')
cpp = meson.get_compiler('cpp')
host_system = host_machine.system()

list_inc_dirs = ['PluginSource']
plugin_include_dirs = include_directories(list_inc_dirs)

if host_system == 'darwin'
    add_languages('objcpp', native: false)
endif

#
# General feature defines
#
vlc_conf_prefix = ''

feature_defines = [
    ['_GNU_SOURCE', 1], # Enable GNU extensions on systems that have them
]

foreach d : feature_defines
    cdata.set(d.get(0), d.get(1))
    vlc_conf_prefix = vlc_conf_prefix + '#define @0@ @1@\n'.format(d.get(0), d.get(1))
endforeach

# Threads
threads_dep = dependency('threads', required: true)

#
# Darwin specific checks
#

if host_system == 'darwin'

    # Check if compiling for iOS
    have_ios = cc.get_define('TARGET_OS_IPHONE',
        prefix: '#include <TargetConditionals.h>') == '1'

    # Check if compiling for tvOS
    have_tvos = cc.get_define('TARGET_OS_TV',
        prefix: '#include <TargetConditionals.h>') == '1'

    # If none of the above, assume compiling for macOS
    have_osx = not have_ios and not have_tvos

else
    have_ios = false
    have_tvos = false
    have_osx = false
endif

if have_ios
    add_project_arguments('-DUNITY_IPHONE=1', language: ['c', 'cpp', 'objc'])
endif

if host_system == 'android' 
    add_project_arguments('-DUNITY_ANDROID=1', language: ['c', 'cpp' ])
endif

if have_osx
    add_project_arguments('-DUNITY_OSX=1', 
        language: ['c', 'cpp', 'objc'])
    add_project_arguments('-mmacosx-version-min=10.11',
        language: ['c', 'cpp', 'objc'])
    add_project_link_arguments('-mmacosx-version-min=10.11',
        language: ['c', 'cpp', 'objc'])
endif


# Windows SDK requirements and checks
has_windows_uwp = false
if host_system == 'windows'
  # Check whether we're building for UWP apps
  code = '''
  #include <windows.h>
  #if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP)
  # error "Not building for UWP: missing WINAPI_PARTITION_APP"
  #endif
  #if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
  # error "Not building for UWP: building for WINAPI_PARTITION_DESKTOP"
  #endif'''
  has_windows_uwp = cc.compiles(code, name : 'building for UWP')
endif

add_project_arguments(cc.get_supported_arguments([
    '-Wextra',
    '-Wsign-compare',
    '-Wundef',
    '-Wpointer-arith',
    '-Wvolatile-register-var',
    '-Wformat',
    '-Wformat-security',
    '-Wduplicated-branches',
    '-Wduplicated-cond',
]), language: ['c', 'cpp'])

# Treat warnings as errors if enabled
if get_option('fatal_warnings')
    add_project_arguments(cc.get_supported_arguments(['-Werror']), language: ['c', 'cpp'])
    if host_system == 'darwin'
        add_project_arguments(['-Werror'], language: ['objcpp'])
    endif
endif

if cc.get_id() not in ['clang-cl']
    add_project_arguments(cc.get_supported_arguments([
        '-Wall', # too verbose with clang-cl
    ]), language: ['c', 'cpp'])
endif

add_project_arguments(cc.first_supported_argument(['-Werror-implicit-function-declaration', '-we4013']), language: ['c'])

# Define the shared library extension
if host_system == 'windows'
    cdata.set_quoted('LIBEXT', '.dll')
elif host_system == 'darwin'
    cdata.set_quoted('LIBEXT', '.dylib')
else
    cdata.set_quoted('LIBEXT', '.so')
endif

#
# Populate config.h with additional infos
#

cdata.set_quoted('VERSION',         vlc_version)
cdata.set_quoted('PACKAGE_VERSION', vlc_version)
cdata.set_quoted('VERSION_MESSAGE', '@0@ @1@'.format(vlc_version, vlc_version_codename))

cdata.set('VERSION_MAJOR',      vlc_version_major)
cdata.set('VERSION_MINOR',      vlc_version_minor)
cdata.set('VERSION_REVISION',   vlc_version_revision)
cdata.set('VERSION_EXTRA',      vlc_version_extra)

cdata.set('PACKAGE_VERSION_MAJOR',      vlc_version_major)
cdata.set('PACKAGE_VERSION_MINOR',      vlc_version_minor)
cdata.set('PACKAGE_VERSION_REVISION',   vlc_version_revision)
cdata.set('PACKAGE_VERSION_EXTRA',      vlc_version_extra)
cdata.set('PACKAGE_VERSION_DEV',        vlc_version_type)

cdata.set('LIBVLC_ABI_MAJOR', libvlc_abi_version_major)
cdata.set('LIBVLC_ABI_MINOR', libvlc_abi_version_minor)
cdata.set('LIBVLC_ABI_MICRO', libvlc_abi_version_micro)

cdata.set_quoted('PACKAGE',             vlc_package_name)
cdata.set_quoted('PACKAGE_NAME',        vlc_package_name)
cdata.set_quoted('PACKAGE_STRING',      '@0@ @1@'.format(vlc_package_name, vlc_version))
cdata.set_quoted('COPYRIGHT_YEARS',     vlc_copyright_years)
cdata.set_quoted('COPYRIGHT_MESSAGE',   'Copyright © @0@ the VideoLAN team'.format(vlc_copyright_years))

# Compiler and build system info
cdata.set_quoted('VLC_COMPILER',        cc.get_id() + ' ' + cc.version())
cdata.set_quoted('VLC_COMPILE_BY',      '[not implemented with meson]') # TODO
cdata.set_quoted('VLC_COMPILE_HOST',    '[not implemented with meson]') # TODO
cdata.set_quoted('CONFIGURE_LINE',      '[not implemented with meson]') # TODO

# Generate config.h
configure_file(input: 'config.h.meson',
  output: 'config.h',
  configuration: cdata)

libvlc_dep = dependency('libvlc', version: '>= 4.0')

# Plugin sources
subdir('PluginSource')
