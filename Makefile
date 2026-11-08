# Desktop build: DSP core tests, render and encoding tools.
#   make test          build and run unit tests
#   make render        speak every word into build/render.wav
#   make words         regenerate dsp/src/words_generated.cpp from tools/words.txt (macOS `say`)
#   make phrases       regenerate dsp/src/phrases_generated.cpp from tools/phrases.txt
#   make vcv           build the VCV Rack plugin (see vcv/Makefile)
#   make vcv-install   package and copy the plugin into Rack's plugin folder
#   make vcv-link      symlink vcv/ into Rack's plugin folder instead (then `make vcv` is enough)

CXX ?= clang++
CXXFLAGS += -std=c++17 -O2 -Wall -Wextra -Wdouble-promotion -Idsp/include

DSP_SRC := $(wildcard dsp/src/*.cpp)
DSP_HDR := $(wildcard dsp/include/cvspeak/*.h)
# The encoder produces words_generated.cpp, so it must not link against it.
ENCODER_SRC := dsp/src/lpc_format.cpp dsp/src/lpc_analysis.cpp

RACK_USER_DIR ?= $(HOME)/Library/Application Support/Rack2
RACK_PLUGINS := $(RACK_USER_DIR)/plugins-mac-arm64

.PHONY: all test render words phrases vcv vcv-install vcv-link clean

all: build/tests build/render build/lpc_encode

build:
	mkdir -p build

build/tests: tests/test_main.cpp $(DSP_SRC) $(DSP_HDR) | build
	$(CXX) $(CXXFLAGS) -Itools -o $@ tests/test_main.cpp $(DSP_SRC)

build/render: tools/render.cpp tools/wav.h $(DSP_SRC) $(DSP_HDR) | build
	$(CXX) $(CXXFLAGS) -Itools -o $@ tools/render.cpp $(DSP_SRC)

build/lpc_encode: tools/lpc_encode.cpp tools/wav.h $(ENCODER_SRC) $(DSP_HDR) | build
	$(CXX) $(CXXFLAGS) -Itools -o $@ tools/lpc_encode.cpp $(ENCODER_SRC)

test: build/tests
	./build/tests

render: build/render
	./build/render build/render.wav

# Word indices can shift when words.txt changes, so phrases are rebuilt too.
words: build/lpc_encode tools/words.txt tools/phrases.txt tools/make_words.sh tools/make_phrases.py
	bash tools/make_words.sh
	python3 tools/make_phrases.py

phrases:
	python3 tools/make_phrases.py

vcv:
	$(MAKE) -C vcv

vcv-install:
	@if [ -L "$(RACK_PLUGINS)/CVSpeak" ]; then \
		echo "Rack's CVSpeak folder is a symlink (make vcv-link); just run 'make vcv'."; exit 1; fi
	$(MAKE) -C vcv install

vcv-link: vcv
	mkdir -p "$(RACK_PLUGINS)"
	rm -rf "$(RACK_PLUGINS)/CVSpeak" "$(RACK_PLUGINS)"/CVSpeak-*.vcvplugin
	ln -s "$(CURDIR)/vcv" "$(RACK_PLUGINS)/CVSpeak"
	@echo "Linked. From now on: quit Rack, 'make vcv', reopen Rack."

clean:
	rm -rf build
	$(MAKE) -C vcv clean
