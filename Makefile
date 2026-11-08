# CVSpeak build. The repository root is the VCV Rack plugin, as the VCV
# Library build expects; the desktop tools and tests share the same core.
#
# VCV Rack plugin (Rack SDK in sdk/Rack-SDK, or set RACK_DIR):
#   make               build the plugin (plugin.dylib / .so / .dll)
#   make dist          package dist/CVSpeak-<version>-<os>-<cpu>.vcvplugin
#   make install       package and copy into Rack's plugin folder (alias: vcv-install)
#   make vcv-link      symlink this folder into Rack's plugin folder instead (then `make` is enough)
#   make clean         remove plugin and desktop build output
#
# Desktop (DSP core, no Rack needed):
#   make test          build and run unit tests
#   make render        speak every word into build/render.wav
#   make screens       print the display screens as ASCII art
#   make panel         regenerate res/CVSpeak.svg + src/panel_layout.hpp from tools/make_panel.py
#   make panel-preview  same, plus build/panel_preview.png with component outlines (macOS)
#
# Firmware (Pico SDK in sdk/pico-sdk, see firmware/README.md):
#   make firmware      build build/firmware/cvspeak.uf2
#   make firmware-breadboard  same for a bare Pico 2 (build/firmware-breadboard/cvspeak.uf2)
#   add SSD1306=1 to either for a 0.96in SSD1306 OLED instead of the 1.3in SH1106
#
# Hardware (KiCad 10, kicad-cli):
#   make schematic     regenerate hardware/CVSpeak.kicad_sch from hardware/scripts/build_sch.py, then run ERC
#   make words         regenerate dsp/src/words_generated.cpp from tools/words.txt + phrases.txt (macOS)
#   make phrases       regenerate dsp/src/phrases_generated.cpp from tools/phrases.txt

# ---- VCV Rack plugin ---------------------------------------------------------

RACK_DIR ?= sdk/Rack-SDK

FLAGS += -Idsp/include

# src/dsp_core.cpp compiles the shared core from dsp/src as a unity build.
SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

# Without the SDK the desktop targets below still work (e.g. `make test` in CI).
ifneq ($(wildcard $(RACK_DIR)/plugin.mk),)
include $(RACK_DIR)/plugin.mk

# The shared core is C++17; the SDK defaults to C++11.
CXXFLAGS := $(filter-out -std=c++11,$(CXXFLAGS)) -std=c++17
ifdef ARCH_MAC
# Rack SDK targets macOS 10.9; new libc++ warns about it on every file.
CXXFLAGS += -Wno-\#warnings
endif
else
all dist install dep:
	@echo "Rack SDK not found at '$(RACK_DIR)'. See readme.md, or set RACK_DIR."; exit 1
clean:
	rm -rf build
endif

# ---- Desktop tools and tests ---------------------------------------------------
# Separate flags: Rack's plugin flags (-fPIC, SDK includes) aren't wanted here.
# Outputs go to build/desktop so they don't mix with the plugin's object files.

DESKTOP_CXX ?= c++
DESKTOP_CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wdouble-promotion -Idsp/include -Itools
DESKTOP := build/desktop

DSP_SRC := $(wildcard dsp/src/*.cpp)
DSP_HDR := $(wildcard dsp/include/cvspeak/*.h)
# The encoder produces words_generated.cpp, so it must not link against it.
ENCODER_SRC := dsp/src/lpc_format.cpp dsp/src/lpc_analysis.cpp

RACK_USER_DIR_MAC ?= $(HOME)/Library/Application Support/Rack2
RACK_PLUGINS := $(RACK_USER_DIR_MAC)/plugins-mac-arm64

.PHONY: desktop test render screens panel panel-preview schematic firmware firmware-breadboard words phrases vcv vcv-install vcv-link

desktop: $(DESKTOP)/tests $(DESKTOP)/render $(DESKTOP)/lpc_encode $(DESKTOP)/ui_preview

$(DESKTOP)/tests: tests/test_main.cpp $(DSP_SRC) $(DSP_HDR)
	@mkdir -p $(@D)
	$(DESKTOP_CXX) $(DESKTOP_CXXFLAGS) -o $@ tests/test_main.cpp $(DSP_SRC)

$(DESKTOP)/render: tools/render.cpp tools/wav.h $(DSP_SRC) $(DSP_HDR)
	@mkdir -p $(@D)
	$(DESKTOP_CXX) $(DESKTOP_CXXFLAGS) -o $@ tools/render.cpp $(DSP_SRC)

$(DESKTOP)/ui_preview: tools/ui_preview.cpp $(DSP_SRC) $(DSP_HDR)
	@mkdir -p $(@D)
	$(DESKTOP_CXX) $(DESKTOP_CXXFLAGS) -o $@ tools/ui_preview.cpp $(DSP_SRC)

$(DESKTOP)/lpc_encode: tools/lpc_encode.cpp tools/wav.h $(ENCODER_SRC) $(DSP_HDR)
	@mkdir -p $(@D)
	$(DESKTOP_CXX) $(DESKTOP_CXXFLAGS) -o $@ tools/lpc_encode.cpp $(ENCODER_SRC)

test: $(DESKTOP)/tests
	./$(DESKTOP)/tests

render: $(DESKTOP)/render
	./$(DESKTOP)/render build/render.wav

screens: $(DESKTOP)/ui_preview
	./$(DESKTOP)/ui_preview

# Word indices can shift when the vocabulary changes, so phrases are rebuilt too.
words: $(DESKTOP)/lpc_encode tools/words.txt tools/phrases.txt tools/make_words.sh tools/make_phrases.py
	bash tools/make_words.sh
	python3 tools/make_phrases.py

phrases:
	python3 tools/make_phrases.py

OLED_FLAG = -DCVSPEAK_SSD1306=$(if $(SSD1306),ON,OFF)

firmware:
	cmake -S firmware -B build/firmware -DPICO_SDK_PATH=$(CURDIR)/sdk/pico-sdk $(OLED_FLAG)
	cmake --build build/firmware -j8

firmware-breadboard:
	cmake -S firmware -B build/firmware-breadboard -DPICO_SDK_PATH=$(CURDIR)/sdk/pico-sdk -DCVSPEAK_BREADBOARD=ON $(OLED_FLAG)
	cmake --build build/firmware-breadboard -j8

panel:
	python3 tools/make_panel.py

KICAD_CLI ?= /Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
schematic:
	cd hardware && python3 scripts/make_symbols.py && python3 scripts/build_sch.py
	cd hardware && $(KICAD_CLI) sch erc --exit-code-violations --severity-error -o build-erc.rpt CVSpeak.kicad_sch

panel-preview:
	python3 tools/make_panel.py --preview

# Aliases from when the plugin lived in vcv/.
vcv: all

vcv-install:
	@if [ -L "$(RACK_PLUGINS)/CVSpeak" ]; then \
		echo "Rack's CVSpeak folder is a symlink (make vcv-link); just run 'make'."; exit 1; fi
	$(MAKE) install

vcv-link: all
	mkdir -p "$(RACK_PLUGINS)"
	rm -rf "$(RACK_PLUGINS)/CVSpeak" "$(RACK_PLUGINS)"/CVSpeak-*.vcvplugin
	ln -s "$(CURDIR)" "$(RACK_PLUGINS)/CVSpeak"
	@echo "Linked. From now on: quit Rack, 'make', reopen Rack."
