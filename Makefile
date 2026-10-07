MAKEFLAGS += --no-print-directory

CPUS?=$(shell getconf _NPROCESSORS_ONLN || echo 1)

BUILD_DIR = build
CONAN_PROFILE = gcc15_c++26

.PHONY: all build test doc clean

all: build

build:
	conan build . -of=${BUILD_DIR} -b=missing -pr=${CONAN_PROFILE} -c 'libpq/*:tools.build:cflags=["-std=gnu17"]' -s mesa-glu/*:compiler.cppstd=23

test: build
	cd ${BUILD_DIR} && ctest --output-on-failure

doc:
	zensical serve

clean:
	@rm -rf $(BUILD_DIR)
	@rm -rf imgui_backends
