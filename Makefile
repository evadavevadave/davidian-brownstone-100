# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
# Linux / macOS / MinGW build of brownstone100.lv2 (plugin + GUI where a window layer exists)
UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  LIB_EXT  := .dylib
  LDFLAGS  += -dynamiclib
  UI_CLASS :=
else ifneq (,$(findstring MINGW,$(UNAME)))
  LIB_EXT  := .dll
  LDFLAGS  += -shared -static-libgcc
  UI_CLASS := ui:WindowsUI
  UI_LIBS  := -luser32 -lgdi32
else
  LIB_EXT  := .so
  LDFLAGS  += -shared -Wl,--no-undefined
  UI_CLASS := ui:X11UI
  UI_LIBS  := -lX11
endif

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
CFLAGS  += -fPIC -fvisibility=hidden -std=c99 -D_DEFAULT_SOURCE
BUNDLE  := build/brownstone100.lv2
LV2DIR  ?= $(HOME)/.lv2

TARGETS := $(BUNDLE)/brownstone100$(LIB_EXT) $(BUNDLE)/manifest.ttl $(BUNDLE)/brownstone100.ttl
ifneq ($(UI_CLASS),)
  TARGETS += $(BUNDLE)/brownstone100_ui$(LIB_EXT)
endif

all: $(TARGETS)

$(BUNDLE) build-test docs:
	mkdir -p $@

$(BUNDLE)/brownstone100$(LIB_EXT): src/brownstone100.c src/lv2_core.h src/ports.h | $(BUNDLE)
	$(CC) $(CFLAGS) -ffast-math $< -o $@ $(LDFLAGS) -lm

$(BUNDLE)/brownstone100_ui$(LIB_EXT): src/ui/ui_lv2.c src/ui/*.h src/ports.h | $(BUNDLE)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(UI_LIBS) -lm

$(BUNDLE)/manifest.ttl: brownstone100.lv2/manifest.ttl.in | $(BUNDLE)
ifeq ($(UI_CLASS),)
	sed -e '/# UI-BEGIN/,/# UI-END/d' -e 's/@LIB_EXT@/$(LIB_EXT)/g' $< > $@
else
	sed -e 's/@LIB_EXT@/$(LIB_EXT)/g' -e 's/@UI_CLASS@/$(UI_CLASS)/' $< > $@
endif

$(BUNDLE)/brownstone100.ttl: brownstone100.lv2/brownstone100.ttl | $(BUNDLE)
	cp $< $@

test: build-test/render build-test/ui_render | docs
	cd build-test && ./render
	./build-test/ui_render docs/gui.bmp

build-test/render: test/render.c src/brownstone100.c | build-test
	$(CC) $(CFLAGS) -ffast-math $< -o $@ -lm

build-test/ui_render: test/ui_render.c src/ui/*.h | build-test
	$(CC) $(CFLAGS) $< -o $@ -lm

install: all
	mkdir -p $(LV2DIR)
	cp -r $(BUNDLE) $(LV2DIR)/

clean:
	rm -rf build build-test

.PHONY: all test install clean
