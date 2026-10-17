BUILD_DIR ?= build/make
CMAKE_ARGS ?=
ifdef DMOD_DIR
CMAKE_ARGS += -DDMOD_DIR=$(DMOD_DIR)
endif
ifdef DMOD_TOOLS_NAME
CMAKE_ARGS += -DDMOD_TOOLS_NAME=$(DMOD_TOOLS_NAME)
endif
.PHONY: all configure test clean
all: configure
	cmake --build $(BUILD_DIR) --parallel
configure:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_ARGS)
test: all
	ctest --test-dir $(BUILD_DIR) --output-on-failure
clean:
	cmake --build $(BUILD_DIR) --target clean
