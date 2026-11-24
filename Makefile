# Convenience wrapper around idf.py and doxygen.
#
#   make              build firmware
#   make flash        flash (port auto-detected; override with PORT=/dev/ttyACMx)
#   make monitor      serial monitor
#   make run          flash + monitor
#   make menuconfig   edit sdkconfig
#   make docs         generate Doxygen HTML into docs/html
#   make screenshot   save the device screen to screenshot.png (OUT=file.png)
#   make touch X=160 Y=120 [MS=100]   simulate a tap on the device
#   make swipe X1=40 Y1=120 X2=280 Y2=120 [MS=300]   simulate a drag
#
# ESP-IDF is activated per command via IDF_ACTIVATE, so no prior
# `source export.sh` is needed. Override if IDF lives elsewhere:
#   make IDF_ACTIVATE=/path/to/activate_idf_vX.sh

SHELL        := /bin/bash
IDF_ACTIVATE ?= $(HOME)/.espressif/tools/activate_idf_v6.1.sh
PORT         ?=

IDF := source "$(IDF_ACTIVATE)" >/dev/null 2>&1 && idf.py
PORT_ARG := $(if $(PORT),-p $(PORT))
PY   := source "$(IDF_ACTIVATE)" >/dev/null 2>&1 && python
TOOL := $(PY) tools/gizmo.py $(if $(PORT),--port $(PORT))
OUT  ?= screenshot.png

DOXYGEN  ?= doxygen
DOCS_DIR := docs

export GIZMO_VERSION  := $(shell git describe --tags --always --dirty 2>/dev/null)
export GIZMO_HAVE_DOT := $(if $(shell command -v dot 2>/dev/null),YES,NO)

.PHONY: all build flash monitor run menuconfig size clean fullclean docs docs-open docs-clean help screenshot touch swipe

all: build

build:
	$(IDF) build

flash:
	$(IDF) $(PORT_ARG) flash

monitor:
	$(IDF) $(PORT_ARG) monitor

run:
	$(IDF) $(PORT_ARG) flash monitor

menuconfig:
	$(IDF) menuconfig

size:
	$(IDF) size

clean:
	$(IDF) clean

fullclean:
	$(IDF) fullclean

docs:
	@command -v $(DOXYGEN) >/dev/null || { echo "doxygen not found (apt install doxygen graphviz)"; exit 1; }
	@mkdir -p $(DOCS_DIR)
	$(DOXYGEN) Doxyfile
	@if [ -s $(DOCS_DIR)/warnings.log ]; then echo "Doxygen warnings:"; cat $(DOCS_DIR)/warnings.log; fi
	@echo "Docs: $(DOCS_DIR)/html/index.html"

docs-open: docs
	xdg-open $(DOCS_DIR)/html/index.html >/dev/null 2>&1 &

docs-clean:
	rm -rf $(DOCS_DIR)

screenshot:
	$(TOOL) screenshot -o $(OUT)

touch:
	@test -n "$(X)" -a -n "$(Y)" || { echo "usage: make touch X=<x> Y=<y> [MS=<ms>]"; exit 1; }
	$(TOOL) touch $(X) $(Y) $(MS)

swipe:
	@test -n "$(X1)" -a -n "$(Y1)" -a -n "$(X2)" -a -n "$(Y2)" || { echo "usage: make swipe X1=.. Y1=.. X2=.. Y2=.. [MS=..]"; exit 1; }
	$(TOOL) swipe $(X1) $(Y1) $(X2) $(Y2) $(MS)

help:
	@sed -n '2,13p' Makefile | sed 's/^# \{0,1\}//'
