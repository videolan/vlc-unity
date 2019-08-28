TARGET = RenderingPlugin

SRCS = RenderingPlugin.cpp RenderAPI.cpp RenderAPI_D3D11.cpp Log.cpp

OBJS = $(SRCS:.cpp=.o)

CXXFLAGS = -O2 -Wall -I./include/
# LDFLAGS = -shared -m64
LDFLAGS = -static-libgcc -static-libstdc++ -shared
LIBS = -L./vlc-4.0.0-dev/sdk/lib -lvlc -ld3d11 -ld3dcompiler_47 -ldxgi

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