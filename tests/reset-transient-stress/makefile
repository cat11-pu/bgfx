#
# Copyright 2011-2026 Branimir Karadzic. All rights reserved.
# License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
#

BGFX_DIR := ../..
BUILD_DIR := $(BGFX_DIR)/.build
PROJECT_DIR := $(BUILD_DIR)/projects/gmake-linux-gcc
LIB_DIR := $(BUILD_DIR)/linux64_gcc/bin
BIN := $(BUILD_DIR)/reset-transient-stress

CXX ?= g++
CXXFLAGS := -std=c++20 -O2 -g -Wall -Wextra -Werror -fno-rtti -fno-exceptions -DBX_CONFIG_DEBUG=1 \
	-I$(BGFX_DIR)/include \
	-I$(BGFX_DIR)/3rdparty \
	-I$(BGFX_DIR)/../bx/include \
	-I$(BGFX_DIR)/../bimg/include

LIBS := -Wl,--allow-multiple-definition \
	-Wl,--start-group \
	$(LIB_DIR)/libbgfxDebug.a \
	$(LIB_DIR)/libbimgDebug.a \
	$(LIB_DIR)/libbxDebug.a \
	-Wl,--end-group \
	-lX11 -lGL -lpthread -ldl

.PHONY: all build run clean

all: build

$(PROJECT_DIR)/Makefile:
	cd $(BGFX_DIR) && ../bx/tools/bin/linux/genie --gcc=linux-gcc gmake

$(LIB_DIR)/libbgfxDebug.a: $(PROJECT_DIR)/Makefile
	$(MAKE) -C $(PROJECT_DIR) -j$$(nproc) bgfx config=debug64

build: $(BIN)

$(BIN): reset_transient_stress.cpp $(LIB_DIR)/libbgfxDebug.a
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIBS)

run: $(BIN)
	cd $(BGFX_DIR) && $(BIN) $(ARGS)

clean:
	rm -f $(BIN)
