TARGET = VLCUnityPlugin

SRCS = RenderingPlugin.cpp RenderAPI.cpp RenderAPI_D3D11.cpp Log.cpp

OBJS = $(SRCS:.cpp=.o)

CXXFLAGS = -O3 -g -gcodeview -fdebug-prefix-map='/mnt/c/'='c:/' -Wall -I./sdk/include/ -DNDEBUG

LIB=./sdk/lib/

LDFLAGS = -static-libgcc -static-libstdc++ -shared -Wl,-pdb= -L$(LIB)

LIBS = -lvlc -ld3d11 -ld3dcompiler_47 -ldxgi

ifeq ($(ARCH), x86_64)
BIN_PREFIX = x86_64-w64-mingw32
COMPILEFLAG = m64
else
BIN_PREFIX = i686-w64-mingw32
COMPILEFLAG = m32
endif

OUTPUT = $(TARGET).dll
WINDOWS_BINARIES = Assets/VLCUnity/Plugins/x86_64

CXX = $(BIN_PREFIX)-c++
STRIP = $(BIN_PREFIX)-strip

all: $(OUTPUT)

copy: $(OUTPUT)
ifndef NOSTRIP
	$(STRIP) $(OUTPUT)
endif
	cp -f $(OUTPUT) ../$(WINDOWS_BINARIES)/$(OUTPUT)

clean:
	rm -f $(OUTPUT) $(OBJS)

$(OUTPUT): $(OBJS)
	$(CXX) $(LDFLAGS) -o $(OUTPUT) $(OBJS) $(LIBS)

.cpp.o:
	$(CXX) $(CXXFLAGS) -$(COMPILEFLAG) -c -o $@ $<