# VCV Rack 2 plugin. Requires the Rack SDK at ../sdk/Rack-SDK (see readme.md).
RACK_DIR ?= ../sdk/Rack-SDK

FLAGS += -I../dsp/include
CFLAGS +=
# Rack SDK targets macOS 10.9; new libc++ warns about it on every file.
CXXFLAGS += -Wno-\#warnings
LDFLAGS +=

# src/dsp_core.cpp compiles the shared core from ../dsp/src as a unity build,
# which keeps object files inside vcv/build.
SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/plugin.mk

# The shared core is C++17; the SDK defaults to C++11.
CXXFLAGS := $(filter-out -std=c++11,$(CXXFLAGS)) -std=c++17
