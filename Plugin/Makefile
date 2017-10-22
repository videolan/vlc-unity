SRCS=Test.cpp
OBJS=$(SRCS:.cpp=.o)
OUTPUT=TestPlugin.dll
CXXFLAGS=-I.. -O2 -Wall -std=c++11
LDFLAGS=-shared
BIN_PREFIX=x86_64-w64-mingw32
CXX=$(BIN_PREFIX)-g++
STRIP=$(BIN_PREFIX)-strip

all: $(OUTPUT)

copy:
	$(STRIP) $(OUTPUT)
	cp -f $(OUTPUT) ../Assets/$(OUTPUT)

clean:
	rm -f $(OUTPUT) $(OBJS)

$(OUTPUT): $(OBJS)
	$(CXX) $(LDFLAGS) -o $(OUTPUT) $(OBJS)

.cpp.o:
	$(CXX) $(CXXFLAGS) -c -o $@ $<
