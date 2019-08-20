TARGET = RenderingPlugin

# SRCS := $(wildcard *.cpp)
SRCS = RenderingPlugin.cpp RenderAPI.cpp RenderAPI_D3D11.cpp Log.cpp

OBJS = $(SRCS:.cpp=.o)

CXXFLAGS = -O2 -Wall -std=c++11 -I./include/
LDFLAGS = -shared
LIBS = -L./vlc-4.0.0-dev/sdk/lib -lvlc 

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
	$(CXX) $(CXXFLAGS) -c -o $@ $<