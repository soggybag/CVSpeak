# Desktop build: DSP core tests and render tools.
#   make test     build and run unit tests
#   make render   render the core to build/render.wav
#   make vcv      build the VCV Rack plugin (see vcv/Makefile)

CXX ?= clang++
CXXFLAGS += -std=c++17 -O2 -Wall -Wextra -Wdouble-promotion -Idsp/include

DSP_SRC := $(wildcard dsp/src/*.cpp)
DSP_HDR := $(wildcard dsp/include/cvspeak/*.h)

.PHONY: all test render vcv vcv-install clean

all: build/tests build/render

build:
	mkdir -p build

build/tests: tests/test_main.cpp $(DSP_SRC) $(DSP_HDR) | build
	$(CXX) $(CXXFLAGS) -o $@ tests/test_main.cpp $(DSP_SRC)

build/render: tools/render.cpp tools/wav.h $(DSP_SRC) $(DSP_HDR) | build
	$(CXX) $(CXXFLAGS) -Itools -o $@ tools/render.cpp $(DSP_SRC)

test: build/tests
	./build/tests

render: build/render
	./build/render build/render.wav

vcv:
	$(MAKE) -C vcv

vcv-install:
	$(MAKE) -C vcv install

clean:
	rm -rf build
	$(MAKE) -C vcv clean
