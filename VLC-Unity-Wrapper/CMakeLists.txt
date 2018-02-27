cmake_minimum_required(VERSION 3.8)
project(VlcUnityWrapper)

set (CMAKE_CXX_STANDARD 11)
#set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
#find_package(GLEW REQUIRED)
pkg_search_module(LIBVLC REQUIRED libvlc)



if (WIN32)
  set(RENDER_SRC
    "src/RenderAPI_OpenGLCoreES.cpp"
    "src/RenderAPI_D3D11.cpp"
	"src/GLEW/glew.c"
    #"src/RenderAPI_D3D12.cpp"
    #"src/RenderAPI_D3D9.cpp"
    )

  add_definitions("-DGLEW_STATIC")
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
  "src/Log.h"
  "src/Log.cpp"
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
  Threads::Threads
  )

target_include_directories(VlcUnityWrapper PUBLIC
  "${CMAKE_SOURCE_DIR}/src/Unity"
  "${CMAKE_SOURCE_DIR}/src/mingw.thread"
  ${LIBVLC_INCLUDE_DIRS}
  ${OPENGL_INCLUDE_DIRS}
  ${GLEW_INCLUDE_DIRS}
  ${X11_INCLUDE_DIRS}
  )

if (WIN32)
  SET_TARGET_PROPERTIES(VlcUnityWrapper PROPERTIES PREFIX "")
endif()
