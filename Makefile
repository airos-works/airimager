# airImager: native Haiku / air/OS build with the development tools that come
# with Haiku.
#   make              builds build-haiku/airImager
#   make package      builds artifacts/airimager-<version>-<arch>.hpkg
#   make check        runs the engine tests (here, on Haiku)
#   make check-host   runs the engine tests on Linux
# tools/build-cross.sh runs this Makefile with an air/OS cross SDK from Linux.
.DEFAULT_GOAL := all
CXX ?= g++
BUILD ?= build-haiku
# The disk device API (BDiskDeviceRoster) and BAboutWindow have private
# headers; haiku_devel ships them. '=' is relative to the (cross) sysroot.
PRIVATE_HEADERS ?= =/boot/system/develop/headers/private
CPPFLAGS += -Isrc -Ithird_party/xz-embedded $(XZ_CPPFLAGS) $(APP_CPPFLAGS)
HAIKU_CPPFLAGS = -I$(PRIVATE_HEADERS)/storage -I$(PRIVATE_HEADERS)/interface
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wno-multichar -Wno-unused-parameter \
	-Wno-missing-field-initializers
# XZ Embedded, with every check, every BCJ filter and concatenated streams.
# It is compiled as C by the same compiler driver, so a cross build needs no
# separate CC.
XZ_CPPFLAGS = -DXZ_USE_CRC64 -DXZ_USE_SHA256 -DXZ_DEC_ANY_CHECK \
	-DXZ_DEC_CONCATENATED -DXZ_DEC_X86 -DXZ_DEC_ARM -DXZ_DEC_ARMTHUMB \
	-DXZ_DEC_ARM64 -DXZ_DEC_RISCV -DXZ_DEC_POWERPC -DXZ_DEC_IA64 \
	-DXZ_DEC_SPARC
XZ_CFLAGS = -x c -std=gnu11 -O2 -g -Wno-implicit-fallthrough
# Resource tools; tools/build-cross.sh points these at host builds.
RC ?= rc
XRES ?= xres
MIMESET ?= mimeset

XZ_SRC = $(addprefix third_party/xz-embedded/, xz_crc32.c xz_crc64.c \
	xz_sha256.c xz_dec_stream.c xz_dec_lzma2.c xz_dec_bcj.c)
ENGINE_SRC = $(wildcard src/engine/*.cpp)
APP_SRC = $(wildcard src/*.cpp)
XZ_OBJ = $(XZ_SRC:%.c=$(BUILD)/%.o)
ENGINE_OBJ = $(ENGINE_SRC:%.cpp=$(BUILD)/%.o) $(XZ_OBJ)
APP_OBJ = $(APP_SRC:%.cpp=$(BUILD)/%.o)
# be: interface, app, storage (and the disk device API); tracker: BFilePanel.
LIBS = -lbe -ltracker

.PHONY: all package check check-host icon clean

all: $(BUILD)/airImager

$(BUILD)/airImager.rsrc: resources/airImager.rdef resources/airImager.hvif \
		resources/image.hvif resources/drive.hvif
	mkdir -p $(BUILD)
	$(RC) -I resources -o $@ resources/airImager.rdef

$(BUILD)/airImager: $(APP_OBJ) $(ENGINE_OBJ) $(BUILD)/airImager.rsrc
	$(CXX) $(APP_LDFLAGS) -o $@.new $(APP_OBJ) $(ENGINE_OBJ) $(LIBS) \
		$(APP_LDEND)
	$(XRES) -o $@.new $(BUILD)/airImager.rsrc
	$(MIMESET) -f $@.new
	mv $@.new $@

$(BUILD)/src/engine/%.o: src/engine/%.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(HAIKU_CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/third_party/%.o: third_party/%.c
	mkdir -p $(dir $@)
	$(CXX) -Ithird_party/xz-embedded $(XZ_CPPFLAGS) $(XZ_CFLAGS) -c $< -o $@

$(BUILD)/engine_tests: tests/EngineTests.cpp $(ENGINE_OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ tests/EngineTests.cpp $(ENGINE_OBJ) \
		-lpthread

check: $(BUILD)/engine_tests
	$(BUILD)/engine_tests

check-host:
	$(MAKE) BUILD=build-host CXX=g++ check

package:
	bash tools/package-haiku.sh

# The vector icon is generated from code (tools/make-icon.py).
icon:
	python3 tools/make-icon.py resources

clean:
	rm -rf build-haiku build-host build-cross build-x86_64 build-arm64

-include $(APP_OBJ:.o=.d) $(ENGINE_OBJ:.o=.d)
