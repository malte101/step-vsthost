# step-vsthost build helper

UNAME_S := $(shell uname -s)
ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
else
    DETECTED_OS := $(UNAME_S)
endif

PROJECT_TARGET = step_vsthost
BUILD_DIR = Build
JUCE_DIR = JUCE

CONFIG ?= Release
VERBOSE ?= 0

ifeq ($(DETECTED_OS),Darwin)
    NPROC := $(shell sysctl -n hw.ncpu)
else ifeq ($(DETECTED_OS),Linux)
    NPROC := $(shell nproc)
else
    NPROC := 4
endif

CMAKE := cmake
CMAKE_GENERATOR ?= "Unix Makefiles"

.PHONY: all configure build vst3 au clean

all: configure build

configure:
	@if [ ! -d "$(JUCE_DIR)" ]; then \
		echo "ERROR: JUCE not found. Clone https://github.com/juce-framework/JUCE.git"; \
		exit 1; \
	fi
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. -DCMAKE_BUILD_TYPE=$(CONFIG) -G $(CMAKE_GENERATOR)

build:
	@cd $(BUILD_DIR) && $(CMAKE) --build . --config $(CONFIG) -j$(NPROC) $(if $(filter 1,$(VERBOSE)),--verbose,)

vst3:
	@cd $(BUILD_DIR) && $(CMAKE) --build . --target $(PROJECT_TARGET)_VST3 --config $(CONFIG) -j$(NPROC)

au:
ifeq ($(DETECTED_OS),Darwin)
	@cd $(BUILD_DIR) && $(CMAKE) --build . --target $(PROJECT_TARGET)_AU --config $(CONFIG) -j$(NPROC)
else
	@echo "AU target is macOS-only."
endif

clean:
	@rm -rf $(BUILD_DIR)
