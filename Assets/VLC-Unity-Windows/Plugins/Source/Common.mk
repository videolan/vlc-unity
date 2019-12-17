TARGET = RenderingPlugin

SRCS = RenderingPlugin.cpp RenderAPI.cpp RenderAPI_D3D11.cpp Log.cpp

OBJS = $(SRCS:.cpp=.o)

CXXFLAGS = -O2 -fdebug-prefix-map='/mnt/d/'='d:/' -Wall -I./include/ -I./mingw.thread
# LDFLAGS = -shared -m64
# gcc (ubuntu)
#LDFLAGS = -static-libgcc -static-libstdc++ -shared -L/mnt/d/Projects/vlc/win64/win64/vlc-4.0.0-dev/sdk/lib
# clang
LDFLAGS = -static-libgcc -static-libstdc++ -shared -Wl,-pdb= -L/mnt/d/vlc-4.0.0-dev/sdk/lib

LIBS = -lvlc -ld3d11 -ld3dcompiler_47 -ldxgi

BIN_PREFIX = x86_64-w64-mingw32
OUTPUT = $(TARGET).dll

CXX = $(BIN_PREFIX)-c++
STRIP = $(BIN_PREFIX)-strip

all: $(OUTPUT)

copy: $(OUTPUT)
ifndef NOSTRIP
	$(STRIP) $(OUTPUT)
endif
	cp -f $(OUTPUT) ../Assets/$(OUTPUT)

clean:
	rm -f $(OUTPUT) $(OBJS)

$(OUTPUT): $(OBJS)
	$(CXX) $(LDFLAGS) -o $(OUTPUT) $(OBJS) $(LIBS)

.cpp.o:
	$(CXX) $(CXXFLAGS) -m64 -c -o $@ $<