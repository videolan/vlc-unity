SRCDIR = src/
BUILDDIR = build/

# Not a wildcard here since GNU Make implies we only will use this lib
# for OpenGL
SRCS = RenderingPlugin.cpp \
RenderAPI.cpp \
RenderAPI_OpenGL2.cpp \
RenderAPI_OpenGLCoreES.cpp
OBJ = $(patsubst %.cpp,$(BUILDDIR)%.o,$(SRCS))
UNITY_DEFINES = -DSUPPORT_OPENGL_LEGACY=1 -DSUPPORT_OPENGL_UNIFIED=1 -DUNITY_LINUX=1
GLEW_CFLAGS = $(shell pkg-config --cflags glew)
GLEW_LIBS = $(shell pkg-config --libs glew)
CXXFLAGS = $(UNITY_DEFINES) -O2 -fPIC $(GLEW_CFLAGS) -I/home/nimag42/VLC/vlc-src/include
LDFLAGS = -shared -rdynamic
LIBS = $(GLEW_LIBS) -L/home/nimag42/VLC/vlc-src/lib/.libs -lvlc 
PLUGIN_SHARED = $(BUILDDIR)libVlcUnityWrapper.so
CXX ?= g++

$(BUILDDIR)%.o: $(SRCDIR)%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

all: shared

clean:
	rm -f $(OBJS) $(OBJ) $(PLUGIN_SHARED)

shared: $(OBJ)
	echo $(OBJ)
	$(CXX) $(LDFLAGS) -o $(PLUGIN_SHARED) $(OBJ) $(LIBS)
