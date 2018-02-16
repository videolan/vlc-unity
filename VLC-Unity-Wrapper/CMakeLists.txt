cmake_minimum_required(VERSION 3.5)
project(VlcUnityWrapper)

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
pkg_search_module(LIBVLC REQUIRED libvlc)

if (WIN32)
  set(RENDER_SRC
	"src/RenderAPI_OpenGLCoreES.cpp"
	"src/RenderAPI_D3D11.cpp"
	#"src/RenderAPI_D3D12.cpp"
	#"src/RenderAPI_D3D9.cpp"
	)
elseif(UNIX)
  set(RENDER_SRC
	"src/RenderAPI_OpenGLCoreES.cpp"
	)
  find_package(GLEW REQUIRED)
  find_package(X11 REQUIRED)
  add_definitions("-DUNITY_LINUX")
endif()

set(COMMON_SRC
  "src/PlatformBase.h"
  "src/RenderAPI.cpp"
  "src/RenderAPI.h"
  "src/RenderingPlugin.cpp"
  )

link_directories(
  ${LIBVLC_LIBRARY_DIRS}
  )


add_library(VlcUnityWrapper SHARED
  ${COMMON_SRC}
  ${RENDER_SRC}
  )

target_link_libraries(VlcUnityWrapper
  ${LIBVLC_LIBRARIES}
  ${OPENGL_LIBRARIES}
  ${GLEW_LIBRARIES}
  ${X11_LIBRARIES}
  )

target_include_directories(VlcUnityWrapper PUBLIC
  "${CMAKE_SOURCE_DIR}/src/Unity"
  ${LIBVLC_INCLUDE_DIRS}
  ${OPENGL_INCLUDE_DIRS}
  ${GLEW_INCLUDE_DIRS}
  ${X11_INCLUDE_DIRS}
  )
