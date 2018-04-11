cmake_minimum_required(VERSION 3.8)
project(VlcUnityWrapper)

set (CMAKE_CXX_STANDARD 11)

set(CMAKE_MODULE_PATH
  "${CMAKE_SOURCE_DIR}/cmake"
)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_search_module(LIBVLC REQUIRED libvlc)

set(RENDER_LIBS
  ${LIBVLC_LIBRARIES}
  Threads::Threads
  )

set(RENDER_SRC
  "src/RenderAPI_OpenGLBase.h"
  "src/RenderAPI_OpenGLBase.cpp"
  )

if (WIN32)
  find_package(OpenGL REQUIRED)
  add_definitions("-DGLEW_STATIC")

  list(APPEND RENDER_SRC
	"src/RenderAPI_OpenGLWgl.cpp"
	"src/GLEW/glew.c"
    "src/RenderAPI_D3D11.cpp"
    )

  list(APPEND RENDER_LIBS
	${OPENGL_LIBRARIES}
	)
elseif(CMAKE_ANDROID_NDK)
  find_package(OpenGLES2 REQUIRED)

  add_definitions("-DUNITY_ANDROID")

  list(APPEND RENDER_SRC
	"src/RenderAPI_OpenGLEGL.cpp"
    )

  list(APPEND RENDER_LIBS
	${OPENGLES2_LIBRARIES}
	EGL log android
	)

elseif(UNIX)
  find_package(GLEW REQUIRED)
  find_package(X11 REQUIRED)
  find_package(OpenGL REQUIRED)

  add_definitions("-DUNITY_LINUX")

  list(APPEND RENDER_SRC
	"src/RenderAPI_OpenGLGlx.cpp"
    )

  list(APPEND RENDER_LIBS
	${OPENGL_LIBRARIES}
	${GLEW_LIBRARIES}
	${X11_LIBRARIES}
	)
endif()

list(APPEND RENDER_SRC
  "src/PlatformBase.h"
  "src/RenderAPI.cpp"
  "src/RenderAPI.h"
  "src/RenderingPlugin.cpp"
  "src/Log.h"
  "src/Log.cpp"
  )

link_directories(
  ${LIBVLC_LIBRARY_DIRS}
  )


add_library(VlcUnityWrapper SHARED
  ${RENDER_SRC}
  )

target_link_libraries(VlcUnityWrapper
  ${RENDER_LIBS}
  )

target_include_directories(VlcUnityWrapper PUBLIC
  "${CMAKE_SOURCE_DIR}/src/Unity"
  "${CMAKE_SOURCE_DIR}/src/mingw.thread"
  ${LIBVLC_INCLUDE_DIRS}
  ${OPENGL_INCLUDE_DIRS}
  ${OPENGLES2_INCLUDE_DIRS}
  ${GLEW_INCLUDE_DIRS}
  ${X11_INCLUDE_DIRS}
  )

if (WIN32)
  SET_TARGET_PROPERTIES(VlcUnityWrapper PROPERTIES PREFIX "")
endif()
