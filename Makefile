# Thin wrapper around the CMake build. All real build logic lives in
# CMakeLists.txt.
#   make          configure (if needed) and build
#   make clean    remove build artifacts and stray *.tga
#   make distclean remove the entire build directory

BUILD_DIR ?= build

all: build

$(BUILD_DIR)/CMakeCache.txt:
	cmake -S . -B $(BUILD_DIR)

build: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR)

clean:
	@[ -d $(BUILD_DIR) ] && cmake --build $(BUILD_DIR) --target clean || true
	rm -f *.tga

distclean:
	rm -rf $(BUILD_DIR)

.PHONY: all build clean distclean
